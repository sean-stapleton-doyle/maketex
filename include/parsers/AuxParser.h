#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace maketex {

// Citation changes don't need parsing: rerun decisions hash the whole .aux
// file (BibtexTool::trackedInputs), which subsumes per-key tracking.
struct AuxRecord {
    // Bare database names from "\\bibdata{}" lines — no .bib extension.
    // The caller is responsible for locating the actual files via TEXINPUTS.
    std::vector<std::string> bibDataFiles;
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
