#pragma once

#include "maketex/Process.h"

#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace maketex {

// Everything needed to spawn one subprocess and wait for it.
struct ProcessLaunchSpec {
    std::vector<std::string> argv;
    std::filesystem::path workingDir;
    std::vector<std::pair<std::string, std::string>> extraEnv;
    WaitLimits limits;
};

// Subprocess execution seam shared by engines, auxiliary tools, and build
// helpers. Must be safe to call concurrently (BuildDriver runs a tool wave on
// parallel threads). Throws std::system_error on spawn failure exactly like
// Process does; ENOENT for a missing program is translated to notFound by
// launchProcess.
using ProcessRunner = std::function<ProcessResult(const ProcessLaunchSpec&)>;

// Default runner: spawns directly via maketex::Process.
ProcessResult runSystemProcess(const ProcessLaunchSpec& spec);

} // namespace maketex
