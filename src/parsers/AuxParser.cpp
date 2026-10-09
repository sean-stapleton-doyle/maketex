#include "maketex/parsers/AuxParser.h"

#include "maketex/parsers/TextFileParser.h"

#include <set>

namespace maketex {

namespace fs = std::filesystem;

namespace {

    // Guards against an \@input cycle, which TeX itself would loop on but a
    // hand-edited or corrupted .aux could still contain.
    constexpr size_t kMaxAuxDepth = 16;

} // namespace

std::expected<AuxRecord, std::error_code> AuxParser::parse(const fs::path& path) const
{
    // Reads a whole .aux so the walk below can descend into a child at
    // the point of its \@input and resume the parent afterwards.
    auto readLines = [](const fs::path& file)
        -> std::expected<std::vector<std::string>, std::error_code> {
        std::vector<std::string> lines;
        auto r = detail::forEachLine(file, [&](std::string_view line) {
            lines.emplace_back(line);
        });
        if (!r)
            return std::unexpected(r.error());
        return lines;
    };

    auto rootLines = readLines(path);
    if (!rootLines)
        return std::unexpected(rootLines.error());

    struct Frame {
        fs::path file;
        std::vector<std::string> lines;
        size_t next = 0;
    };
    AuxRecord rec;
    std::set<fs::path> visited { fs::weakly_canonical(path) };
    std::vector<Frame> stack;
    stack.push_back({ path, std::move(*rootLines) });
    // Depth-first in document order, as TeX itself reads the .aux tree.
    while (!stack.empty()) {
        auto& frame = stack.back();
        if (frame.next == frame.lines.size()) {
            stack.pop_back();
            continue;
        }
        std::string_view line = frame.lines[frame.next++];
        if (auto b = extractBraces(line, "\\bibdata{"); !b.empty()) {
            splitComma(b, [&](std::string_view name) {
                rec.bibDataFiles.emplace_back(name);
            });
        } else if (auto st = extractBraces(line, "\\bibstyle{"); !st.empty()) {
            rec.bibStyles.emplace_back(st);
        } else if (!line.starts_with("\\citation{")) {
            // A missing child has not been \include'd yet; a repeated one
            // would be a cycle.
            auto child = extractBraces(line, "\\@input{");
            if (child.empty() || stack.size() > kMaxAuxDepth)
                continue;
            auto childPath = frame.file.parent_path() / child;
            if (!visited.insert(fs::weakly_canonical(childPath)).second)
                continue;
            if (auto childLines = readLines(childPath))
                stack.push_back({ std::move(childPath), std::move(*childLines) });
            continue;
        }
        rec.bibtexSignature += line;
        rec.bibtexSignature += '\n';
    }
    return rec;
}

std::string_view AuxParser::extractBraces(std::string_view line, std::string_view prefix)
{
    if (!line.starts_with(prefix))
        return { };
    auto end = line.rfind('}');
    if (end == std::string_view::npos || end < prefix.size())
        return { };
    return line.substr(prefix.size(), end - prefix.size());
}

} // namespace maketex
