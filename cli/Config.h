#pragma once

#include "maketex/ToolFactory.h"
#include "maketex/engines/EngineFactory.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace maketex::cli {

// Values read from maketex.toml. Fields are optional so the merge layer can
// tell "set in config" from "library default". Relative paths are resolved
// against the config file's directory.
struct Config {
    std::optional<std::filesystem::path> main;
    std::optional<EngineKind> engine;
    std::optional<std::string> engineProgram;
    std::vector<std::string> engineOptions;
    std::optional<std::vector<ToolKind>> tools;
    std::optional<std::filesystem::path> outputDir;
    std::optional<bool> shellEscape;
    std::optional<bool> synctex;
    std::optional<bool> haltOnError;
    std::optional<int> maxPasses;
    std::optional<int> timeoutSeconds;
    std::optional<bool> installMissing;

    std::optional<int> watchIntervalMs;
    std::vector<std::filesystem::path> watchExtraPaths;
    std::vector<std::string> cleanExtraExtensions;

    std::vector<std::string> warnings; // unknown keys — reported, not fatal
    std::filesystem::path sourceFile;
};

// Nearest maketex.toml in startDir or a parent, or nullopt. The search
// stops at the repository root (.git/.hg/.svn) and before $HOME.
std::optional<std::filesystem::path> findConfigFile(
    const std::filesystem::path& startDir);

// Parses TOML text as if it lived at sourceFile (seam for tests).
// The error is a user-facing message including file and line.
std::expected<Config, std::string> parseConfig(std::string_view toml,
    const std::filesystem::path& sourceFile);

std::expected<Config, std::string> loadConfig(const std::filesystem::path& path);

} // namespace maketex::cli
