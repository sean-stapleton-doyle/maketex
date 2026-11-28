#pragma once

#include "maketex/ProcessRunner.h"
#include "maketex/build/Environment.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace maketex::build {

// Searches PATH (from env, defaulting to processEnv()) for an executable
// regular file named program.
std::optional<std::filesystem::path> findOnPath(
    std::string_view program, const EnvLookup& env = { });

// Cache directory for maketex metadata: $XDG_CACHE_HOME/maketex, else
// $HOME/.cache/maketex, else empty.
std::filesystem::path defaultCacheDir(const EnvLookup& env = { });

// Returns "<size>:<mtime_ns>" for path, or empty on stat failure.
std::string fileStampToken(const std::filesystem::path& path);

// Cache key capturing the kpsewhich binary path, its stamp, and every
// TeX-relevant environment variable that changes kpsewhich expansion.
std::string rootsCacheKey(const std::filesystem::path& kpsePath,
    std::string_view stamp, const EnvLookup& env = { });

// Parses the `|`-separated stdout of `kpsewhich -expand-var=...`, stripping
// trailing CR/LF and leading `!!` ls-R markers and dropping unset variables
// or path lists ({a,b}, a:b, a;b).
std::vector<std::filesystem::path> parseKpsewhichRoots(std::string_view stdoutOutput);

// Reads cached TeX system roots when cacheFile exists and its first line
// matches expectedKey.
std::optional<std::vector<std::filesystem::path>> readRootsCache(
    const std::filesystem::path& cacheFile, std::string_view expectedKey);

// Best-effort atomic write of key and roots to cacheFile.
void writeRootsCache(const std::filesystem::path& cacheFile,
    std::string_view key, const std::vector<std::filesystem::path>& roots);

// Resolves TeX distribution roots via cache or kpsewhich (using runner and
// env, defaulting to runSystemProcess and processEnv()).
std::vector<std::filesystem::path> resolveTexSystemRoots(
    const ProcessRunner& runner = { }, const EnvLookup& env = { });

} // namespace maketex::build
