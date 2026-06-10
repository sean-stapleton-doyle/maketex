#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace maketex {

struct AuxRecord {
    // Deduplicated citation keys extracted from "\\citation{}" lines.
    // Non-empty means bibtex/biber may need to run (cross-check with bibDataFiles).
    std::vector<std::string> citations;

    // Bare database names from "\\bibdata{}" lines — no .bib extension.
    // The caller is responsible for locating the actual files via TEXINPUTS.
    std::vector<std::string> bibDataFiles;

    // Paths of included sub-aux files from "\\@input{}" lines, weakly-canonical.
    // Produced by the LaTeX \include{} command — each must be parsed separately.
    std::vector<std::filesystem::path> inputs;
};

class AuxParser {
public:
    std::expected<AuxRecord, std::error_code> parse(
        const std::filesystem::path& path) const;

private:
    static std::string_view extractBraces(std::string_view line, std::string_view prefix);
    static void splitComma(std::string_view s,
        const std::function<void(std::string_view)>& fn);
};

} // namespace maketex
