#include "maketex/parsers/AuxParser.h"

#include "maketex/parsers/TextFileParser.h"

#include <set>

namespace maketex {

namespace fs = std::filesystem;

namespace {

    // Guards against an \@input cycle, which TeX itself would loop on but a
    // hand-edited or corrupted .aux could still contain.
    constexpr size_t kMaxAuxDepth = 16;

    // Lines whose effect on the next pass something already watches.
    bool isCheckedContent(std::string_view line)
    {
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t'))
            line.remove_prefix(1);
        if (line.empty())
            return true;
        constexpr std::string_view kChecked[] = {
            "\\relax", // the kernel's opening line
            "\\gdef \\@abspage@last{", // page total; see note below
            "\\newlabel{", // LaTeX: "Label(s) may have changed"
            "\\@writefile{", // .toc/.lof/.lot: "No file …" on first use
            "\\@input{", // a child .aux, checked line by line itself
            "\\citation{",
            "\\bibdata{",
            "\\bibstyle{",
            "\\bibcite{", // bibtex
            "\\abx@aux@", // biblatex: "Please (re)run" warnings, biber inputs
            "\\providecommand\\hyper@newdestlabel",
            "\\providecommand\\HyField@",
            "\\providecommand*\\HyPL@Entry",
            "\\HyPL@Entry{", // hyperref: own rerun warning
            "\\providecommand\\babel@aux",
            "\\babel@aux{",
            "\\@nameuse{bbl@beforestart}",
            "\\catcode",
            "\\@ifundefined",
            "\\global\\let",
            "\\gdef\\HyperFirstAtBeginDocument",
            "\\ifx\\rEfLiNK",
            "\\fi",
            "\\else",
            "\\gdef\\minted@oldcachelist{", // minted: cache files to clean up
            "\\@newglossary{", // glossaries: tracked by MakeglossariesTool
            "\\@istfilename{",
            "\\glsxtr@", // glossaries-extra / bib2gls: tracked by Bib2glsTool
        };
        for (auto prefix : kChecked)
            if (line.starts_with(prefix))
                return true;
        return false;
    }
    // \@abspage@last is the kernel's page total. It is ignored here because
    // every document writes it, and treating it as unchecked would cost
    // every cold build a second pass; documents that print "page X of Y"
    // use lastpage or zref, whose \newlabel entries are checked.

    // Extracts up to `count` sequential `{...}` brace groups starting at pos.
    std::vector<std::string_view> extractBraceGroups(std::string_view text,
        size_t pos, size_t count)
    {
        std::vector<std::string_view> groups;
        while (groups.size() < count && pos < text.size()) {
            if (text[pos] != '{')
                break;
            int depth = 1;
            size_t start = ++pos;
            while (pos < text.size() && depth > 0) {
                if (text[pos] == '{')
                    ++depth;
                else if (text[pos] == '}')
                    --depth;
                ++pos;
            }
            if (depth != 0)
                break;
            groups.push_back(text.substr(start, pos - start - 1));
        }
        return groups;
    }

    // Extracts bib file names from `src=entries` or `src={a,b}` inside
    // \glsxtr@resource{options}{basename}.
    void extractGlsSrcFiles(std::string_view options, std::vector<std::string>& out)
    {
        size_t pos = 0;
        while ((pos = options.find("src=", pos)) != std::string_view::npos) {
            if (pos > 0 && std::isalnum(static_cast<unsigned char>(options[pos - 1]))) {
                pos += 4;
                continue;
            }
            pos += 4;
            std::string_view val;
            if (pos < options.size() && options[pos] == '{') {
                auto end = options.find('}', pos + 1);
                if (end == std::string_view::npos)
                    break;
                val = options.substr(pos + 1, end - pos - 1);
                pos = end + 1;
            } else {
                auto end = options.find(',', pos);
                val = options.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
                pos = end == std::string_view::npos ? options.size() : end + 1;
            }
            size_t s = 0;
            while (s < val.size()) {
                auto comma = val.find(',', s);
                auto item = val.substr(s, comma == std::string_view::npos ? std::string_view::npos : comma - s);
                while (!item.empty() && (item.front() == ' ' || item.front() == '\t'))
                    item.remove_prefix(1);
                while (!item.empty() && (item.back() == ' ' || item.back() == '\t'))
                    item.remove_suffix(1);
                if (!item.empty())
                    out.emplace_back(item);
                if (comma == std::string_view::npos)
                    break;
                s = comma + 1;
            }
        }
    }

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
    bool lastLineChecked = true;
    // Depth-first in document order, as TeX itself reads the .aux tree.
    while (!stack.empty()) {
        auto& frame = stack.back();
        if (frame.next == frame.lines.size()) {
            stack.pop_back();
            continue;
        }
        std::string_view line = frame.lines[frame.next++];
        // An indented line continues the previous entry (minted's cache
        // list spans several lines) and shares its verdict.
        bool continuation = !line.empty() && (line.front() == ' ' || line.front() == '\t');
        if (!continuation)
            lastLineChecked = isCheckedContent(line);
        if (!lastLineChecked)
            rec.hasUncheckedContent = true;
        if (line.starts_with("\\@newglossary{")) {
            auto groups = extractBraceGroups(line, 13, 4);
            if (groups.size() == 4 && !groups[2].empty() && !groups[3].empty()) {
                rec.glossaries.push_back(GlossarySpec {
                    .triggerExt = std::string(groups[3]),
                    .outputExt = std::string(groups[2]),
                });
            }
        } else if (line.starts_with("\\glsxtr@resource{")) {
            auto groups = extractBraceGroups(line, 16, 2);
            if (groups.size() == 2 && !groups[1].empty()) {
                extractGlsSrcFiles(groups[0], rec.glsBibFiles);
                rec.glsOutputs.emplace_back(groups[1]);
            }
            rec.bib2glsSignature += line;
            rec.bib2glsSignature += '\n';
        } else if (line.starts_with("\\glsxtr@record") || line.starts_with("\\glsxtr@fields")) {
            rec.bib2glsSignature += line;
            rec.bib2glsSignature += '\n';
        }

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
