#include "maketex/DiagnosticExtractor.h"

#include "maketex/InvokerKind.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>

namespace fs = std::filesystem;

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
        if (ec != std::errc { } || ptr != last)
            return std::nullopt;
        return value;
    }

    // ── source attribution ────────────────────────────────────────────────────

    // A file reference recovered from engine output: the display form for
    // Diagnostic::file and, when the file exists on disk, the resolved
    // absolute path for Diagnostic::sourcePath.
    struct SourceRef {
        std::string display;
        fs::path abs;
    };

    bool hasTexSourceExtension(const fs::path& p)
    {
        static constexpr std::string_view kExtensions[] = { ".tex", ".sty",
            ".cls", ".def", ".ltx", ".cfg", ".clo", ".fd", ".bbl", ".toc",
            ".ind", ".bib" };
        auto ext = p.extension().string();
        return std::ranges::find(kExtensions, ext) != std::ranges::end(kExtensions);
    }

    // Display form of a path token: relative to the engine's working
    // directory when it lies underneath it (engines print absolute paths
    // when invoked with an absolute tex file), normalised otherwise.
    std::string displayFor(const fs::path& p, const fs::path& workingDir)
    {
        if (p.is_absolute() && !workingDir.empty()) {
            std::error_code ec;
            auto rel = fs::weakly_canonical(p, ec)
                           .lexically_relative(fs::weakly_canonical(workingDir, ec));
            if (!rel.empty() && *rel.begin() != "..")
                return rel.generic_string();
        }
        return p.lexically_normal().generic_string();
    }

    // Resolves a path token printed by the engine against its working
    // directory (the tex file's directory). Returns nullopt when the token
    // does not name an existing file.
    std::optional<SourceRef> resolveToken(
        std::string_view token, const fs::path& workingDir)
    {
        if (token.empty())
            return std::nullopt;
        fs::path p { std::string(token) };
        fs::path full = p.is_absolute() ? p : workingDir / p;
        std::error_code ec;
        if (!fs::is_regular_file(full, ec))
            return std::nullopt;
        return SourceRef {
            .display = displayFor(p, workingDir),
            .abs = fs::weakly_canonical(full, ec),
        };
    }

    SourceRef makeRootRef(const fs::path& texFile)
    {
        SourceRef ref { texFile.filename().string(), { } };
        std::error_code ec;
        if (fs::is_regular_file(texFile, ec))
            ref.abs = fs::weakly_canonical(texFile, ec);
        return ref;
    }

    // Tracks TeX's paren-based open-file reporting: `(` followed by a path
    // pushes a file, a bare `)` pops. Parens that do not introduce an
    // existing file (version strings, message text) push a sentinel so the
    // matching `)` pops the sentinel and not a real file. Reliable only
    // because EngineBase raises max_print_line — wrapped lines would split
    // tokens mid-path.
    class FileStack {
    public:
        FileStack(fs::path workingDir, const fs::path& texFile)
            : m_workingDir(std::move(workingDir))
            , m_root(makeRootRef(texFile))
        {
        }

        void scan(const std::string& line)
        {
            for (size_t i = 0; i < line.size(); ++i) {
                if (line[i] == ')') {
                    if (!m_stack.empty())
                        m_stack.pop_back();
                    continue;
                }
                if (line[i] != '(')
                    continue;
                size_t j = i + 1;
                while (j < line.size() && line[j] != ' ' && line[j] != '('
                    && line[j] != ')')
                    ++j;
                auto token = std::string_view(line).substr(i + 1, j - (i + 1));
                m_stack.push_back(resolveToken(token, m_workingDir));
                i = j - 1; // resume at the token's terminator
            }
        }

        // The innermost open file; the root document when nothing resolved.
        const SourceRef& current() const
        {
            for (const auto& entry : m_stack | std::views::reverse)
                if (entry.has_value())
                    return *entry;
            return m_root;
        }

    private:
        fs::path m_workingDir;
        SourceRef m_root;
        std::vector<std::optional<SourceRef>> m_stack;
    };

    // ── message categorisation ────────────────────────────────────────────────

    bool containsAny(std::string_view msg, std::initializer_list<std::string_view> needles)
    {
        return std::ranges::any_of(needles, [&](std::string_view n) {
            return msg.find(n) != std::string_view::npos;
        });
    }

    std::string categorizeError(std::string_view msg)
    {
        if (msg.starts_with("Undefined control sequence"))
            return "undefined-macro";
        if (containsAny(msg, { "font", "Font" })
            && containsAny(msg, { "cannot be found", "not loadable", "cannot be loaded" }))
            return "missing-font";
        if (containsAny(msg, { "not found" })
            && (msg.starts_with("File `") || msg.starts_with("I can't find file")))
            return "missing-file";
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

    // Strips engine boilerplate from an error message: the "LaTeX Error: "
    // prefix and a trailing period.
    std::string cleanErrorMessage(std::string message)
    {
        if (message.starts_with("LaTeX Error: "))
            message = message.substr(13);
        if (!message.empty() && message.back() == '.')
            message.pop_back();
        return message;
    }

    // ── TeX engine (pdflatex / xelatex / lualatex) ───────────────────────────

    // A `file:line: message` error marker produced by -file-line-error.
    struct FileLineError {
        std::string fileToken;
        int line = -1;
        std::string message;
    };

    // Matches `<path>:<digits>: <message>` where <path> is whitespace-free
    // and either carries a TeX-ish extension or names an existing file
    // relative to workingDir. This is the primary error form; the bare `! `
    // form remains as fallback for engines run without -file-line-error.
    std::optional<FileLineError> parseFileLineError(
        const std::string& line, const fs::path& workingDir)
    {
        for (size_t pos = line.find(':'); pos != std::string::npos;
            pos = line.find(':', pos + 1)) {
            size_t d = pos + 1;
            while (d < line.size() && std::isdigit((unsigned char)line[d]))
                ++d;
            if (d == pos + 1 || d + 1 >= line.size() || line[d] != ':'
                || line[d + 1] != ' ')
                continue;

            auto fileToken = std::string_view(line).substr(0, pos);
            if (fileToken.empty()
                || fileToken.find_first_of(" \t") != std::string_view::npos)
                continue;

            fs::path candidate { std::string(fileToken) };
            std::error_code ec;
            bool exists = fs::is_regular_file(
                candidate.is_absolute() ? candidate : workingDir / candidate, ec);
            if (!hasTexSourceExtension(candidate) && !exists)
                continue;

            auto num = tryParseInt(std::string_view(line).substr(pos + 1, d - pos - 1));
            if (!num.has_value())
                continue;

            // A second space after the colon marks a continuation, not a new
            // error — the "file:line:  ==> Fatal error occurred" summary is
            // the `!  ==>` line in -file-line-error form.
            if (d + 2 < line.size() && line[d + 2] == ' ')
                return std::nullopt;

            return FileLineError {
                .fileToken = std::string(fileToken),
                .line = num.value(),
                .message = line.substr(d + 2),
            };
        }
        return std::nullopt;
    }

    // Builds the SourceRef for a file token from an error marker: display is
    // the normalised token, abs only when the file exists.
    SourceRef refForToken(const std::string& token, const fs::path& workingDir)
    {
        if (auto ref = resolveToken(token, workingDir))
            return *ref;
        return SourceRef { displayFor(fs::path(token), workingDir), { } };
    }

    std::vector<Diagnostic> extractLatex(
        const std::string& output, const std::filesystem::path& texFile)
    {
        std::vector<Diagnostic> diags;
        const fs::path workingDir = texFile.parent_path();
        FileStack stack(workingDir, texFile);

        // Index-based scan over all lines: the look-ahead for `l.N` must not
        // consume lines, or an error inside the look-ahead window would be
        // silently dropped.
        std::vector<std::string> lines;
        {
            std::istringstream stream(output);
            std::string line;
            while (std::getline(stream, line))
                lines.push_back(std::move(line));
        }

        constexpr size_t kContextBefore = 7;
        constexpr size_t kLookAhead = 8;

        struct ContextResult {
            std::string rawCtx;
            int line = -1; // line number found in the look-ahead, -1 if none
        };

        // rawContext: up to kContextBefore lines of history plus the
        // look-ahead up to the `l.N` line (or the next error). The line
        // number found in the look-ahead (if any) rides along (F.21).
        auto gatherContext = [&](size_t i) -> ContextResult {
            ContextResult out;
            for (size_t j = i - std::min(i, kContextBefore); j <= i; ++j)
                out.rawCtx += lines[j] + "\n";

            for (size_t j = i + 1; j < lines.size() && j <= i + kLookAhead; ++j) {
                if (lines[j].starts_with("! "))
                    break; // next error owns its own context
                out.rawCtx += lines[j] + "\n";
                if (lines[j].starts_with("l.")) {
                    auto space = lines[j].find(' ');
                    auto numStr = std::string_view(lines[j]).substr(2,
                        space == std::string_view::npos ? std::string_view::npos
                                                        : space - 2);
                    if (auto n = tryParseInt(numStr); n.has_value()) {
                        out.line = n.value();
                        return out;
                    }
                    break;
                }
            }
            return out;
        };

        for (size_t i = 0; i < lines.size(); ++i) {
            const auto& line = lines[i];

            // Primary form: `file:line: message` from -file-line-error.
            if (auto fle = parseFileLineError(line, workingDir)) {
                auto ctx = gatherContext(i);
                auto ref = refForToken(fle->fileToken, workingDir);
                std::string message = cleanErrorMessage(std::move(fle->message));
                diags.push_back(Diagnostic {
                    .severity = Diagnostic::Severity::Error,
                    .file = ref.display,
                    .sourcePath = ref.abs,
                    .line = fle->line,
                    .category = categorizeError(message),
                    .rawContext = std::move(ctx.rawCtx),
                });
                diags.back().message = std::move(message);
                continue;
            }

            // Fallback form: `! message`, attributed to the innermost open
            // file. Skip the "!  ==>" fatal-error summary line (two spaces
            // after '!' mark a continuation, not a new error).
            if (line.starts_with("! ") && !(line.size() > 2 && line[2] == ' ')) {
                auto ctx = gatherContext(i);
                std::string message = cleanErrorMessage(line.substr(2));
                const auto& ref = stack.current();
                diags.push_back(Diagnostic {
                    .severity = Diagnostic::Severity::Error,
                    .file = ref.display,
                    .sourcePath = ref.abs,
                    .line = ctx.line,
                    .category = categorizeError(message),
                    .rawContext = std::move(ctx.rawCtx),
                });
                diags.back().message = std::move(message);
                continue;
            }

            stack.scan(line);
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

    // Strips an "on input line N" suffix (the location already carries it)
    // or a bare trailing period.
    void stripInputLineSuffix(std::string& msg)
    {
        auto lineTagPos = msg.rfind(" on input line ");
        if (lineTagPos != std::string::npos)
            msg.erase(lineTagPos);
        else if (!msg.empty() && msg.back() == '.')
            msg.pop_back();
    }

    std::vector<Diagnostic> extractLatexWarnings(
        const std::string& output, const std::filesystem::path& texFile)
    {
        std::vector<Diagnostic> diags;
        const fs::path workingDir = texFile.parent_path();
        FileStack stack(workingDir, texFile);

        std::vector<std::string> lines;
        {
            std::istringstream stream(output);
            std::string line;
            while (std::getline(stream, line))
                lines.push_back(std::move(line));
        }

        auto attribute = [&](Diagnostic& d) {
            const auto& ref = stack.current();
            d.file = ref.display;
            d.sourcePath = ref.abs;
        };

        for (size_t i = 0; i < lines.size(); ++i) {
            const auto& line = lines[i];

            // LaTeX Warning: <message>
            if (line.starts_with("LaTeX Warning: ")) {
                std::string msg = line.substr(15);

                // Skip summary lines — individual warnings already cover them,
                // and rerun signals are handled by LogParser, not surfaced here.
                if (msg.starts_with("There were ")
                    || msg.starts_with("Label(s) may have changed"))
                    continue;

                int lineNum = extractInputLine(msg);
                stripInputLineSuffix(msg);

                Diagnostic d {
                    .severity = Diagnostic::Severity::Warning,
                    .line = lineNum,
                    .category = categorizeWarning(msg),
                    .rawContext = line,
                };
                attribute(d);
                d.message = std::move(msg);
                diags.push_back(std::move(d));
                continue;
            }

            // Package <name> Warning: <message>, with continuation lines
            // prefixed "(<name>)" folded into a single message.
            if (line.starts_with("Package ")) {
                auto warnPos = line.find(" Warning: ");
                if (warnPos != std::string::npos) {
                    std::string name = line.substr(8, warnPos - 8);
                    std::string msg = line.substr(warnPos + 10);
                    std::string rawCtx = line;

                    std::string contPrefix = "(" + name + ")";
                    while (i + 1 < lines.size()
                        && lines[i + 1].starts_with(contPrefix)) {
                        ++i;
                        rawCtx += "\n" + lines[i];
                        auto rest = std::string_view(lines[i]).substr(contPrefix.size());
                        while (!rest.empty() && rest.front() == ' ')
                            rest.remove_prefix(1);
                        if (!rest.empty()) {
                            msg += ' ';
                            msg += rest;
                        }
                    }

                    int lineNum = extractInputLine(msg);
                    stripInputLineSuffix(msg);

                    Diagnostic d {
                        .severity = Diagnostic::Severity::Warning,
                        .line = lineNum,
                        .category = "package",
                        .rawContext = std::move(rawCtx),
                    };
                    attribute(d);
                    d.message = "Package ";
                    d.message += name;
                    d.message += ": ";
                    d.message += msg;
                    diags.push_back(std::move(d));
                    continue;
                }
            }

            // Missing character: There is no X in font Y!
            if (line.starts_with("Missing character: ")) {
                Diagnostic d {
                    .severity = Diagnostic::Severity::Warning,
                    .category = "missing-glyph",
                    .rawContext = line,
                };
                attribute(d);
                d.message = line;
                diags.push_back(std::move(d));
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
                    lineNum = tryParseInt(
                        std::string_view(line).substr(start, end - start))
                                  .value_or(-1);
                }

                Diagnostic d {
                    .severity = Diagnostic::Severity::Warning,
                    .line = lineNum,
                    .category = "overfull-hbox",
                    .rawContext = line,
                };
                attribute(d);
                d.message = line;
                diags.push_back(std::move(d));
                continue;
            }

            stack.scan(line);
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
                    .category = "missing-file",
                    .rawContext = output,
                });
                return diags; // one fatal error is enough
            }

            // "Warning--..." lines
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
            }
        }

        return diags;
    }

    // ── pattern-driven tool extractors ────────────────────────────────────────
    // biber, makeindex, makeglossaries, and pythontex all report diagnostics
    // as single lines recognisable by a marker token. One scan loop, one
    // pattern table per tool.

    struct LinePattern {
        std::string_view token;
        bool prefixMatch; // starts_with vs. contains
        Diagnostic::Severity severity;
        bool messageAfterToken; // message = rest of line after token, else whole line
    };

    std::vector<Diagnostic> extractByLinePatterns(const std::string& output,
        std::initializer_list<LinePattern> patterns)
    {
        std::vector<Diagnostic> diags;
        std::istringstream stream(output);
        std::string line;

        while (std::getline(stream, line)) {
            for (const auto& p : patterns) {
                auto pos = p.prefixMatch
                    ? (line.starts_with(p.token) ? size_t { 0 } : std::string::npos)
                    : line.find(p.token);
                if (pos == std::string::npos)
                    continue;
                diags.push_back(Diagnostic {
                    .severity = p.severity,
                    .message = p.messageAfterToken
                        ? line.substr(pos + p.token.size())
                        : line,
                    .rawContext = line,
                });
                break; // first matching pattern wins
            }
        }

        return diags;
    }

    constexpr auto kError = Diagnostic::Severity::Error;
    constexpr auto kWarning = Diagnostic::Severity::Warning;

    std::vector<Diagnostic> extractBiber(const std::string& output)
    {
        auto diags = extractByLinePatterns(output,
            { { "ERROR - ", false, kError, true },
                { "WARN - ", false, kWarning, true } });
        // "I didn't find a database entry for 'key'" — the message keeps the
        // key; the category makes it machine-matchable.
        for (auto& d : diags)
            if (d.message.find("didn't find a database entry") != std::string::npos)
                d.category = "undefined-citation";
        return diags;
    }

    // makeindex writes errors prefixed with "!! " and warnings prefixed with "## ".
    std::vector<Diagnostic> extractMakeindex(const std::string& output)
    {
        return extractByLinePatterns(output,
            { { "!! ", true, kError, true },
                { "## ", true, kWarning, true } });
    }

    // makeglossaries is a Perl script that delegates to makeindex/xindy.
    // Errors are reported as lines containing "Error" (fatal) or "Warning".
    std::vector<Diagnostic> extractMakeglossaries(const std::string& output)
    {
        return extractByLinePatterns(output,
            { { "Error", false, kError, false },
                { "Warning", false, kWarning, false } });
    }

    // pythontex reports structured messages as "PythonTeX error" / "PythonTeX
    // warning" lines, followed by detail lines indented with spaces.
    std::vector<Diagnostic> extractPythontex(const std::string& output)
    {
        return extractByLinePatterns(output,
            { { "PythonTeX error", false, kError, false },
                { "PythonTeX warning", false, kWarning, false } });
    }

    // ── fallback ──────────────────────────────────────────────────────────────

    Diagnostic makeFallback(const std::string& stdout)
    {
        return Diagnostic {
            .severity = Diagnostic::Severity::Error,
            .message = "build failed — run with --verbose to see full engine output",
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
        case InvokerKind::PdfLaTeX:
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
            return { };

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
