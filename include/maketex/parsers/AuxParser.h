#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
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

    // Templated on the callback (cf. detail::forEachLine) so the call site is
    // monomorphised — avoids a per-call std::function indirection.
    template <typename Fn>
    static void splitComma(std::string_view s, Fn&& fn);
};

template <typename Fn>
void AuxParser::splitComma(std::string_view s, Fn&& fn)
{
    size_t start = 0;
    while (true) {
        auto end = s.find(',', start);
        if (end == std::string_view::npos) {
            std::forward<Fn>(fn)(s.substr(start));
            break;
        }
        std::forward<Fn>(fn)(s.substr(start, end - start));
        start = end + 1;
    }
}

} // namespace maketex
