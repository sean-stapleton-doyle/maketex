#pragma once

#include "maketex/StateDatabase.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace maketex::build {

// Builds a combined (size, mtime) stamp for each artifact present in outDir
// alongside its published copy in destDir. Returns empty if any source
// artifact is missing its published counterpart (or cannot be stat'd).
std::string publishedStamp(const std::filesystem::path& outDir,
    const std::filesystem::path& destDir, std::string_view jobName);

// True when an up-to-date build's existing published artifacts still match the
// recorded stamp and do not need to be re-read or copied.
bool canReusePublished(bool upToDate,
    const std::optional<std::string>& recordedStamp,
    std::string_view currentStamp);

// Reuses unchanged published artifacts on up-to-date runs or publishes fresh
// copies and records the resulting stamp in stateDb.
std::vector<std::filesystem::path> publishResultArtifacts(bool upToDate,
    const std::filesystem::path& outDir,
    const std::filesystem::path& destDir,
    const std::string& resolvedJob,
    StateDatabase& stateDb);

} // namespace maketex::build
