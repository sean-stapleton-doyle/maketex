#include "maketex/diagnostics/Categorize.h"

#include "maketex/diagnostics/LogLines.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>

namespace maketex::diagnostics {

namespace {

    bool containsAny(std::string_view msg, std::initializer_list<std::string_view> needles)
    {
        return std::ranges::any_of(needles, [&](std::string_view n) {
            return msg.find(n) != std::string_view::npos;
        });
    }

} // namespace

std::string categorizeError(std::string_view msg)
{
    if (msg.starts_with("Undefined control sequence"))
        return "undefined-macro";
    if (containsAny(msg, { "font", "Font" })
        && containsAny(msg, { "cannot be found", "not loadable", "cannot be loaded" }))
        return "missing-font";
    if ((msg.starts_with("File `") && containsAny(msg, { "not found" }))
        || msg.starts_with("I can't find file"))
        return "missing-file";
    if (containsAny(msg, { "-shell-escape", "shell escape", "\\write18" }))
        return "shell-escape-required";
    if (msg.starts_with("Package "))
        return "package";
    return { };
}

std::string categorizeWarning(std::string_view msg)
{
    if (msg.find("Citation") != std::string_view::npos
        && msg.find("undefined") != std::string_view::npos)
        return "undefined-citation";
    if (msg.find("Reference") != std::string_view::npos
        && msg.find("undefined") != std::string_view::npos)
        return "undefined-reference";
    if (msg.find("multiply defined") != std::string_view::npos)
        return "duplicate-label";
    return { };
}

std::string cleanErrorMessage(std::string message)
{
    if (message.starts_with("LaTeX Error: "))
        message = message.substr(13);
    if (!message.empty() && message.back() == '.')
        message.pop_back();
    return message;
}

int extractInputLine(std::string_view msg)
{
    auto pos = msg.rfind("on input line ");
    if (pos == std::string_view::npos)
        return -1;
    auto start = pos + 14;
    auto end = start;
    while (end < msg.size() && std::isdigit(static_cast<unsigned char>(msg[end])))
        ++end;
    if (end == start)
        return -1;
    return tryParseInt(msg.substr(start, end - start)).value_or(-1);
}

void stripInputLineSuffix(std::string& msg)
{
    auto lineTagPos = msg.rfind(" on input line ");
    if (lineTagPos != std::string::npos)
        msg.erase(lineTagPos);
    else if (!msg.empty() && msg.back() == '.')
        msg.pop_back();
}

} // namespace maketex::diagnostics
