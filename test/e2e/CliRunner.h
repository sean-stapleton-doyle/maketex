#pragma once

// End-to-end tests drive the real `maketex` binary the way a user does:
// arguments, working directory, environment, exit code, stdout, stderr.

#include <chrono>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace maketex::e2e {

struct CliResult {
    int exitCode = -1;
    std::string out;
    std::string err;
    bool timedOut = false;
};

// Runs the maketex binary under test. Each runner has an isolated HOME and
// XDG_CACHE_HOME (no user config, no shared package cache); `env` entries
// are added on top and win.
class CliRunner {
public:
    explicit CliRunner(std::filesystem::path sandbox);

    CliResult run(const std::vector<std::string>& args, const std::filesystem::path& cwd,
        std::chrono::seconds timeout = std::chrono::seconds(120)) const;

    // Runs the binary attached to a pseudo-terminal — as from an
    // interactive shell — typing `input` into it. stdout and stderr arrive
    // interleaved in `out`, as the user would see them.
    CliResult runInTerminal(const std::vector<std::string>& args, const std::filesystem::path& cwd,
        const std::string& input, std::chrono::seconds timeout = std::chrono::seconds(120)) const;

    void setEnv(const std::string& name, std::string value) { m_env[name] = std::move(value); }
    // Prepends dir to PATH for every run.
    void prependPath(const std::filesystem::path& dir);

    // The environment runs use: the parent's, plus overrides.
    std::vector<std::pair<std::string, std::string>> environment() const;

    static std::filesystem::path binary();

private:
    std::map<std::string, std::string> m_env;
};

// Stub programs in a directory placed first on PATH. A stub is a shell
// script: it appends "<name> <args>" to the call log, then runs `body`.
class FakeToolchain {
public:
    explicit FakeToolchain(std::filesystem::path dir);

    const std::filesystem::path& dir() const { return m_dir; }

    void add(const std::string& name, const std::string& body);

    // A stub that answers the given `args` patterns (shell `case` globs,
    // e.g. "-var-value=TEXMFROOT") itself and hands everything else to the
    // real program found on PATH now.
    void wrap(const std::string& name, const std::vector<std::pair<std::string, std::string>>& cases);

    // Lines "<name> <args>" for every stub invocation so far.
    std::vector<std::string> calls() const;

private:
    std::filesystem::path m_dir;
    std::filesystem::path m_log;
};

// Whether text contains needle.
bool contains(const std::string& text, const std::string& needle);

} // namespace maketex::e2e
