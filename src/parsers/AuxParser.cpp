#include "maketex/parsers/AuxParser.h"

#include "maketex/parsers/TextFileParser.h"

namespace maketex {

namespace fs = std::filesystem;

std::expected<AuxRecord, std::error_code> AuxParser::parse(const fs::path& path) const
{
    AuxRecord rec;

    auto r = detail::forEachLine(path, [&](std::string_view line) {
        if (auto b = extractBraces(line, "\\bibdata{"); !b.empty()) {
            splitComma(b, [&](std::string_view name) {
                rec.bibDataFiles.emplace_back(name);
            });
        }
    });
    if (!r)
        return std::unexpected(r.error());

    return rec;
}

std::string_view AuxParser::extractBraces(std::string_view line, std::string_view prefix)
{
    if (!line.starts_with(prefix))
        return { };
    auto end = line.rfind('}');
    if (end == std::string_view::npos || end < prefix.size())
        return { };
    return line.substr(prefix.size(), end - prefix.size());
}

void AuxParser::splitComma(std::string_view s,
    const std::function<void(std::string_view)>& fn)
{
    size_t start = 0;
    while (true) {
        auto end = s.find(',', start);
        if (end == std::string_view::npos) {
            fn(s.substr(start));
            break;
        }
        fn(s.substr(start, end - start));
        start = end + 1;
    }
}

} // namespace maketex
