#include "maketex/diagnostics/ToolOutput.h"

#include "maketex/diagnostics/LogLines.h"

#include <algorithm>
#include <array>
#include <regex>

namespace maketex::diagnostics {

namespace {

    constexpr auto kError = Diagnostic::Severity::Error;
    constexpr auto kWarning = Diagnostic::Severity::Warning;

    constexpr std::array<LinePattern, 2> kBiberPatterns { {
        { "ERROR - ", false, kError, true },
        { "WARN - ", false, kWarning, true },
    } };

    // makeindex writes errors prefixed with "!! " and warnings prefixed with "## ".
    constexpr std::array<LinePattern, 2> kMakeindexPatterns { {
        { "!! ", true, kError, true },
        { "## ", true, kWarning, true },
    } };

    // makeglossaries delegates to makeindex/xindy plus its own "Error:" reports.
    constexpr std::array<LinePattern, 5> kMakeglossariesPatterns { {
        { "!! ", true, kError, true },
        { "## ", true, kWarning, true },
        { "*** ", true, kError, true },
        { "Error: ", false, kError, false },
        { "Warning: ", false, kWarning, false },
    } };

    constexpr std::array<LinePattern, 4> kBib2glsPatterns { {
        { "Error: ", false, kError, false },
        { "ERROR: ", false, kError, false },
        { "Warning: ", false, kWarning, false },
        { "WARNING: ", false, kWarning, false },
    } };

    constexpr std::array<LinePattern, 2> kPythontexPatterns { {
        { "PythonTeX error", false, kError, false },
        { "PythonTeX warning", false, kWarning, false },
    } };

    constexpr std::array<LinePattern, 4> kXdvipdfmxPatterns { {
        { "xdvipdfmx:fatal:", false, kError, true },
        { "xdvipdfmx:warning:", false, kWarning, true },
        { "** ERROR **", false, kError, true },
        { "** WARNING **", false, kWarning, true },
    } };

    constexpr std::array<LinePattern, 8> kCustomToolPatterns { {
        { "ERROR: ", false, kError, true },
        { "Error: ", false, kError, true },
        { "error: ", false, kError, true },
        { "!! ", true, kError, true },
        { "WARNING: ", false, kWarning, true },
        { "Warning: ", false, kWarning, true },
        { "warning: ", false, kWarning, true },
        { "## ", true, kWarning, true },
    } };

    struct ToolPatternEntry {
        InvokerKind kind;
        std::span<const LinePattern> patterns;
    };

    constexpr std::array<ToolPatternEntry, 7> kToolPatternTable { {
        { InvokerKind::Biber, kBiberPatterns },
        { InvokerKind::Makeindex, kMakeindexPatterns },
        { InvokerKind::Makeglossaries, kMakeglossariesPatterns },
        { InvokerKind::Bib2gls, kBib2glsPatterns },
        { InvokerKind::Pythontex, kPythontexPatterns },
        { InvokerKind::Xdvipdfmx, kXdvipdfmxPatterns },
        { InvokerKind::Custom, kCustomToolPatterns },
    } };

    bool isBibtexChatter(std::string_view line)
    {
        return line.starts_with("This is BibTeX")
            || line.starts_with("The top-level auxiliary file")
            || line.starts_with("The style file")
            || line.starts_with("Database file #")
            || line.starts_with("(There w")
            || line.starts_with("I'm skipping whatever remains")
            || line.starts_with(" : ")
            || line.empty();
    }

    Diagnostic parseBibtexError(std::span<const std::string> lines, std::size_t& i)
    {
        static const std::regex kWhere { R"(---line (\d+) of file (.+)$)" };

        const auto& line = lines[i];
        Diagnostic d {
            .severity = Diagnostic::Severity::Error,
            .rawContext = line,
        };
        std::string message = line;
        std::smatch where;
        const std::string* whereLine = nullptr;
        if (std::regex_search(line, where, kWhere)) {
            message = line.substr(0, static_cast<std::size_t>(where.position(0)));
            whereLine = &line;
        } else if (i + 1 < lines.size() && lines[i + 1].starts_with("---line ")) {
            ++i;
            d.rawContext += "\n" + lines[i];
            whereLine = &lines[i];
        }
        if (whereLine && std::regex_search(*whereLine, where, kWhere)) {
            d.file = where[2].str();
            d.line = tryParseInt(where[1].str()).value_or(-1);
        }
        while (i + 1 < lines.size() && lines[i + 1].starts_with(" : "))
            d.rawContext += "\n" + lines[++i];

        constexpr std::string_view kCouldntOpen = "I couldn't open ";
        if (message.starts_with(kCouldntOpen)) {
            d.category = "missing-file";
            // "… database file refs.bib" / "… file name `main.aux'"
            auto quoted = quotedName(message);
            d.missingFile = !quoted.empty()
                ? quoted
                : message.substr(message.rfind(' ') + 1);
            // The .aux location is where bibtex read the name; the useful
            // location is the missing file itself.
            d.file = d.missingFile;
            d.line = -1;
        }
        d.message = std::move(message);
        return d;
    }

} // namespace

std::vector<Diagnostic> extractByLinePatterns(
    std::string_view output, std::span<const LinePattern> patterns)
{
    std::vector<Diagnostic> diags;
    for (const auto& line : splitLines(output)) {
        for (const auto& p : patterns) {
            auto pos = p.prefixMatch
                ? (line.starts_with(p.token) ? std::size_t { 0 } : std::string::npos)
                : line.find(p.token);
            if (pos == std::string::npos)
                continue;
            diags.push_back(Diagnostic {
                .severity = p.severity,
                .message = p.messageAfterToken ? line.substr(pos + p.token.size()) : line,
                .rawContext = line,
            });
            break; // first matching pattern wins
        }
    }
    return diags;
}

std::vector<Diagnostic> extractBibtex(std::string_view output)
{
    const auto lines = splitLines(output);
    std::vector<Diagnostic> diags;

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];
        if (line.starts_with("Warning--")) {
            std::string msg = line.substr(9);
            std::string category;
            if (msg.find("didn't find a database entry") != std::string::npos)
                category = "undefined-citation";
            diags.push_back(Diagnostic {
                .severity = Diagnostic::Severity::Warning,
                .message = std::move(msg),
                .category = std::move(category),
                .rawContext = line,
            });
            continue;
        }
        if (isBibtexChatter(line) || line.starts_with("---"))
            continue;

        diags.push_back(parseBibtexError(lines, i));
    }

    // "I found no database files" only restates a missing database.
    bool missingDatabase = std::ranges::any_of(diags,
        [](const Diagnostic& d) { return d.category == "missing-file"; });
    if (missingDatabase)
        std::erase_if(diags, [](const Diagnostic& d) {
            return d.message.starts_with("I found no database files");
        });
    return diags;
}

std::vector<Diagnostic> extractToolOutput(InvokerKind kind, const std::string& output)
{
    if (kind == InvokerKind::Bibtex)
        return extractBibtex(output);

    for (const auto& entry : kToolPatternTable) {
        if (entry.kind != kind)
            continue;
        auto diags = extractByLinePatterns(output, entry.patterns);
        // "I didn't find a database entry for 'key'" — keep key, tag category.
        if (kind == InvokerKind::Biber)
            for (auto& d : diags)
                if (d.message.find("didn't find a database entry") != std::string::npos)
                    d.category = "undefined-citation";
        return diags;
    }
    return { };
}

} // namespace maketex::diagnostics
