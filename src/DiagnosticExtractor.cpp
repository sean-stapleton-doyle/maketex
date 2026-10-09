#include "maketex/DiagnosticExtractor.h"

#include "maketex/InvokerKind.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <regex>
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
                // TeX Live quotes names containing spaces: ("./my file.tex"
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    auto close = line.find('"', i + 2);
                    if (close != std::string::npos) {
                        auto token = std::string_view(line).substr(i + 2, close - (i + 2));
                        m_stack.push_back(resolveToken(token, m_workingDir));
                        i = close;
                        continue;
                    }
                }
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
            if (fileToken.size() >= 2 && fileToken.front() == '"' && fileToken.back() == '"')
                fileToken = fileToken.substr(1, fileToken.size() - 2);
            if (fileToken.empty())
                continue;

            fs::path candidate { std::string(fileToken) };
            std::error_code ec;
            bool exists = fs::is_regular_file(
                candidate.is_absolute() ? candidate : workingDir / candidate, ec);
            // A token with spaces is only a file name if the file exists —
            // otherwise prose like "Error at x.tex:3: …" would match.
            bool hasSpace = fileToken.find_first_of(" \t") != std::string_view::npos;
            if (hasSpace ? !exists : (!hasTexSourceExtension(candidate) && !exists))
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

    // Lines where TeX echoes the user's source after an error: "l.42 \\foo{(}"
    // and the indented remainder on the next line. Their parentheses are the
    // document's, not file opens, and must not reach the file stack.
    bool isSourceEcho(const std::vector<std::string>& lines, size_t i)
    {
        auto isLineTag = [](std::string_view l) {
            return l.size() > 2 && l.starts_with("l.") && std::isdigit(static_cast<unsigned char>(l[2]));
        };
        if (isLineTag(lines[i]))
            return true;
        return i > 0 && isLineTag(lines[i - 1]) && lines[i].starts_with(' ');
    }

    // A package or class error continues on lines prefixed with its name:
    //   ./main.tex:24: Package fontspec Error: The font "X" cannot be found;
    //   (fontspec)                this may be but usually is not a fontspec bug.
    // Appends those continuations (starting at lines[next]) to message.
    std::string foldContinuation(std::string message,
        const std::vector<std::string>& lines, size_t next)
    {
        static const std::regex kContinuation { R"(^\(([A-Za-z][\w.-]*)\)\s+(.*)$)" };
        for (size_t j = next; j < lines.size(); ++j) {
            std::smatch m;
            if (!std::regex_match(lines[j], m, kContinuation))
                break;
            auto text = m[2].str();
            if (text.empty())
                continue;
            if (!message.empty() && message.back() != ' ')
                message += ' ';
            message += text;
        }
        return message;
    }

    // The name between `…' in "File `minted.sty' not found" or "I can't
    // find file `chapter'".
    std::string quotedName(std::string_view message)
    {
        auto open = message.find('`');
        auto close = message.find('\'', open == std::string_view::npos ? 0 : open);
        if (open == std::string_view::npos || close == std::string_view::npos)
            return { };
        return std::string(message.substr(open + 1, close - open - 1));
    }

    // The line of `source` that loads `name` (a .sty via \usepackage or
    // \RequirePackage, a .cls via \documentclass, anything else via \input
    // or \include), or -1.
    int loadingLine(const fs::path& source, const fs::path& name)
    {
        std::ifstream in(source);
        if (!in)
            return -1;
        const auto stem = name.stem().string();
        const bool isClass = name.extension() == ".cls";
        const bool isPackage = name.extension() == ".sty";
        // \input{chapters/intro} may be written with or without ".tex".
        const auto withoutTex = name.extension() == ".tex"
            ? fs::path(name).replace_extension().generic_string()
            : name.generic_string();
        std::string line;
        for (int n = 1; std::getline(in, line); ++n) {
            auto code = std::string_view(line).substr(0, line.find('%'));
            bool loads = isClass ? code.contains("\\documentclass")
                : isPackage      ? (code.contains("\\usepackage") || code.contains("\\RequirePackage"))
                                 : (code.contains("\\input") || code.contains("\\include"));
            if (!loads)
                continue;
            // The name as a whole entry of the brace list.
            auto brace = code.find('{');
            if (brace == std::string_view::npos)
                continue;
            auto list = code.substr(brace + 1, code.find('}', brace) - brace - 1);
            while (!list.empty()) {
                auto comma = list.find(',');
                auto entry = list.substr(0, comma);
                while (!entry.empty() && entry.front() == ' ')
                    entry.remove_prefix(1);
                while (!entry.empty() && entry.back() == ' ')
                    entry.remove_suffix(1);
                if (isClass || isPackage ? entry == stem
                                         : (entry == withoutTex || entry == name.generic_string()))
                    return n;
                if (comma == std::string_view::npos)
                    break;
                list.remove_prefix(comma + 1);
            }
        }
        return -1;
    }

    // Post-processing that needs the whole error list:
    //  - "Emergency stop" only restates that an earlier error was fatal;
    //  - a missing package is reported where TeX gave up (often a later
    //    line), so point it at the line that loads it instead;
    //  - errors that need a flag get a hint saying so.
    void refineErrors(std::vector<Diagnostic>& diags, const fs::path& texFile)
    {
        auto isNoise = [](const Diagnostic& d) {
            return d.message.starts_with("Emergency stop")
                || d.message.starts_with("==> Fatal error occurred");
        };
        if (std::ranges::any_of(diags, [&](const Diagnostic& d) { return !isNoise(d); }))
            std::erase_if(diags, isNoise);

        for (auto& d : diags) {
            if (d.category == "missing-file") {
                d.missingFile = quotedName(d.message);
                fs::path name { d.missingFile };
                if (!name.empty()) {
                    auto source = d.sourcePath.empty() ? texFile : d.sourcePath;
                    if (int line = loadingLine(source, name); line > 0) {
                        d.line = line;
                        if (d.sourcePath.empty()) {
                            d.sourcePath = texFile;
                            d.file = texFile.filename().string();
                        }
                    }
                }
            } else if (d.category == "shell-escape-required") {
                d.hint = "rebuild with --shell-escape (or set `shell-escape = true` in "
                         "maketex.toml) — this lets the document run programs, so only "
                         "for documents you trust";
            }
        }
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
                if (lines[j].starts_with("! ") || parseFileLineError(lines[j], workingDir))
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
                std::string message = cleanErrorMessage(
                    foldContinuation(std::move(fle->message), lines, i + 1));
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
            // after '!' mark a continuation, not a new error). Engine-level
            // errors have no space: "!pdfTeX error: …", "!LuaTeX error: …".
            bool bangError = line.starts_with("! ") && !(line.size() > 2 && line[2] == ' ');
            bool engineError = line.size() > 1 && line[0] == '!' && line[1] != ' '
                && line.contains(" error");
            if (bangError || engineError) {
                auto ctx = gatherContext(i);
                std::string message = cleanErrorMessage(
                    foldContinuation(line.substr(bangError ? 2 : 1), lines, i + 1));
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

            if (isSourceEcho(lines, i))
                continue;
            stack.scan(line);
        }

        refineErrors(diags, texFile);
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
            // Warnings raised while LaTeX re-reads its .aux at the end of
            // the run ("Label multiply defined") would point at a generated
            // file; the document is the useful place to look.
            if (fs::path(ref.display).extension() == ".aux") {
                auto root = makeRootRef(texFile);
                d.file = root.display;
                d.sourcePath = root.abs;
                return;
            }
            d.file = ref.display;
            d.sourcePath = ref.abs;
        };

        auto push = [&](Diagnostic d, std::string message) {
            attribute(d);
            d.message = std::move(message);
            diags.push_back(std::move(d));
        };

        // "Overfull \hbox (12.0pt too wide) in paragraph at lines 10--12",
        // "… detected at line 7", "… in alignment at lines 3--9".
        static const std::regex kBox { R"(^(Overfull|Underfull) \\([hv]box) )" };
        static const std::regex kBoxLine { R"((?:at lines?|detected at line) (\d+))" };
        // "Package natbib Warning: …", "Class memoir Warning: …",
        // "LaTeX Font Warning: …" — continued on "(natbib)"/"(Font)" lines.
        static const std::regex kNamedWarning {
            R"(^(?:(Package|Class) ([\w.-]+)|LaTeX (Font)) Warning: (.*)$)"
        };

        for (size_t i = 0; i < lines.size(); ++i) {
            const auto& line = lines[i];

            // LaTeX Warning: <message>, continued on indented lines.
            if (line.starts_with("LaTeX Warning: ")) {
                std::string msg = line.substr(15);
                std::string rawCtx = line;
                // LaTeX aligns continuations under the message text.
                while (i + 1 < lines.size() && lines[i + 1].starts_with("  ")
                    && lines[i + 1].find_first_not_of(' ') != std::string::npos) {
                    ++i;
                    rawCtx += "\n" + lines[i];
                    auto rest = std::string_view(lines[i]);
                    while (!rest.empty() && rest.front() == ' ')
                        rest.remove_prefix(1);
                    msg += ' ';
                    msg += rest;
                }

                // Skip summary lines — individual warnings already cover them,
                // and rerun signals are handled by LogParser, not surfaced here.
                if (msg.starts_with("There were ")
                    || msg.starts_with("Label(s) may have changed"))
                    continue;

                int lineNum = extractInputLine(msg);
                stripInputLineSuffix(msg);
                push(Diagnostic {
                         .severity = Diagnostic::Severity::Warning,
                         .line = lineNum,
                         .category = categorizeWarning(msg),
                         .rawContext = std::move(rawCtx),
                     },
                    std::move(msg));
                continue;
            }

            std::smatch named;
            if (std::regex_match(line, named, kNamedWarning)) {
                bool isFont = named[3].matched;
                std::string name = isFont ? "Font" : named[2].str();
                std::string msg = named[4].str();
                std::string rawCtx = line;

                std::string contPrefix = "(" + name + ")";
                while (i + 1 < lines.size() && lines[i + 1].starts_with(contPrefix)) {
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
                std::string category = categorizeWarning(msg);
                if (category.empty())
                    category = isFont ? "font" : (named[1] == "Class" ? "class" : "package");
                std::string label = isFont ? "Font" : named[1].str() + " " + name;
                push(Diagnostic {
                         .severity = Diagnostic::Severity::Warning,
                         .line = lineNum,
                         .category = std::move(category),
                         .rawContext = std::move(rawCtx),
                     },
                    label + ": " + msg);
                continue;
            }

            // Missing character: There is no X in font Y!
            if (line.starts_with("Missing character: ")) {
                push(Diagnostic {
                         .severity = Diagnostic::Severity::Warning,
                         .category = "missing-glyph",
                         .rawContext = line,
                     },
                    line);
                continue;
            }

            // pdfTeX warning (ext4): destination with the same identifier …
            if (line.starts_with("pdfTeX warning")) {
                push(Diagnostic {
                         .severity = Diagnostic::Severity::Warning,
                         .category = "pdftex",
                         .rawContext = line,
                     },
                    line);
                continue;
            }

            std::smatch box;
            if (std::regex_search(line, box, kBox)) {
                int lineNum = -1;
                if (std::smatch at; std::regex_search(line, at, kBoxLine))
                    lineNum = tryParseInt(at[1].str()).value_or(-1);
                bool overfull = box[1] == "Overfull";
                std::string category = (overfull ? "overfull-" : "underfull-") + box[2].str();
                // Underfull boxes are badness reports, rarely actionable.
                push(Diagnostic {
                         .severity = overfull ? Diagnostic::Severity::Warning
                                              : Diagnostic::Severity::Info,
                         .line = lineNum,
                         .category = std::move(category),
                         .rawContext = line,
                     },
                    line);
                // The box's typeset content follows up to a blank line; its
                // parentheses are the document's text, not file opens.
                while (i + 1 < lines.size() && !lines[i + 1].empty())
                    ++i;
                continue;
            }

            if (isSourceEcho(lines, i))
                continue;
            stack.scan(line);
        }

        return diags;
    }

    // ── bibtex ────────────────────────────────────────────────────────────────

    // BibTeX reports an error on one line and where it happened either on
    // the same line or the next:
    //   I was expecting a `,' or a `}'---line 1 of file refs.bib
    //   I couldn't open database file refs.bib
    //   ---line 4 of file main.aux
    // followed by " : " lines quoting the offending input.
    std::vector<Diagnostic> extractBibtex(const std::string& output)
    {
        std::vector<std::string> lines;
        {
            std::istringstream stream(output);
            std::string line;
            while (std::getline(stream, line))
                lines.push_back(std::move(line));
        }
        static const std::regex kWhere { R"(---line (\d+) of file (.+)$)" };
        // Status chatter, not errors.
        auto isChatter = [](std::string_view l) {
            return l.starts_with("This is BibTeX") || l.starts_with("The top-level auxiliary file")
                || l.starts_with("The style file") || l.starts_with("Database file #")
                || l.starts_with("(There w") || l.starts_with("I'm skipping whatever remains")
                || l.starts_with(" : ") || l.empty();
        };

        std::vector<Diagnostic> diags;
        for (size_t i = 0; i < lines.size(); ++i) {
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
            if (isChatter(line) || line.starts_with("---"))
                continue;

            Diagnostic d {
                .severity = Diagnostic::Severity::Error,
                .rawContext = line,
            };
            std::string message = line;
            std::smatch where;
            const std::string* whereLine = nullptr;
            if (std::regex_search(line, where, kWhere)) {
                message = line.substr(0, static_cast<size_t>(where.position(0)));
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
                // The .aux location is where bibtex read the name; the
                // useful location is the missing file itself.
                d.file = d.missingFile;
                d.line = -1;
            }
            d.message = std::move(message);
            diags.push_back(std::move(d));
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

    // makeglossaries is a Perl script that delegates to makeindex/xindy:
    // the indexer's own "!! "/"## " lines, plus its own "Error:" reports.
    // Not any line merely containing "Error" — entry text can.
    std::vector<Diagnostic> extractMakeglossaries(const std::string& output)
    {
        return extractByLinePatterns(output,
            { { "!! ", true, kError, true },
                { "## ", true, kWarning, true },
                { "*** ", true, kError, true },
                { "Error: ", false, kError, false },
                { "Warning: ", false, kWarning, false } });
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

    namespace {

        // A run that never really happened (program missing) or was cut
        // short has no log worth parsing; say what happened instead.
        template <typename Inv>
        std::optional<Diagnostic> abnormalRun(const RunResult<Inv>& result)
        {
            if (result.notFound)
                return Diagnostic {
                    .severity = Diagnostic::Severity::Error,
                    .message = result.program + " is not installed (not found on PATH)",
                    .category = "missing-tool",
                    .missingFile = result.program,
                };
            if (result.timedOut)
                return Diagnostic {
                    .severity = Diagnostic::Severity::Error,
                    .message = result.program + " was stopped: it ran longer than the timeout",
                    .category = "timeout",
                };
            if (result.cancelled)
                return Diagnostic {
                    .severity = Diagnostic::Severity::Info,
                    .message = "build cancelled",
                    .category = "cancelled",
                };
            return std::nullopt;
        }

    } // namespace

    // ── public interface ──────────────────────────────────────────────────────────

    std::vector<Diagnostic> extract(const RunResult<EngineInvocation>& result)
    {
        if (auto abnormal = abnormalRun(result))
            return { *abnormal };
        std::vector<Diagnostic> diags;

        switch (result.kind) {
        case InvokerKind::PdfLaTeX:
        case InvokerKind::XeLaTeX:
        case InvokerKind::LuaLaTeX:
            if (result.success()) {
                diags = extractLatexWarnings(result.stdoutOutput, result.invocation.texFile);
            } else {
                // Errors first; the warnings of a failed pass still explain
                // a lot (an undefined reference before the fatal error), but
                // box-layout noise does not.
                diags = extractLatex(result.stdoutOutput, result.invocation.texFile);
                for (auto& w : extractLatexWarnings(result.stdoutOutput, result.invocation.texFile))
                    if (!w.category.starts_with("overfull-") && !w.category.starts_with("underfull-"))
                        diags.push_back(std::move(w));
            }
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
        if (auto abnormal = abnormalRun(result))
            return { *abnormal };

        // Tools split their reports between the streams (makeindex writes
        // to stderr); parse both.
        std::string output = result.stdoutOutput;
        if (!result.stderrOutput.empty()) {
            if (!output.empty() && output.back() != '\n')
                output += '\n';
            output += result.stderrOutput;
        }

        std::vector<Diagnostic> diags;
        switch (result.kind) {
        case InvokerKind::Bibtex:
            diags = extractBibtex(output);
            break;
        case InvokerKind::Biber:
            diags = extractBiber(output);
            break;
        case InvokerKind::Makeindex:
            diags = extractMakeindex(output);
            break;
        case InvokerKind::Makeglossaries:
            diags = extractMakeglossaries(output);
            break;
        case InvokerKind::Pythontex:
            diags = extractPythontex(output);
            break;
        default:
            break;
        }

        // A successful run still reports its warnings (a citation with no
        // database entry), but nothing that claims it failed.
        if (result.success()) {
            std::erase_if(diags, [](const Diagnostic& d) {
                return d.severity == Diagnostic::Severity::Error;
            });
            return diags;
        }
        if (std::ranges::none_of(diags, [](const Diagnostic& d) {
                return d.severity == Diagnostic::Severity::Error;
            }))
            diags.insert(diags.begin(), makeFallback(output));
        return diags;
    }

} // namespace diagnostics

} // namespace maketex
