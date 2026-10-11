#include "maketex/Process.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdexcept>
#include <string_view>
#include <sys/wait.h>
#include <system_error>
#include <unistd.h>
#include <vector>

// Not declared in <unistd.h> on macOS; required by posix_spawnp.
extern char** environ;

namespace maketex {

namespace {

    // Closes a raw fd on scope exit.
    // Replace with std::scope_exit once Apple's libc++ ships it (C++26).
    struct FdGuard {
        int fd;
        ~FdGuard()
        {
            if (fd >= 0)
                ::close(fd);
        }
    };

} // namespace

Process::Process(std::span<const std::string> argv,
    const std::filesystem::path& workingDir,
    std::span<const std::pair<std::string, std::string>> extraEnv)
{
    if (argv.empty())
        throw std::invalid_argument("Process: argv must not be empty");

    int pOut[2];
    int pErr[2];
    if (::pipe(pOut) != 0)
        throw std::system_error(errno, std::system_category(), "pipe(stdout)");
    // Guard all four ends so they're closed if anything below throws.
    FdGuard gOut0 { pOut[0] }, gOut1 { pOut[1] };
    FdGuard gErr0 { -1 }, gErr1 { -1 };

    if (::pipe(pErr) != 0)
        throw std::system_error(errno, std::system_category(), "pipe(stderr)");
    gErr0.fd = pErr[0];
    gErr1.fd = pErr[1];

    // Mark all four ends close-on-exec so the child only inherits what we
    // explicitly dup2 via file actions.
    for (int fd : { pOut[0], pOut[1], pErr[0], pErr[1] })
        ::fcntl(fd, F_SETFD, FD_CLOEXEC);

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    if (!workingDir.empty()) {
#ifdef MAKETEX_HAVE_SPAWN_ADDCHDIR
        posix_spawn_file_actions_addchdir(&fa, workingDir.c_str());
#else
        posix_spawn_file_actions_addchdir_np(&fa, workingDir.c_str());
#endif
    }
    posix_spawn_file_actions_addopen(&fa, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_adddup2(&fa, pOut[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fa, pErr[1], STDERR_FILENO);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    // The child starts with nothing blocked and default dispositions, even
    // when this process blocks signals to handle them on its own thread —
    // otherwise TeX would inherit the mask and ignore our SIGTERM.
    sigset_t noSignals;
    sigemptyset(&noSignals);
    posix_spawnattr_setsigmask(&attr, &noSignals);
    sigset_t defaults;
    sigemptyset(&defaults);
    for (int sig : { SIGINT, SIGTERM, SIGHUP, SIGPIPE, SIGUSR1 })
        sigaddset(&defaults, sig);
    posix_spawnattr_setsigdefault(&attr, &defaults);
    short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
    // macOS: the child gets only the descriptors set up above, never one
    // the parent happens to hold (the state database, a watched file).
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#endif
    posix_spawnattr_setflags(&attr, flags);

    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (const auto& s : argv)
        cargv.push_back(const_cast<char*>(s.c_str())); // NOLINT — POSIX API
    cargv.push_back(nullptr);

    // Inherited environment with extraEnv overrides appended. envStorage owns
    // the "KEY=VALUE" strings; cenvp must outlive posix_spawnp only.
    std::vector<std::string> envStorage;
    std::vector<char*> cenv;
    char** cenvp = environ;
    if (!extraEnv.empty()) {
        auto overridden = [&](std::string_view entry) {
            auto eq = entry.find('=');
            auto name = entry.substr(0, eq);
            for (const auto& [key, value] : extraEnv)
                if (key == name)
                    return true;
            return false;
        };
        for (char** e = environ; *e; ++e)
            if (!overridden(*e))
                cenv.push_back(*e);
        envStorage.reserve(extraEnv.size());
        for (const auto& [key, value] : extraEnv) {
            std::string entry = key;
            entry += '=';
            entry += value;
            envStorage.push_back(std::move(entry));
            cenv.push_back(envStorage.back().data());
        }
        cenv.push_back(nullptr);
        cenvp = cenv.data();
    }

    pid_t pid;
    int err = ::posix_spawnp(&pid, cargv[0], &fa, &attr, cargv.data(), cenvp);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&attr);

    if (err != 0)
        throw std::system_error(err, std::system_category(),
            std::string("posix_spawnp: ") + cargv[0]);

    // Spawn succeeded — parent takes ownership of the read ends and releases
    // the guards so they aren't closed on scope exit.
    m_pid = pid;
    m_stdoutFd = pOut[0];
    m_stderrFd = pErr[0];
    gOut0.fd = -1;
    gErr0.fd = -1;
    // Write ends (gOut1, gErr1) are intentionally left to close via RAII;
    // the parent must not hold them open or the pipes never reach EOF.
}

Process::Process(Process&& other) noexcept
    : m_pid(std::exchange(other.m_pid, -1))
    , m_stdoutFd(std::exchange(other.m_stdoutFd, -1))
    , m_stderrFd(std::exchange(other.m_stderrFd, -1))
{
}

Process& Process::operator=(Process&& other) noexcept
{
    if (this != &other) {
        this->~Process();
        m_pid = std::exchange(other.m_pid, -1);
        m_stdoutFd = std::exchange(other.m_stdoutFd, -1);
        m_stderrFd = std::exchange(other.m_stderrFd, -1);
    }
    return *this;
}

Process::~Process()
{
    // Close our pipe ends first so a child blocked writing into a full pipe
    // sees EPIPE instead of hanging the reap.
    if (m_stdoutFd != -1) {
        ::close(m_stdoutFd);
        m_stdoutFd = -1;
    }
    if (m_stderrFd != -1) {
        ::close(m_stderrFd);
        m_stderrFd = -1;
    }
    if (m_pid != -1)
        terminateAndReap();
}

void Process::kill() noexcept
{
    if (m_pid != -1)
        ::kill(m_pid, SIGTERM);
}

int Process::terminateAndReap() noexcept
{
    using namespace std::chrono_literals;
    int status = 0;
    ::kill(m_pid, SIGTERM);
    // TeX and the tools exit promptly on SIGTERM; something wedged (a
    // shell-escape child ignoring signals) gets SIGKILL after the grace.
    auto deadline = std::chrono::steady_clock::now() + 2s;
    while (true) {
        pid_t r = ::waitpid(m_pid, &status, WNOHANG);
        if (r == m_pid || (r < 0 && errno != EINTR))
            break;
        if (std::chrono::steady_clock::now() >= deadline) {
            ::kill(m_pid, SIGKILL);
            while (::waitpid(m_pid, &status, 0) < 0 && errno == EINTR) { }
            break;
        }
        ::usleep(10'000);
    }
    m_pid = -1;
    return status;
}

ProcessResult Process::wait(const WaitLimits& limits)
{
    using Clock = std::chrono::steady_clock;
    ProcessResult result;

    struct pollfd fds[2] = {
        { m_stdoutFd, POLLIN, 0 },
        { m_stderrFd, POLLIN, 0 },
    };
    std::string* bufs[2] = { &result.stdoutOutput, &result.stderrOutput };
    int open = 2;

    // Closes one pipe end and mirrors the change into the member fd, so the
    // destructor never double-closes if poll/read throws mid-drain.
    auto closeEnd = [&](int i) {
        ::close(fds[i].fd);
        fds[i].fd = -1;
        (i == 0 ? m_stdoutFd : m_stderrFd) = -1;
        --open;
    };

    std::optional<Clock::time_point> deadline;
    if (limits.timeout)
        deadline = Clock::now() + *limits.timeout;
    // With a limit to watch, wake periodically to check it; otherwise block.
    const bool limited = deadline || limits.stop.stop_possible();
    constexpr int kSliceMs = 100;

    std::array<char, 65536> buf;

    while (open > 0) {
        if (limits.stop.stop_requested()) {
            result.cancelled = true;
            break;
        }
        if (deadline && Clock::now() >= *deadline) {
            result.timedOut = true;
            break;
        }
        int n = ::poll(fds, 2, limited ? kSliceMs : -1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            throw std::system_error(errno, std::system_category(), "poll");
        }
        for (int i = 0; i < 2; ++i) {
            if (fds[i].fd < 0)
                continue;
            if (fds[i].revents & (POLLIN | POLLHUP)) {
                ssize_t r = ::read(fds[i].fd, buf.data(), buf.size());
                if (r > 0)
                    bufs[i]->append(buf.data(), static_cast<size_t>(r));
                else if (r == 0 || errno != EINTR)
                    closeEnd(i); // EOF, or a real read error
            } else if (fds[i].revents & (POLLERR | POLLNVAL)) {
                closeEnd(i);
            }
        }
    }

    if (result.cancelled || result.timedOut) {
        for (int i = 0; i < 2; ++i)
            if (fds[i].fd >= 0)
                closeEnd(i);
        terminateAndReap();
        result.exitCode = -1;
        return result;
    }

    int status = 0;
    while (::waitpid(m_pid, &status, 0) < 0) {
        if (errno != EINTR)
            throw std::system_error(errno, std::system_category(), "waitpid");
    }
    m_pid = -1;

    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

} // namespace maketex
