#pragma once

#include "maketex/Diagnostic.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace maketex::cli {

// Where the TeX installation came from — it decides how a missing package
// is installed.
enum class TexDistribution {
    TexLive, // upstream TeX Live, MacTeX, BasicTeX: tlmgr
    Debian, // apt-packaged TeX Live (tlmgr disabled)
    Fedora, // dnf-packaged TeX Live
    MiKTeX,
    Unknown,
};

TexDistribution detectDistribution();

// Answers "which package provides this file?" — `tlmgr search` in
// production, a fake in tests. nullopt when unknown or unreachable.
using PackageLookup = std::function<std::optional<std::string>(const std::string& file)>;

// The TeX Live package that provides `file` (a .sty/.cls name or a program
// like "biber"): a built-in table for common ones, else `lookup`.
std::optional<std::string> packageProviding(const std::string& file,
    const PackageLookup& lookup);

// The install command to suggest for `missing` on `distro`.
std::string installHint(const std::string& missing, TexDistribution distro,
    const PackageLookup& lookup);

// `tlmgr search --global --file` with a timeout, cached on disk under
// cacheDir (results are stable; the network round trip is not cheap).
PackageLookup tlmgrLookup(std::filesystem::path cacheDir);

// The per-user cache directory ($XDG_CACHE_HOME/maketex, ~/.cache/maketex).
std::filesystem::path userCacheDir();

// Whether d reports a missing package, class, or program that a TeX
// distribution could provide — not the user's own \input file.
bool isInstallable(const Diagnostic& d);

// Sets `hint` on every missing-file / missing-tool diagnostic that names
// what is missing and has no hint yet. Detects the distribution and looks
// packages up only when such a diagnostic exists.
void addInstallHints(std::vector<Diagnostic>& diagnostics);

} // namespace maketex::cli
