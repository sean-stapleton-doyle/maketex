#include "maketex/DocumentProbe.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>
#include <string_view>

namespace maketex {

namespace fs = std::filesystem;

namespace {

    // Editors only look at the first lines for magic comments; so do we.
    constexpr int kMagicCommentLines = 20;
    // A preamble longer than this is not plausible; stop reading there.
    constexpr std::size_t kMaxPreambleBytes = 256 * 1024;
    constexpr int kMaxRootHops = 8;

    std::string lowercase(std::string_view s)
    {
        std::string out(s);
        std::ranges::transform(out, out.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    std::string_view trim(std::string_view s)
    {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
            s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
            s.remove_suffix(1);
        return s;
    }

    // Text before an unescaped %.
    std::string_view stripComment(std::string_view line)
    {
        for (std::size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '\\') {
                ++i; // skip the escaped character (\%)
                continue;
            }
            if (line[i] == '%')
                return line.substr(0, i);
        }
        return line;
    }

    // The preamble of file (up to \begin{document}) with comments removed.
    std::string readPreamble(const fs::path& file)
    {
        std::ifstream in(file);
        std::string preamble;
        std::string line;
        while (std::getline(in, line) && preamble.size() < kMaxPreambleBytes) {
            auto code = stripComment(line);
            auto begin = code.find("\\begin{document}");
            preamble += code.substr(0, begin);
            preamble += '\n';
            if (begin != std::string_view::npos)
                break;
        }
        return preamble;
    }

    // Brace groups following each occurrence of command in text (skipping
    // an optional [...] argument), e.g. the {a,b} of \usepackage[x]{a,b}.
    std::vector<std::string> argumentsOf(std::string_view text, std::string_view command)
    {
        std::vector<std::string> args;
        std::size_t pos = 0;
        while ((pos = text.find(command, pos)) != std::string_view::npos) {
            pos += command.size();
            // \usepackage must not match \usepackageX.
            if (pos < text.size() && std::isalpha(static_cast<unsigned char>(text[pos])))
                continue;
            auto rest = text.substr(pos);
            auto lead = rest.find_first_not_of(" \t\n");
            if (lead == std::string_view::npos)
                break;
            rest.remove_prefix(lead);
            if (rest.starts_with('[')) {
                auto close = rest.find(']');
                if (close == std::string_view::npos)
                    break;
                rest.remove_prefix(close + 1);
                lead = rest.find_first_not_of(" \t\n");
                if (lead == std::string_view::npos)
                    break;
                rest.remove_prefix(lead);
            }
            if (!rest.starts_with('{'))
                continue;
            auto close = rest.find('}');
            if (close == std::string_view::npos)
                break;
            args.emplace_back(rest.substr(1, close - 1));
        }
        return args;
    }

    void addPackages(std::set<std::string>& packages, std::string_view preamble)
    {
        for (auto command : { std::string_view { "\\usepackage" },
                 std::string_view { "\\RequirePackage" } }) {
            for (const auto& list : argumentsOf(preamble, command)) {
                std::string_view rest = list;
                while (!rest.empty()) {
                    auto comma = rest.find(',');
                    auto name = trim(rest.substr(0, comma));
                    if (!name.empty())
                        packages.emplace(name);
                    if (comma == std::string_view::npos)
                        break;
                    rest.remove_prefix(comma + 1);
                }
            }
        }
    }

    bool startsDocument(const fs::path& file)
    {
        std::ifstream in(file);
        std::string line;
        // \documentclass comes first in practice; magic comments, a license
        // header, or \RequirePackage{…} lines may precede it.
        for (int i = 0; i < 200 && std::getline(in, line); ++i) {
            auto code = trim(stripComment(line));
            if (code.starts_with("\\documentclass"))
                return true;
            if (code.starts_with("\\begin{document}"))
                return false;
        }
        return false;
    }

} // namespace

MagicComments readMagicComments(const fs::path& texFile)
{
    MagicComments magic;
    std::ifstream in(texFile);
    std::string line;
    for (int i = 0; i < kMagicCommentLines && std::getline(in, line); ++i) {
        auto text = trim(line);
        auto body = text.find_first_not_of('%');
        if (!text.starts_with('%') || body == std::string_view::npos)
            continue;
        text = trim(text.substr(body));
        if (!text.starts_with('!'))
            continue;
        auto eq = text.find('=');
        if (eq == std::string_view::npos)
            continue;
        auto keyText = lowercase(trim(text.substr(1, eq - 1)));
        auto value = trim(text.substr(eq + 1));
        if (keyText == "tex root" && !magic.root) {
            magic.root = (texFile.parent_path() / fs::path(value)).lexically_normal();
        } else if ((keyText == "tex program" || keyText == "tex ts-program") && !magic.program) {
            magic.program = parseEngineKind(lowercase(value));
        }
    }
    return magic;
}

fs::path resolveRootDocument(const fs::path& texFile)
{
    fs::path current = texFile;
    std::set<fs::path> seen { fs::weakly_canonical(current) };
    for (int hop = 0; hop < kMaxRootHops; ++hop) {
        auto root = readMagicComments(current).root;
        std::error_code ec;
        if (!root || !fs::is_regular_file(*root, ec))
            return current;
        if (!seen.insert(fs::weakly_canonical(*root)).second)
            return texFile; // a cycle names no real root
        current = *root;
    }
    return current;
}

std::vector<fs::path> findRootDocuments(const fs::path& dir)
{
    std::vector<fs::path> roots;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().extension() == ".tex" && entry.is_regular_file(ec)
            && startsDocument(entry.path()))
            roots.push_back(entry.path());
    }
    std::ranges::sort(roots);
    return roots;
}

EngineChoice detectEngine(const fs::path& texFile)
{
    if (auto program = readMagicComments(texFile).program)
        return { .kind = *program, .reason = "% !TeX program" };

    auto preamble = readPreamble(texFile);
    std::set<std::string> packages;
    addPackages(packages, preamble);
    // Shared preambles are commonly \input from a file next to the root.
    for (const auto& name : argumentsOf(preamble, "\\input")) {
        fs::path included = texFile.parent_path() / trim(name);
        if (!included.has_extension())
            included += ".tex";
        auto nested = readPreamble(included);
        addPackages(packages, nested);
        preamble += nested;
    }

    constexpr std::array kLuaOnly = { "luacode", "luatexbase", "luaotfload",
        "luatextra", "luamplib", "luatexja", "luatexja-fontspec", "luacolor",
        "lua-ul", "selnolig", "lua-visual-debug" };
    constexpr std::array kXeOnly = { "xeCJK", "xltxtra", "xunicode", "mathspec",
        "xgreek", "zhspacing", "bidi" };
    constexpr std::array kUnicode = { "fontspec", "unicode-math", "polyglossia",
        "fontsetup" };

    for (const char* pkg : kLuaOnly)
        if (packages.contains(pkg))
            return { .kind = EngineKind::LuaLaTeX, .reason = std::string("loads ") + pkg };
    if (preamble.contains("\\directlua"))
        return { .kind = EngineKind::LuaLaTeX, .reason = "uses \\directlua" };
    for (const char* pkg : kXeOnly)
        if (packages.contains(pkg))
            return { .kind = EngineKind::XeLaTeX, .reason = std::string("loads ") + pkg };
    for (const char* pkg : kUnicode)
        if (packages.contains(pkg))
            return { .kind = EngineKind::LuaLaTeX, .reason = std::string("loads ") + pkg };
    return { };
}

namespace {

    // The whole file with comments removed (bounded like the preamble).
    std::string readSource(const fs::path& file)
    {
        std::ifstream in(file);
        std::string text;
        std::string line;
        while (std::getline(in, line) && text.size() < 4 * kMaxPreambleBytes) {
            text += stripComment(line);
            text += '\n';
        }
        return text;
    }

    // Options in the [...] before {name} on a \usepackage line, if any.
    std::string packageOptions(std::string_view text, std::string_view name)
    {
        for (auto command : { std::string_view { "\\usepackage" }, std::string_view { "\\RequirePackage" } }) {
            std::size_t pos = 0;
            while ((pos = text.find(command, pos)) != std::string_view::npos) {
                pos += command.size();
                auto rest = text.substr(pos);
                if (!rest.starts_with('['))
                    continue;
                auto close = rest.find(']');
                auto brace = rest.find('{', close);
                auto end = rest.find('}', brace);
                if (close == std::string_view::npos || brace == std::string_view::npos
                    || end == std::string_view::npos)
                    continue;
                if (rest.substr(brace + 1, end - brace - 1).contains(name))
                    return std::string(rest.substr(1, close - 1));
            }
        }
        return { };
    }

} // namespace

DocumentRequirements analyzeRequirements(const fs::path& texFile)
{
    DocumentRequirements req;
    req.engine = detectEngine(texFile);

    // The root plus the files it pulls in directly: chapters often hold
    // the \bibliography or \printindex.
    std::string text = readSource(texFile);
    for (auto command : { std::string_view { "\\input" }, std::string_view { "\\include" } }) {
        for (const auto& name : argumentsOf(std::string(text), command)) {
            fs::path child = texFile.parent_path() / trim(name);
            if (!child.has_extension())
                child += ".tex";
            text += readSource(child);
        }
    }

    if (auto cls = argumentsOf(text, "\\documentclass"); !cls.empty())
        req.documentClass = std::string(trim(cls.front()));
    std::set<std::string> packages;
    addPackages(packages, text);
    req.packages.assign(packages.begin(), packages.end());

    auto uses = [&](std::string_view pkg) { return packages.contains(std::string(pkg)); };
    auto need = [&](std::string program, std::string why) {
        for (const auto& [p, w] : req.programs)
            if (p == program)
                return;
        req.programs.emplace_back(std::move(program), std::move(why));
    };

    if (uses("biblatex")) {
        auto opts = packageOptions(text, "biblatex");
        if (opts.contains("backend=bibtex"))
            need("bibtex", "biblatex with backend=bibtex");
        else
            need("biber", "loads biblatex");
    }
    if (text.contains("\\bibliography{"))
        need("bibtex", "uses \\bibliography");
    if (uses("makeidx") || uses("imakeidx") || text.contains("\\makeindex"))
        need("makeindex", uses("imakeidx") ? "loads imakeidx" : "makes an index");
    if (uses("glossaries") || uses("glossaries-extra"))
        need("makeglossaries", uses("glossaries") ? "loads glossaries" : "loads glossaries-extra");
    if (uses("pythontex"))
        need("pythontex", "loads pythontex");
    if (uses("minted")) {
        need("pygmentize", "loads minted");
        req.needsShellEscape = true;
    }
    if (uses("svg"))
        req.needsShellEscape = true;
    return req;
}

} // namespace maketex
