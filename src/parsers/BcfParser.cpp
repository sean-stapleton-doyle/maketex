#include "maketex/parsers/BcfParser.h"

#include "maketex/parsers/TextFileParser.h"

#include <string_view>

namespace maketex {

std::expected<BcfRecord, std::error_code> BcfParser::parse(
    const std::filesystem::path& path) const
{
    BcfRecord rec;
    // biblatex writes each datasource on its own line:
    //   <bcf:datasource type="file" datatype="bibtex" glob="false">refs.bib</bcf:datasource>
    // Remote sources (type="url") are not files and are skipped.
    auto r = detail::forEachLine(path, [&](std::string_view line) {
        constexpr std::string_view kOpen = "<bcf:datasource";
        constexpr std::string_view kClose = "</bcf:datasource>";
        auto open = line.find(kOpen);
        if (open == std::string_view::npos)
            return;
        auto tagEnd = line.find('>', open);
        auto close = line.find(kClose, open);
        if (tagEnd == std::string_view::npos || close == std::string_view::npos
            || close < tagEnd)
            return;
        auto tag = line.substr(open, tagEnd - open);
        if (!tag.contains("type=\"file\""))
            return;
        auto name = line.substr(tagEnd + 1, close - tagEnd - 1);
        if (!name.empty())
            rec.dataSources.emplace_back(name);
    });
    if (!r)
        return std::unexpected(r.error());
    return rec;
}

} // namespace maketex
