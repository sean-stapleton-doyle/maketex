#pragma once

#include <chrono>
#include <filesystem>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <sys/types.h>
#include <utility>

namespace maketex {

struct ProcessResult {
    int exitCode = -1; // -1 when the process was killed or died by signal
    std::string stdoutOutput;
    std::string stderrOutput;
    bool timedOut = false; // killed because the timeout expired
    bool cancelled = false; // killed because the stop token fired
};

// How long wait() may block, and how a caller can interrupt it.
struct WaitLimits {
    // Kill the process (SIGTERM, then SIGKILL after a grace period) once it
    // has run this long. Unset waits indefinitely.
    std::optional<std::chrono::milliseconds> timeout;
    // Kill the process when stop is requested — how watch mode's quit
    // interrupts an engine pass.
    std::stop_token stop;
};

// Launches a subprocess and captures its stdout and stderr. Its stdin is
// /dev/null — TeX must never wait on the user's terminal (which watch
// mode's TUI owns), and nonstopmode makes it not ask.
// Construction spawns immediately; call wait() to block until exit.
// The destructor terminates (SIGTERM, then SIGKILL) and reaps the process if
// wait() was never called.
class Process {
public:
    // Spawns the process. Throws std::system_error on spawn failure.
    // argv[0] is looked up via PATH (posix_spawnp semantics).
    // If workingDir is non-empty the child process starts in that directory.
    // extraEnv entries are added to the inherited environment, overriding
    // any variable of the same name.
    explicit Process(std::span<const std::string> argv,
        const std::filesystem::path& workingDir = { },
        std::span<const std::pair<std::string, std::string>> extraEnv = { });

    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    Process(Process&&) noexcept;
    Process& operator=(Process&&) noexcept;

    ~Process();

    // Drains stdout and stderr via poll(), then waits for the process to exit.
    // Call at most once; behaviour is undefined if called twice.
    ProcessResult wait(const WaitLimits& limits = { });

    // Sends SIGTERM. Safe to call multiple times or before wait().
    void kill() noexcept;

private:
    // SIGTERM, a grace period for cleanup, then SIGKILL; reaps the child.
    // Returns its wait status.
    int terminateAndReap() noexcept;

    pid_t m_pid { -1 };
    int m_stdoutFd { -1 };
    int m_stderrFd { -1 };
};

} // namespace maketex
