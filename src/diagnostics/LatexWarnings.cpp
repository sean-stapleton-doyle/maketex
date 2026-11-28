#include "maketex/diagnostics/LatexWarnings.h"

#include "maketex/diagnostics/Categorize.h"
#include "maketex/diagnostics/FileStack.h"
#include "maketex/diagnostics/LogLines.h"

#include <regex>
#include <string_view>

namespace fs = std::filesystem;

namespace maketex::diagnostics {

WarningMatch matchLatexWarning(LineCursor& cursor)
{
    if (cursor.index >= cursor.lines.size())
        return { };
    const auto& first = cursor.lines[cursor.index];
    if (!first.starts_with("LaTeX Warning: "))
        return { };

    std::size_t i = cursor.index;
    std::string msg = first.substr(15);
    std::string rawCtx = first;

    // LaTeX aligns continuations under the message text.
    while (i + 1 < cursor.lines.size() && cursor.lines[i + 1].starts_with("  ")
        && cursor.lines[i + 1].find_first_not_of(' ') != std::string::npos) {
        ++i;
        rawCtx += "\n" + cursor.lines[i];
        auto rest = std::string_view(cursor.lines[i]);
        while (!rest.empty() && rest.front() == ' ')
            rest.remove_prefix(1);
        msg += ' ';
        msg += rest;
    }
    cursor.index = i + 1;

    // Skip summary lines — individual warnings already cover them,
    // and rerun signals are handled by LogParser, not surfaced here.
    if (msg.starts_with("There were ") || msg.starts_with("Label(s) may have changed"))
        return WarningMatch { .matched = true, .diagnostic = std::nullopt };

    int lineNum = extractInputLine(msg);
    stripInputLineSuffix(msg);
    auto category = categorizeWarning(msg);
    return WarningMatch {
        .matched = true,
        .diagnostic = Diagnostic {
            .severity = Diagnostic::Severity::Warning,
            .line = lineNum,
            .message = std::move(msg),
            .category = std::move(category),
            .rawContext = std::move(rawCtx),
        },
    };
}

WarningMatch matchNamedWarning(LineCursor& cursor)
{
    if (cursor.index >= cursor.lines.size())
        return { };
    // "Package natbib Warning: …", "Class memoir Warning: …",
    // "LaTeX Font Warning: …" — continued on "(natbib)"/"(Font)" lines.
    static const std::regex kNamedWarning {
        R"(^(?:(Package|Class) ([\w.-]+)|LaTeX (Font)) Warning: (.*)$)"
    };

    const auto& first = cursor.lines[cursor.index];
    std::smatch named;
    if (!std::regex_match(first, named, kNamedWarning))
        return { };

    std::size_t i = cursor.index;
    bool isFont = named[3].matched;
    std::string name = isFont ? "Font" : named[2].str();
    std::string msg = named[4].str();
    std::string rawCtx = first;

    std::string contPrefix = "(" + name + ")";
    while (i + 1 < cursor.lines.size() && cursor.lines[i + 1].starts_with(contPrefix)) {
        ++i;
        rawCtx += "\n" + cursor.lines[i];
        auto rest = std::string_view(cursor.lines[i]).substr(contPrefix.size());
        while (!rest.empty() && rest.front() == ' ')
            rest.remove_prefix(1);
        if (!rest.empty()) {
            msg += ' ';
            msg += rest;
        }
    }
    cursor.index = i + 1;

    int lineNum = extractInputLine(msg);
    stripInputLineSuffix(msg);
    std::string category = categorizeWarning(msg);
    if (category.empty())
        category = isFont ? "font" : (named[1] == "Class" ? "class" : "package");
    std::string label = isFont ? "Font" : named[1].str() + " " + name;
    return WarningMatch {
        .matched = true,
        .diagnostic = Diagnostic {
            .severity = Diagnostic::Severity::Warning,
            .line = lineNum,
            .message = label + ": " + msg,
            .category = std::move(category),
            .rawContext = std::move(rawCtx),
        },
    };
}

WarningMatch matchMissingCharacter(LineCursor& cursor)
{
    if (cursor.index >= cursor.lines.size())
        return { };
    const auto& line = cursor.lines[cursor.index];
    if (!line.starts_with("Missing character: "))
        return { };
    ++cursor.index;
    return WarningMatch {
        .matched = true,
        .diagnostic = Diagnostic {
            .severity = Diagnostic::Severity::Warning,
            .message = line,
            .category = "missing-glyph",
            .rawContext = line,
        },
    };
}

WarningMatch matchPdfTexWarning(LineCursor& cursor)
{
    if (cursor.index >= cursor.lines.size())
        return { };
    const auto& line = cursor.lines[cursor.index];
    if (!line.starts_with("pdfTeX warning"))
        return { };
    ++cursor.index;
    return WarningMatch {
        .matched = true,
        .diagnostic = Diagnostic {
            .severity = Diagnostic::Severity::Warning,
            .message = line,
            .category = "pdftex",
            .rawContext = line,
        },
    };
}

WarningMatch matchBoxWarning(LineCursor& cursor)
{
    if (cursor.index >= cursor.lines.size())
        return { };
    // "Overfull \hbox (12.0pt too wide) in paragraph at lines 10--12",
    // "… detected at line 7", "… in alignment at lines 3--9".
    static const std::regex kBox { R"(^(Overfull|Underfull) \\([hv]box) )" };
    static const std::regex kBoxLine { R"((?:at lines?|detected at line) (\d+))" };

    const auto& line = cursor.lines[cursor.index];
    std::smatch box;
    if (!std::regex_search(line, box, kBox))
        return { };

    int lineNum = -1;
    if (std::smatch at; std::regex_search(line, at, kBoxLine))
        lineNum = tryParseInt(at[1].str()).value_or(-1);
    bool overfull = box[1] == "Overfull";
    std::string category = (overfull ? "overfull-" : "underfull-") + box[2].str();

    // The box's typeset content follows up to a blank line; its parentheses
    // are the document's text, not file opens.
    std::size_t i = cursor.index;
    while (i + 1 < cursor.lines.size() && !cursor.lines[i + 1].empty())
        ++i;
    cursor.index = i + 1;

    // Underfull boxes are badness reports, rarely actionable.
    return WarningMatch {
        .matched = true,
        .diagnostic = Diagnostic {
            .severity = overfull ? Diagnostic::Severity::Warning : Diagnostic::Severity::Info,
            .line = lineNum,
            .message = line,
            .category = std::move(category),
            .rawContext = line,
        },
    };
}

std::vector<Diagnostic> extractLatexWarnings(const std::string& output,
    const fs::path& texFile, const ISourceResolver& resolver)
{
    static constexpr WarningMatcher kMatchers[] = {
        matchLatexWarning,
        matchNamedWarning,
        matchMissingCharacter,
        matchPdfTexWarning,
        matchBoxWarning,
    };

    std::vector<Diagnostic> diags;
    const fs::path workingDir = texFile.parent_path();
    FileStack stack(workingDir, texFile, resolver);
    const auto lines = splitLines(output);

    auto attribute = [&](Diagnostic& d) {
        const auto& ref = stack.current();
        // Warnings raised while LaTeX re-reads its .aux at the end of the run
        // ("Label multiply defined") would point at a generated file; the
        // document is the useful place to look.
        const auto& target = fs::path(ref.display).extension() == ".aux"
            ? stack.root()
            : ref;
        d.file = target.display;
        d.sourcePath = target.abs;
    };

    LineCursor cursor { lines, 0 };
    while (cursor.index < lines.size()) {
        bool consumed = false;
        for (WarningMatcher matcher : kMatchers) {
            if (auto m = matcher(cursor); m.matched) {
                if (m.diagnostic) {
                    attribute(*m.diagnostic);
                    diags.push_back(std::move(*m.diagnostic));
                }
                consumed = true;
                break;
            }
        }
        if (consumed)
            continue;

        if (!isSourceEcho(lines, cursor.index))
            stack.scan(lines[cursor.index]);
        ++cursor.index;
    }

    return diags;
}

} // namespace maketex::diagnostics
