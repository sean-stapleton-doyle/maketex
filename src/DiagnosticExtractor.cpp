#include "DiagnosticExtractor.h"

#include "InvokerKind.h"

#include <charconv>
#include <deque>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace maketex {

namespace {

    // Parses an integer from sv. Returns nullopt if sv is empty or not a
    // valid base-10 integer. Used to recover from malformed line numbers in
    // log output without losing the surrounding diagnostic.
    std::optional<int> tryParseInt(std::string_view sv)
    {
        int value = 0;
        auto first = sv.data();
        auto last = sv.data() + sv.size();
        auto [ptr, ec] = std::from_chars(first, last, value);
        if (ec != std::errc {} || ptr != last)
            return std::nullopt;
        return value;
    }

    // ── TeX engine (pdflatex / xelatex / lualatex) ───────────────────────────

    // TeX's paren-based file tracking is fragile to parse: paths wrap at 79
    // chars, version strings contain balanced parens, and package names look
    // like filenames. For single-file documents (the common case) the error
    // always belongs to the main tex file, so we use the invocation path
    // directly rather than attempting a heuristic stack parse.
    std::vector<Diagnostic> extractLatex(
        const std::string& output, const std::filesystem::path& texFile)
    {
        std::vector<Diagnostic> diags;
        std::istringstream stream(output);
        std::string line;

        // Rolling context window shown in rawContext.
        std::deque<std::string> window;
        constexpr int kWindowSize = 8;
        std::string file = texFile.filename().string();

        while (std::getline(stream, line)) {
            window.push_back(line);
            if ((int)window.size() > kWindowSize)
                window.pop_front();

            // Skip non-error lines and the "!  ==>" fatal-error summary line
            // (two spaces after '!' marks a continuation, not a new error).
            if (!line.starts_with("! ") || (line.size() > 2 && line[2] == ' '))
                continue;

            std::string message = line.substr(2);
            int lineNum = -1;

            // Assemble rawContext from the window plus a few lines ahead.
            std::string rawCtx;
            for (const auto& w : window)
                rawCtx += w + "\n";

            // Scan ahead for the `l.N` line that carries the source line number.
            std::string ahead;
            for (int i = 0; i < 8 && std::getline(stream, ahead); ++i) {
                rawCtx += ahead + "\n";
                if (ahead.starts_with("l.")) {
                    auto space = ahead.find(' ');
                    auto numStr = ahead.substr(2, space == std::string::npos
                            ? std::string::npos
                            : space - 2);
                    if (auto n = tryParseInt(numStr); n.has_value())
                        lineNum = n.value();
                    break;
                }
            }

            // Strip a leading "LaTeX Error: " prefix for cleaner messages.
            if (message.starts_with("LaTeX Error: "))
                message = message.substr(13);

            // Strip trailing period if present.
            if (!message.empty() && message.back() == '.')
                message.pop_back();

            diags.push_back(Diagnostic {
                .severity = Diagnostic::Severity::Error,
                .file = std::move(file),
                .line = lineNum,
                .message = std::move(message),
                .rawContext = std::move(rawCtx),
            });
        }

        return diags;
    }

    // ── TeX warnings (successful builds) ─────────────────────────────────────

    // Extracts line number from "... on input line N." suffix.
    int extractInputLine(const std::string& msg)
    {
        auto pos = msg.rfind("on input line ");
        if (pos == std::string::npos)
            return -1;
        auto start = pos + 14;
        auto end = start;
        while (end < msg.size() && std::isdigit((unsigned char)msg[end]))
            ++end;
        if (end == start)
            return -1;
        return tryParseInt(std::string_view(msg).substr(start, end - start)).value_or(-1);
    }

    std::vector<Diagnostic> extractLatexWarnings(
        const std::string& output, const std::filesystem::path& texFile)
    {
        std::vector<Diagnostic> diags;
        std::istringstream stream(output);
        std::string line;
        std::string file = texFile.filename().string();

        while (std::getline(stream, line)) {
            // LaTeX Warning: <message>
            if (line.starts_with("LaTeX Warning: ")) {
                std::string msg = line.substr(15);

                // Skip summary lines — individual warnings already cover them,
                // and rerun signals are handled by LogParser, not surfaced here.
                if (msg.starts_with("There were ")
                    || msg.starts_with("Label(s) may have changed"))
                    continue;

                int lineNum = extractInputLine(msg);

                // Strip "on input line N" suffix — the location already carries it.
                auto lineTagPos = msg.rfind(" on input line ");
                if (lineTagPos != std::string::npos)
                    msg.erase(lineTagPos);
                else if (!msg.empty() && msg.back() == '.')
                    msg.pop_back();

                diags.push_back(Diagnostic {
                    .severity = Diagnostic::Severity::Warning,
                    .file = file,
                    .line = lineNum,
                    .message = std::move(msg),
                    .rawContext = line,
                });
                continue;
            }

            // Overfull \hbox (Xpt too wide) in paragraph at lines N--M
            if (line.starts_with("Overfull \\hbox")) {
                // Extract the starting line from "at lines N--M" or "at line N".
                int lineNum = -1;
                auto pos = line.rfind("at lines ");
                if (pos != std::string::npos) {
                    auto start = pos + 9;
                    auto end = start;
                    while (end < line.size() && std::isdigit((unsigned char)line[end]))
                        ++end;
                    try { lineNum = std::stoi(line.substr(start, end - start)); }
                    catch (...) {}
                }

                diags.push_back(Diagnostic {
                    .severity = Diagnostic::Severity::Warning,
                    .file = file,
                    .line = lineNum,
                    .message = line,
                    .rawContext = line,
                });
            }
        }

        return diags;
    }

    // ── bibtex ────────────────────────────────────────────────────────────────

    std::vector<Diagnostic> extractBibtex(const std::string& output)
    {
        std::vector<Diagnostic> diags;
        std::istringstream stream(output);
        std::string line;

        while (std::getline(stream, line)) {
            // "I couldn't open database file foo.bib"
            if (line.find("I couldn't open") != std::string::npos) {
                // Extract the filename from "... file <name>"
                std::string msg = line;
                auto filePos = line.rfind("file ");
                std::string file;
                if (filePos != std::string::npos)
                    file = line.substr(filePos + 5);

                diags.push_back(Diagnostic {
                    .severity = Diagnostic::Severity::Error,
                    .file = std::move(file),
                    .line = -1,
                    .message = std::move(msg),
                    .rawContext = output,
                });
                return diags; // one fatal error is enough
            }

            // "Warning--..." lines
            if (line.starts_with("Warning--")) {
                diags.push_back(Diagnostic {
                    .severity = Diagnostic::Severity::Warning,
                    .message = line.substr(9),
                    .rawContext = line,
                });
            }
        }

        return diags;
    }

    // ── biber ─────────────────────────────────────────────────────────────────

    std::vector<Diagnostic> extractBiber(const std::string& output)
    {
        std::vector<Diagnostic> diags;
        std::istringstream stream(output);
        std::string line;

        while (std::getline(stream, line)) {
            Diagnostic::Severity sev;
            std::string_view prefix;

            if (line.find("ERROR - ") != std::string::npos) {
                sev = Diagnostic::Severity::Error;
                prefix = "ERROR - ";
            } else if (line.find("WARN - ") != std::string::npos) {
                sev = Diagnostic::Severity::Warning;
                prefix = "WARN - ";
            } else {
                continue;
            }

            auto pos = line.find(prefix);
            std::string message = (pos != std::string::npos)
                ? line.substr(pos + prefix.size())
                : line;

            diags.push_back(Diagnostic {
                .severity = sev,
                .message = std::move(message),
                .rawContext = line,
            });
        }

        return diags;
    }

    // ── makeindex ─────────────────────────────────────────────────────────────
    // makeindex writes errors prefixed with "!! " and warnings prefixed with "## ".

    std::vector<Diagnostic> extractMakeindex(const std::string& output)
    {
        std::vector<Diagnostic> diags;
        std::istringstream stream(output);
        std::string line;

        while (std::getline(stream, line)) {
            if (line.starts_with("!! ")) {
                diags.push_back(Diagnostic {
                    .severity = Diagnostic::Severity::Error,
                    .message = line.substr(3),
                    .rawContext = line,
                });
            } else if (line.starts_with("## ")) {
                diags.push_back(Diagnostic {
                    .severity = Diagnostic::Severity::Warning,
                    .message = line.substr(3),
                    .rawContext = line,
                });
            }
        }

        return diags;
    }

    // ── makeglossaries ────────────────────────────────────────────────────────
    // makeglossaries is a Perl script that delegates to makeindex/xindy.
    // Errors are reported as lines containing "Error" (fatal) or "Warning".

    std::vector<Diagnostic> extractMakeglossaries(const std::string& output)
    {
        std::vector<Diagnostic> diags;
        std::istringstream stream(output);
        std::string line;

        while (std::getline(stream, line)) {
            if (line.find("Error") != std::string::npos) {
                diags.push_back(Diagnostic {
                    .severity = Diagnostic::Severity::Error,
                    .message = line,
                    .rawContext = line,
                });
            } else if (line.find("Warning") != std::string::npos) {
                diags.push_back(Diagnostic {
                    .severity = Diagnostic::Severity::Warning,
                    .message = line,
                    .rawContext = line,
                });
            }
        }

        return diags;
    }

    // ── pythontex ─────────────────────────────────────────────────────────────
    // pythontex reports structured messages as "PythonTeX error" / "PythonTeX
    // warning" lines, followed by detail lines indented with spaces.

    std::vector<Diagnostic> extractPythontex(const std::string& output)
    {
        std::vector<Diagnostic> diags;
        std::istringstream stream(output);
        std::string line;

        while (std::getline(stream, line)) {
            if (line.find("PythonTeX error") != std::string::npos) {
                diags.push_back(Diagnostic {
                    .severity = Diagnostic::Severity::Error,
                    .message = line,
                    .rawContext = line,
                });
            } else if (line.find("PythonTeX warning") != std::string::npos) {
                diags.push_back(Diagnostic {
                    .severity = Diagnostic::Severity::Warning,
                    .message = line,
                    .rawContext = line,
                });
            }
        }

        return diags;
    }

    // ── fallback ──────────────────────────────────────────────────────────────

    Diagnostic makeFallback(const std::string& stdout)
    {
        return Diagnostic {
            .severity = Diagnostic::Severity::Error,
            .message = "build failed \xe2\x80\x94 run with --verbose to see full engine output",
            .rawContext = stdout,
        };
    }

} // namespace

namespace diagnostics {

// ── public interface ──────────────────────────────────────────────────────────

std::vector<Diagnostic> extract(const RunResult<EngineInvocation>& result)
{
    std::vector<Diagnostic> diags;

    switch (result.kind) {
    case InvokerKind::PdfLatex:
    case InvokerKind::XeLaTeX:
    case InvokerKind::LuaLaTeX:
        if (result.success())
            diags = extractLatexWarnings(result.stdoutOutput, result.invocation.texFile);
        else
            diags = extractLatex(result.stdoutOutput, result.invocation.texFile);
        break;
    default:
        break;
    }

    if (!result.success() && diags.empty())
        diags.push_back(makeFallback(result.stdoutOutput));

    return diags;
}

std::vector<Diagnostic> extract(const RunResult<ToolInvocation>& result)
{
    if (result.success())
        return {};

    std::vector<Diagnostic> diags;

    switch (result.kind) {
    case InvokerKind::Bibtex:
        diags = extractBibtex(result.stdoutOutput);
        break;
    case InvokerKind::Biber:
        diags = extractBiber(result.stdoutOutput);
        break;
    case InvokerKind::Makeindex:
        diags = extractMakeindex(result.stdoutOutput);
        break;
    case InvokerKind::Makeglossaries:
        diags = extractMakeglossaries(result.stdoutOutput);
        break;
    case InvokerKind::Pythontex:
        diags = extractPythontex(result.stdoutOutput);
        break;
    default:
        break;
    }

    if (diags.empty())
        diags.push_back(makeFallback(result.stdoutOutput));

    return diags;
}

} // namespace diagnostics

} // namespace maketex
