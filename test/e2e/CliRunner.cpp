#include "CliRunner.h"

#include "maketex/Process.h"

#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unistd.h>

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
