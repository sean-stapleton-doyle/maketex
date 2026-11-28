#include "maketex/build/BuildHeuristics.h"

#include <fstream>
#include <map>
#include <system_error>

namespace fs = std::filesystem;

namespace maketex::build {

namespace {

    constexpr std::size_t kMultiPassScanLimit = 256 * 1024;

    bool hasMultiPassSignals(std::string_view text)
    {
        if (text.contains("\\tableofcontents") || text.contains("\\listoffigures")
            || text.contains("\\listoftables") || text.contains("\\bibliography{")
            || text.contains("{biblatex}") || text.contains("\\makeindex")
            || text.contains("\\makeglossaries") || text.contains("\\makenomenclature")
            || text.contains("{pythontex}"))
            return true;
        bool hasLabel = text.contains("\\label{");
        bool hasRef = text.contains("\\ref{") || text.contains("\\pageref{")
            || text.contains("\\cref{") || text.contains("\\autoref{");
        return hasLabel && hasRef;
    }

} // namespace

bool likelyMultiPassText(std::string_view raw)
{
    std::string text;
    std::size_t start = 0;
    while (start < raw.size() && text.size() < kMultiPassScanLimit) {
        auto nl = raw.find('\n', start);
        std::string_view line = nl == std::string_view::npos
            ? raw.substr(start)
            : raw.substr(start, nl - start);
        start = nl == std::string_view::npos ? raw.size() : nl + 1;
        // Strip simple comments so commented \tableofcontents doesn't count.
        auto pct = line.find('%');
        text += line.substr(0, pct);
        text += '\n';
    }
    return hasMultiPassSignals(text);
}

bool likelyMultiPassColdBuild(const fs::path& texFile, const fs::path& auxFile)
{
    std::error_code ec;
    if (fs::exists(auxFile, ec))
        return false; // warm build: often finishes in 1 pass
    std::ifstream in(texFile);
    if (!in)
        return false;
    std::string text;
    std::string line;
    while (std::getline(in, line) && text.size() < kMultiPassScanLimit) {
        // Strip simple comments so commented \tableofcontents doesn't count.
        auto pct = line.find('%');
        text += line.substr(0, pct);
        text += '\n';
    }
    return hasMultiPassSignals(text);
}

std::optional<EngineKind> unicodeEngineNeeded(std::span<const Diagnostic> diagnostics)
{
    for (const auto& d : diagnostics) {
        const auto& m = d.message;
        // Matched on prefixes/phrases; check the "either / LuaTeX" cases
        // before XeTeX-only ones so "requires XeTeX or LuaTeX" selects
        // LuaLaTeX rather than matching "requires XeTeX".
        if (m.contains("requires either XeTeX or") || m.contains("XeLaTeX or LuaLaTeX")
            || m.contains("XeTeX or LuaTeX") || m.contains("Cannot be run with pdfLaTeX")
            || m.contains("requires LuaTeX") || m.contains("LuaTeX is required")
            || m.contains("run LuaTeX") || m.contains("need LuaTeX")
            || m.contains("requires LuaLaTeX") || m.contains("LuaLaTeX is required"))
            return EngineKind::LuaLaTeX;
        if (m.contains("requires XeTeX") || m.contains("XeTeX is required")
            || m.contains("requires XeLaTeX") || m.contains("XeLaTeX is required"))
            return EngineKind::XeLaTeX;
    }
    return std::nullopt;
}

DriverModes chooseDriverModes(EngineKind engineKind, int maxPasses,
    bool xdvipdfmxAvailable, bool likelyMultiPassCold)
{
    const bool useXdvSplit = engineKind == EngineKind::XeLaTeX && xdvipdfmxAvailable;
    const bool draftFirst = !useXdvSplit
        && (engineKind == EngineKind::PdfLaTeX || engineKind == EngineKind::LuaLaTeX)
        && maxPasses >= 2
        && likelyMultiPassCold;
    return DriverModes {
        .draftFirstPass = draftFirst,
        .xelatexXdvSplit = useXdvSplit,
    };
}

std::vector<std::pair<std::string, std::regex>> mergeContentFilters(
    std::span<const std::pair<std::string, std::string>> userFilters)
{
    // Merge user filters with built-in defaults for the same extension
    // instead of overwriting them (e.g. keep \@writefile filtering on
    // .aux when the user adds another .aux ignore pattern).
    std::map<std::string, std::string> combined {
        { "eps", R"(^%%CreationDate: )" },
        { "aux", R"(^\\@writefile\{)" },
    };
    for (const auto& [ext, pattern] : userFilters) {
        auto it = combined.find(ext);
        if (it == combined.end())
            combined.emplace(ext, pattern);
        else
            it->second = "(" + it->second + ")|(" + pattern + ")";
    }
    std::vector<std::pair<std::string, std::regex>> filters;
    filters.reserve(combined.size());
    for (const auto& [ext, pattern] : combined)
        filters.emplace_back(ext, std::regex { pattern });
    return filters;
}

} // namespace maketex::build
