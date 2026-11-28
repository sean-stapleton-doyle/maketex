#pragma once

#include "maketex/parsers/FlsParser.h"

#include <filesystem>
#include <vector>

namespace maketex::build {

// True when path p is located at or underneath root (component-wise prefix).
bool isUnderRoot(const std::filesystem::path& p, const std::filesystem::path& root);

// Deduplicated input paths from record that were never written as outputs.
std::vector<std::filesystem::path> sourcesFromFls(const FlsRecord& record);

} // namespace maketex::build
