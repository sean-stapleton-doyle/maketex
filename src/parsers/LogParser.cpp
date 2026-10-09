#include "maketex/parsers/LogParser.h"

#include "maketex/parsers/TextFileParser.h"

#include <algorithm>
#include <cctype>
#include <string_view>

namespace maketex {

namespace {

    bool containsIgnoreCase(std::string_view haystack, std::string_view needle)
    {
        return std::ranges::search(haystack, needle, [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a))
                == std::tolower(static_cast<unsigned char>(b));
        }).begin()
            != haystack.end();
    }

    // Rerun requests addressed to LaTeX. "Please (re)run Biber" and similar
    // tool requests are deliberately excluded: tools are scheduled from
    // their own inputs, not from log text.
    bool requestsRerun(std::string_view line)
    {
        return containsIgnoreCase(line, "Rerun to get")
            || containsIgnoreCase(line, "Rerun LaTeX")
            || containsIgnoreCase(line, "Please rerun LaTeX")
            || containsIgnoreCase(line, "Please (re)run LaTeX");
    }

} // namespace

std::expected<LogRecord, std::error_code> LogParser::parse(
    const std::filesystem::path& path) const
{
    LogRecord rec;
    auto r = detail::forEachLine(path, [&](std::string_view line) {
        if (requestsRerun(line))
            rec.rerunNeeded = true;
        // "No file main.toc." — LaTeX's \@input on a not-yet-written file.
        constexpr std::string_view kNoFile = "No file ";
        if (line.starts_with(kNoFile) && line.ends_with('.')) {
            auto name = line.substr(kNoFile.size(), line.size() - kNoFile.size() - 1);
            if (!name.empty())
                rec.missingFiles.emplace_back(name);
        }
    });
    if (!r)
        return std::unexpected(r.error());
    return rec;
}

} // namespace maketex
