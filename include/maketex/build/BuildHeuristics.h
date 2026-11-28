#pragma once

#include "maketex/Diagnostic.h"
#include "maketex/engines/EngineFactory.h"

#include <filesystem>
#include <optional>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace maketex::build {

// True when raw TeX source text (comment-stripped up to the 256 KiB cap)
// contains commands that almost certainly require >= 2 passes.
bool likelyMultiPassText(std::string_view text);

// True when a cold build of texFile (auxFile absent) will almost certainly
// take >= 2 passes, so pass 1 can safely run with -draftmode.
bool likelyMultiPassColdBuild(
    const std::filesystem::path& texFile, const std::filesystem::path& auxFile);

// Whether failed pdflatex diagnostics indicate the document needs a Unicode
// engine (checking LuaLaTeX/"either" patterns before XeTeX-only ones).
std::optional<EngineKind> unicodeEngineNeeded(std::span<const Diagnostic> diagnostics);

// Optimisation modes selected for BuildDriver::Config.
struct DriverModes {
    bool draftFirstPass = false;
    bool xelatexXdvSplit = false;
};

// Decides whether to enable XeLaTeX XDV split or draft-mode first pass.
DriverModes chooseDriverModes(EngineKind engineKind, int maxPasses,
    bool xdvipdfmxAvailable, bool likelyMultiPassCold);

// Merges user-supplied per-extension regex filters with the built-in defaults
// (eps %%CreationDate, aux \@writefile) so user filters augment rather than
// overwrite defaults for the same extension.
std::vector<std::pair<std::string, std::regex>> mergeContentFilters(
    std::span<const std::pair<std::string, std::string>> userFilters);

} // namespace maketex::build
