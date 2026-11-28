#pragma once

#include <filesystem>
#include <string_view>

namespace maketex::build {

// True when a file with p's extension should cause its parent directory to be
// mirrored into the output directory for TeX / graphics tools.
bool shouldMirrorForFile(const std::filesystem::path& p);

// True for directory names that should never be descended into while mirroring
// (hidden directories, node_modules, __pycache__).
bool shouldSkipMirrorDir(std::string_view dirName);

// Mirrors source subdirectories holding TeX/graphics assets into outDir so
// \include and graphics converters can write into them.
void mirrorSourceDirectories(
    const std::filesystem::path& texDir, const std::filesystem::path& outDir);

// Writes a catch-all .gitignore into outDir if one does not already exist.
void ignoreInVersionControl(const std::filesystem::path& outDir);

} // namespace maketex::build
