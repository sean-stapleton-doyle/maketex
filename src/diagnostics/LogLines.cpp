#include "maketex/diagnostics/LogLines.h"

#include <charconv>

namespace maketex::diagnostics {

std::vector<std::string> splitLines(std::string_view output)
{
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start < output.size()) {
        auto nl = output.find('\n', start);
        if (nl == std::string_view::npos) {
            lines.emplace_back(output.substr(start));
            break;
        }
        lines.emplace_back(output.substr(start, nl - start));
        start = nl + 1;
    }
    return lines;
}

std::optional<int> tryParseInt(std::string_view sv)
{
    int value = 0;
    const auto* first = sv.data();
    const auto* last = sv.data() + sv.size();
    auto [ptr, ec] = std::from_chars(first, last, value);
    if (ec != std::errc { } || ptr != last)
        return std::nullopt;
    return value;
}

std::string quotedName(std::string_view message)
{
    auto open = message.find('`');
    auto close = message.find('\'', open == std::string_view::npos ? 0 : open);
    if (open == std::string_view::npos || close == std::string_view::npos)
        return { };
    return std::string(message.substr(open + 1, close - open - 1));
}

} // namespace maketex::diagnostics
