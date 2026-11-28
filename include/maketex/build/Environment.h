#pragma once

#include "maketex/Build.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace maketex::build {

// Process environment lookup seam. Returns nullopt when the variable is unset
// and an empty string when set to empty, preserving std::getenv semantics.
using EnvLookup = std::function<std::optional<std::string>(std::string_view)>;

// Default lookup backed by std::getenv.
EnvLookup processEnv();

// Joins configured kpathsea search directories with ':' and appends any
// inherited value (or a trailing ':' so TeX Live's built-in defaults stay
// on the search path). Defaults an empty env to processEnv().
std::vector<std::pair<std::string, std::string>> searchPathEnv(
    const BuildOptions& options, const EnvLookup& env = { });

// Prepends outDir (with a trailing ':') to any inherited TEXFORMATS value.
// Defaults an empty env to processEnv().
std::string prependTexFormats(
    const std::filesystem::path& outDir, const EnvLookup& env = { });

} // namespace maketex::build
