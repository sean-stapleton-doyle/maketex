#include "parsers/LogParser.h"

#include "parsers/TextFileParser.h"

#include <string_view>

namespace maketex {

std::expected<LogRecord, std::error_code> LogParser::parse(
    const std::filesystem::path& path) const
{
    LogRecord rec;

    auto r = detail::forEachLine(path, [&](std::string_view line) {
        // Rerun triggers — latex itself requesting another pass.
        // Covers: cross-references, hyperref outlines, rerunfilecheck, natbib, etc.
        if (line.contains("Rerun to get") || line.contains("Please rerun LaTeX")
            || line.contains("Please (re)run LaTeX"))
            rec.rerunNeeded = true;

        // Secondary bibtex signal (primary trigger is AuxParser \bibdata detection).
        if (line.contains("run BibTeX") || line.contains("run bibtex"))
            rec.bibtexRerunNeeded = true;

        if (line.contains("There were undefined references"))
            rec.undefinedReferences = true;

        // "LaTeX Warning: Citation `foo' on page 1 undefined"
        if (line.contains("Citation") && line.contains("undefined"))
            rec.undefinedCitations = true;
    });
    if (!r)
        return std::unexpected(r.error());

    return rec;
}

} // namespace maketex
