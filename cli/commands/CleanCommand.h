#pragma once

#include "EffectiveOptions.h"

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace maketex::cli {

// External seams for runCleanCommand so legacy layout sweeps, shared state DB
// policies, dry-run formatting, and filesystem removal errors are unit-testable.
class ICleanEnv {
public:
    virtual ~ICleanEnv() = default;

    virtual std::optional<BuildFileSets> scanBuildFiles(const BuildOptions& build) = 0;
    virtual std::vector<std::filesystem::path> publishedArtifactPaths(
        const BuildOptions& build) = 0;
    virtual std::vector<std::string> readDbJobs(const std::filesystem::path& db) = 0;
    virtual void forgetDbJob(const std::filesystem::path& db,
        const std::string& jobName) = 0;
    virtual std::uintmax_t removeAll(const std::filesystem::path& path,
        std::error_code& ec) = 0;
    virtual void removeIfEmptyDir(const std::filesystem::path& dir) = 0;
    virtual std::ostream& out() = 0;
    virtual std::ostream& err() = 0;
};

// Removes generated files. Returns the process exit code.
int runCleanCommand(const EffectiveOptions& options, ICleanEnv& env);
int runCleanCommand(const EffectiveOptions& options);

} // namespace maketex::cli
