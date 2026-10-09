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
    if (!workingDir.empty())
        posix_spawn_file_actions_addchdir(&fa, workingDir.c_str());
    posix_spawn_file_actions_adddup2(&fa, pOut[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fa, pErr[1], STDERR_FILENO);

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
    int err = ::posix_spawnp(&pid, cargv[0], &fa, nullptr, cargv.data(), cenvp);
    posix_spawn_file_actions_destroy(&fa);

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
    if (m_pid != -1) {
        ::kill(m_pid, SIGTERM);
        // Close our pipe ends before waitpid so the child isn't blocked
        // trying to write into a full kernel buffer.
        if (m_stdoutFd != -1) {
            ::close(m_stdoutFd);
            m_stdoutFd = -1;
        }
        if (m_stderrFd != -1) {
            ::close(m_stderrFd);
            m_stderrFd = -1;
        }
        int status;
        ::waitpid(m_pid, &status, 0);
        m_pid = -1;
    }
    if (m_stdoutFd != -1)
        ::close(m_stdoutFd);
    if (m_stderrFd != -1)
        ::close(m_stderrFd);
}

void Process::kill() noexcept
{
    if (m_pid != -1)
        ::kill(m_pid, SIGTERM);
}

ProcessResult Process::wait()
{
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

    std::array<char, 65536> buf;

    while (open > 0) {
        int n = ::poll(fds, 2, -1);
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
                if (r > 0) {
                    bufs[i]->append(buf.data(), static_cast<size_t>(r));
                } else {
                    // r == 0: EOF. r < 0 after POLLIN is unexpected; close anyway.
                    closeEnd(i);
                }
            } else if (fds[i].revents & (POLLERR | POLLNVAL)) {
                closeEnd(i);
            }
        }
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
