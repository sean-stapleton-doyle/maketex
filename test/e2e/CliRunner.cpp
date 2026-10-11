#include "CliRunner.h"

#include "maketex/Process.h"

#include <array>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unistd.h>

#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#if __has_include(<util.h>)
#include <util.h> // forkpty on macOS
#else
#include <pty.h> // forkpty on Linux
#endif

extern char** environ;

namespace fs = std::filesystem;

namespace maketex::e2e {

namespace {

    std::optional<fs::path> onPath(const std::string& name)
    {
        const char* path = std::getenv("PATH");
        std::string_view rest = path ? path : "";
        while (!rest.empty()) {
            auto colon = rest.find(':');
            auto dir = rest.substr(0, colon);
            auto candidate = fs::path(dir) / name;
            if (!dir.empty() && ::access(candidate.c_str(), X_OK) == 0)
                return candidate;
            if (colon == std::string_view::npos)
                break;
            rest.remove_prefix(colon + 1);
        }
        return std::nullopt;
    }

    // Single quotes for the shell, with embedded quotes escaped.
    std::string shellQuote(const std::string& s)
    {
        std::string out = "'";
        for (char c : s)
            out += c == '\'' ? std::string("'\\''") : std::string(1, c);
        return out + "'";
    }

} // namespace

CliRunner::CliRunner(fs::path sandbox)
{
    fs::create_directories(sandbox / "home");
    fs::create_directories(sandbox / "cache");
    m_env["HOME"] = (sandbox / "home").string();
    m_env["XDG_CACHE_HOME"] = (sandbox / "cache").string();
    // Deterministic, plain output whatever the developer's terminal says.
    m_env["NO_COLOR"] = "1";
}

void CliRunner::prependPath(const fs::path& dir)
{
    auto current = m_env.contains("PATH") ? m_env["PATH"] : std::string(std::getenv("PATH"));
    m_env["PATH"] = dir.string() + ":" + current;
}

std::vector<std::pair<std::string, std::string>> CliRunner::environment() const
{
    return { m_env.begin(), m_env.end() };
}

fs::path CliRunner::binary()
{
    return MAKETEX_BINARY;
}

CliResult CliRunner::run(const std::vector<std::string>& args, const fs::path& cwd,
    std::chrono::seconds timeout) const
{
    std::vector<std::string> argv { binary().string() };
    argv.insert(argv.end(), args.begin(), args.end());
    auto env = environment();
    auto r = Process(argv, cwd, env).wait({ .timeout = timeout });
    return CliResult {
        .exitCode = r.exitCode,
        .out = std::move(r.stdoutOutput),
        .err = std::move(r.stderrOutput),
        .timedOut = r.timedOut,
    };
}

CliResult CliRunner::runInTerminal(const std::vector<std::string>& args, const fs::path& cwd,
    const std::string& input, std::chrono::seconds timeout) const
{
    // Everything the child needs is prepared before fork: after it, only
    // async-signal-safe calls (chdir, execve) may run in this threaded
    // process.
    std::vector<std::string> argvStorage { binary().string() };
    argvStorage.insert(argvStorage.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (auto& a : argvStorage)
        argv.push_back(a.data());
    argv.push_back(nullptr);

    std::map<std::string, std::string> merged;
    for (char** e = environ; *e; ++e) {
        std::string_view entry(*e);
        auto eq = entry.find('=');
        merged[std::string(entry.substr(0, eq))] = std::string(entry.substr(eq + 1));
    }
    for (const auto& [k, v] : m_env)
        merged[k] = v;
    merged.erase("NO_COLOR"); // a terminal user's view
    std::vector<std::string> envStorage;
    for (const auto& [k, v] : merged)
        envStorage.push_back(k + "=" + v);
    std::vector<char*> envp;
    for (auto& e : envStorage)
        envp.push_back(e.data());
    envp.push_back(nullptr);
    std::string dir = cwd.string();

    int master = -1;
    pid_t pid = ::forkpty(&master, nullptr, nullptr, nullptr);
    if (pid < 0)
        throw std::runtime_error("forkpty failed");
    if (pid == 0) {
        if (::chdir(dir.c_str()) != 0)
            ::_exit(126);
        ::execve(argv[0], argv.data(), envp.data());
        ::_exit(127);
    }

    if (!input.empty())
        (void)::write(master, input.data(), input.size());

    CliResult result;
    auto deadline = std::chrono::steady_clock::now() + timeout;
    std::array<char, 4096> buf;
    while (true) {
        auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) {
            ::kill(pid, SIGKILL);
            result.timedOut = true;
            break;
        }
        pollfd pfd { master, POLLIN, 0 };
        if (::poll(&pfd, 1, static_cast<int>(left.count())) <= 0)
            continue;
        auto n = ::read(master, buf.data(), buf.size());
        if (n <= 0)
            break; // EIO once the child has exited and closed the terminal
        result.out.append(buf.data(), static_cast<size_t>(n));
    }
    ::close(master);
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    // Terminals turn \n into \r\n.
    std::erase(result.out, '\r');
    return result;
}

FakeToolchain::FakeToolchain(fs::path dir)
    : m_dir(std::move(dir))
    , m_log(m_dir / "calls.log")
{
    fs::create_directories(m_dir);
    std::ofstream(m_log).flush();
}

void FakeToolchain::add(const std::string& name, const std::string& body)
{
    auto script = m_dir / name;
    std::ofstream(script) << "#!/bin/sh\n"
                          << "echo \"" << name << " $*\" >> " << shellQuote(m_log.string()) << "\n"
                          << body << "\n";
    fs::permissions(script, fs::perms::owner_all);
}

void FakeToolchain::wrap(const std::string& name,
    const std::vector<std::pair<std::string, std::string>>& cases)
{
    auto real = onPath(name);
    if (!real)
        throw std::runtime_error("cannot wrap " + name + ": not on PATH");
    std::string body = "case \"$*\" in\n";
    for (const auto& [pattern, answer] : cases)
        body += "  " + pattern + ") " + answer + " ;;\n";
    body += "  *) exec " + shellQuote(real->string()) + " \"$@\" ;;\nesac";
    add(name, body);
}

std::vector<std::string> FakeToolchain::calls() const
{
    std::vector<std::string> lines;
    std::ifstream in(m_log);
    std::string line;
    while (std::getline(in, line))
        lines.push_back(line);
    return lines;
}

bool contains(const std::string& text, const std::string& needle)
{
    return text.find(needle) != std::string::npos;
}

} // namespace maketex::e2e
