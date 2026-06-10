#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <sys/types.h>

namespace maketex {

struct ProcessResult {
    int exitCode;
    std::string stdoutOutput;
    std::string stderrOutput;
};

// Launches a subprocess and captures its stdout and stderr.
// Construction spawns immediately; call wait() to block until exit.
// The destructor sends SIGTERM and reaps the process if wait() was never called.
class Process {
public:
    // Spawns the process. Throws std::system_error on spawn failure.
    // argv[0] is looked up via PATH (posix_spawnp semantics).
    // If workingDir is non-empty the child process starts in that directory.
    explicit Process(std::span<const std::string> argv,
        const std::filesystem::path& workingDir = {});

    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    Process(Process&&) noexcept;
    Process& operator=(Process&&) noexcept;

    ~Process();

    // Drains stdout and stderr via poll(), then waits for the process to exit.
    // Call at most once; behaviour is undefined if called twice.
    ProcessResult wait();

    // Sends SIGTERM. Safe to call multiple times or before wait().
    void kill() noexcept;

private:
    pid_t m_pid { -1 };
    int m_stdoutFd { -1 };
    int m_stderrFd { -1 };
};

} // namespace maketex
