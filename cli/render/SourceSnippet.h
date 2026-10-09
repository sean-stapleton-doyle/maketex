#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace maketex::cli {

// Reads line `line` (1-based) from path for compiler-style source context
// under a diagnostic. Returns nullopt when the file is missing, the line is
// out of range, or the file exceeds an internal size guard.
std::optional<std::string> readSourceLine(
    const std::filesystem::path& path, int line);

} // namespace maketex::cli
