#include "maketex/diagnostics/LatexErrors.h"

#include "maketex/diagnostics/Categorize.h"
#include "maketex/diagnostics/FileStack.h"
#include "maketex/diagnostics/LogLines.h"

#include <algorithm>
#include <cctype>
#include <regex>

namespace fs = std::filesystem;

namespace maketex::diagnostics {

std::optional<FileLineError> parseFileLineError(std::string_view line,
    const fs::path& workingDir, const ISourceResolver& resolver)
{
    for (std::size_t pos = line.find(':'); pos != std::string_view::npos;
        pos = line.find(':', pos + 1)) {
        std::size_t d = pos + 1;
        while (d < line.size() && std::isdigit(static_cast<unsigned char>(line[d])))
            ++d;
        if (d == pos + 1 || d + 1 >= line.size() || line[d] != ':' || line[d + 1] != ' ')
            continue;

        auto fileToken = line.substr(0, pos);
        if (fileToken.size() >= 2 && fileToken.front() == '"' && fileToken.back() == '"')
            fileToken = fileToken.substr(1, fileToken.size() - 2);
        if (fileToken.empty())
            continue;

        fs::path candidate { std::string(fileToken) };
        bool exists = resolver.isFile(
            candidate.is_absolute() ? candidate : workingDir / candidate);
        // A token with spaces is only a file name if the file exists —
        // otherwise prose like "Error at x.tex:3: …" would match.
        bool hasSpace = fileToken.find_first_of(" \t") != std::string_view::npos;
        if (hasSpace ? !exists : (!hasTexSourceExtension(candidate) && !exists))
            continue;

        auto num = tryParseInt(line.substr(pos + 1, d - pos - 1));
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
            .message = std::string(line.substr(d + 2)),
        };
    }
    return std::nullopt;
}

ContextResult gatherContext(std::span<const std::string> lines, std::size_t index,
    const fs::path& workingDir, const ISourceResolver& resolver)
{
    constexpr std::size_t kContextBefore = 7;
    constexpr std::size_t kLookAhead = 8;

    ContextResult out;
    if (index >= lines.size())
        return out;

    for (std::size_t j = index - std::min(index, kContextBefore); j <= index; ++j)
        out.rawCtx += lines[j] + "\n";

    for (std::size_t j = index + 1; j < lines.size() && j <= index + kLookAhead; ++j) {
        if (lines[j].starts_with("! ") || parseFileLineError(lines[j], workingDir, resolver))
            break; // next error owns its own context
        out.rawCtx += lines[j] + "\n";
        if (lines[j].starts_with("l.")) {
            auto space = lines[j].find(' ');
            auto numStr = std::string_view(lines[j]).substr(2,
                space == std::string_view::npos ? std::string_view::npos : space - 2);
            if (auto n = tryParseInt(numStr); n.has_value()) {
                out.line = n.value();
                return out;
            }
            break;
        }
    }
    return out;
}

std::string foldContinuation(std::string message,
    std::span<const std::string> lines, std::size_t next)
{
    static const std::regex kContinuation { R"(^\(([A-Za-z][\w.-]*)\)\s+(.*)$)" };
    for (std::size_t j = next; j < lines.size(); ++j) {
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

int loadingLine(std::string_view text, const fs::path& name)
{
    const auto stem = name.stem().string();
    const bool isClass = name.extension() == ".cls";
    const bool isPackage = name.extension() == ".sty";
    // \input{chapters/intro} may be written with or without ".tex".
    const auto withoutTex = name.extension() == ".tex"
        ? fs::path(name).replace_extension().generic_string()
        : name.generic_string();
    const auto fullName = name.generic_string();

    const auto lines = splitLines(text);
    for (std::size_t idx = 0; idx < lines.size(); ++idx) {
        auto code = std::string_view(lines[idx]).substr(0, lines[idx].find('%'));
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
                                     : (entry == withoutTex || entry == fullName))
                return static_cast<int>(idx) + 1;
            if (comma == std::string_view::npos)
                break;
            list.remove_prefix(comma + 1);
        }
    }
    return -1;
}

void refineErrors(std::vector<Diagnostic>& diags, const fs::path& texFile,
    const ISourceResolver& resolver)
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
                if (auto text = resolver.readText(source)) {
                    if (int line = loadingLine(*text, name); line > 0) {
                        d.line = line;
                        if (d.sourcePath.empty()) {
                            d.sourcePath = texFile;
                            d.file = texFile.filename().string();
                        }
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

std::vector<Diagnostic> extractLatexErrors(const std::string& output,
    const fs::path& texFile, const ISourceResolver& resolver)
{
    std::vector<Diagnostic> diags;
    const fs::path workingDir = texFile.parent_path();
    FileStack stack(workingDir, texFile, resolver);
    const auto lines = splitLines(output);

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto& line = lines[i];

        // Primary form: `file:line: message` from -file-line-error.
        if (auto fle = parseFileLineError(line, workingDir, resolver)) {
            auto ctx = gatherContext(lines, i, workingDir, resolver);
            auto ref = refForToken(fle->fileToken, workingDir, resolver);
            std::string message = cleanErrorMessage(
                foldContinuation(std::move(fle->message), lines, i + 1));
            std::string category = categorizeError(message);
            diags.push_back(Diagnostic {
                .severity = Diagnostic::Severity::Error,
                .file = std::move(ref.display),
                .sourcePath = std::move(ref.abs),
                .line = fle->line,
                .message = std::move(message),
                .category = std::move(category),
                .rawContext = std::move(ctx.rawCtx),
            });
            continue;
        }

        // Fallback form: `! message`, attributed to the innermost open file.
        // Engine-level errors have no space: "!pdfTeX error: …", "!LuaTeX error: …".
        bool bangError = line.starts_with("! ") && !(line.size() > 2 && line[2] == ' ');
        bool engineError = line.size() > 1 && line[0] == '!' && line[1] != ' '
            && line.contains(" error");
        if (bangError || engineError) {
            auto ctx = gatherContext(lines, i, workingDir, resolver);
            std::string message = cleanErrorMessage(
                foldContinuation(line.substr(bangError ? 2 : 1), lines, i + 1));
            std::string category = categorizeError(message);
            const auto& ref = stack.current();
            diags.push_back(Diagnostic {
                .severity = Diagnostic::Severity::Error,
                .file = ref.display,
                .sourcePath = ref.abs,
                .line = ctx.line,
                .message = std::move(message),
                .category = std::move(category),
                .rawContext = std::move(ctx.rawCtx),
            });
            continue;
        }

        if (isSourceEcho(lines, i))
            continue;
        stack.scan(line);
    }

    refineErrors(diags, texFile, resolver);
    return diags;
}

} // namespace maketex::diagnostics
