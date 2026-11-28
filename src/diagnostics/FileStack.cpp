#include "maketex/diagnostics/FileStack.h"

#include <algorithm>
#include <cctype>
#include <ranges>

namespace fs = std::filesystem;

namespace maketex::diagnostics {

bool hasTexSourceExtension(const fs::path& p)
{
    static constexpr std::string_view kExtensions[] = { ".tex", ".sty",
        ".cls", ".def", ".ltx", ".cfg", ".clo", ".fd", ".bbl", ".toc",
        ".ind", ".bib" };
    auto ext = p.extension().string();
    return std::ranges::find(kExtensions, ext) != std::ranges::end(kExtensions);
}

std::string displayFor(const fs::path& p, const fs::path& workingDir,
    const ISourceResolver& resolver)
{
    if (p.is_absolute() && !workingDir.empty()) {
        auto rel = resolver.canonical(p).lexically_relative(resolver.canonical(workingDir));
        if (!rel.empty() && *rel.begin() != "..")
            return rel.generic_string();
    }
    return p.lexically_normal().generic_string();
}

std::optional<SourceRef> resolveToken(std::string_view token,
    const fs::path& workingDir, const ISourceResolver& resolver)
{
    if (token.empty())
        return std::nullopt;
    fs::path p { std::string(token) };
    fs::path full = p.is_absolute() ? p : workingDir / p;
    if (!resolver.isFile(full))
        return std::nullopt;
    return SourceRef {
        .display = displayFor(p, workingDir, resolver),
        .abs = resolver.canonical(full),
    };
}

SourceRef makeRootRef(const fs::path& texFile, const ISourceResolver& resolver)
{
    SourceRef ref { texFile.filename().string(), { } };
    if (resolver.isFile(texFile))
        ref.abs = resolver.canonical(texFile);
    return ref;
}

SourceRef refForToken(std::string_view token, const fs::path& workingDir,
    const ISourceResolver& resolver)
{
    if (auto ref = resolveToken(token, workingDir, resolver))
        return *ref;
    return SourceRef { displayFor(fs::path(std::string(token)), workingDir, resolver), { } };
}

bool isSourceEcho(std::span<const std::string> lines, std::size_t index)
{
    if (index >= lines.size())
        return false;
    auto isLineTag = [](std::string_view l) {
        return l.size() > 2 && l.starts_with("l.")
            && std::isdigit(static_cast<unsigned char>(l[2]));
    };
    if (isLineTag(lines[index]))
        return true;
    return index > 0 && isLineTag(lines[index - 1]) && lines[index].starts_with(' ');
}

FileStack::FileStack(fs::path workingDir, const fs::path& texFile,
    const ISourceResolver& resolver)
    : m_workingDir(std::move(workingDir))
    , m_resolver(resolver)
    , m_root(makeRootRef(texFile, resolver))
{
}

void FileStack::scan(std::string_view line)
{
    for (std::size_t i = 0; i < line.size(); ++i) {
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
            if (close != std::string_view::npos) {
                auto token = line.substr(i + 2, close - (i + 2));
                m_stack.push_back(resolveToken(token, m_workingDir, m_resolver));
                i = close;
                continue;
            }
        }
        std::size_t j = i + 1;
        while (j < line.size() && line[j] != ' ' && line[j] != '(' && line[j] != ')')
            ++j;
        auto token = line.substr(i + 1, j - (i + 1));
        m_stack.push_back(resolveToken(token, m_workingDir, m_resolver));
        i = j - 1; // resume at the token's terminator
    }
}

const SourceRef& FileStack::current() const
{
    for (const auto& entry : m_stack | std::views::reverse)
        if (entry.has_value())
            return *entry;
    return m_root;
}

} // namespace maketex::diagnostics
