#include "parsers/FlsParser.h"

#include "parsers/TextFileParser.h"

#include <algorithm>
#include <unordered_set>

namespace maketex {

namespace fs = std::filesystem;

FlsParser& FlsParser::addIgnorePrefix(fs::path prefix)
{
    m_ignorePrefixes.push_back(fs::weakly_canonical(std::move(prefix)));
    return *this;
}

std::expected<FlsRecord, std::error_code> FlsParser::parse(const fs::path& path) const
{
    FlsRecord rec;
    std::unordered_set<std::string> seenIn, seenOut;

    auto r = detail::forEachLine(path, [&](std::string_view line) {
        if (line.starts_with("PWD ")) {
            rec.pwd = fs::weakly_canonical(fs::path(line.substr(4)));
        } else if (line.starts_with("INPUT ")) {
            auto p = resolvePath(rec.pwd, line.substr(6));
            if (!isIgnored(p) && seenIn.insert(p.string()).second)
                rec.inputs.push_back(p);
        } else if (line.starts_with("OUTPUT ")) {
            auto p = resolvePath(rec.pwd, line.substr(7));
            if (!isIgnored(p) && seenOut.insert(p.string()).second)
                rec.outputs.push_back(p);
        }
    });
    if (!r)
        return std::unexpected(r.error());

    return rec;
}

fs::path FlsParser::resolvePath(const fs::path& pwd, std::string_view raw)
{
    fs::path p(raw);
    if (p.is_relative() && !pwd.empty())
        p = pwd / p;
    return fs::weakly_canonical(p);
}

bool FlsParser::isIgnored(const fs::path& p) const
{
    return std::ranges::any_of(m_ignorePrefixes, [&](const fs::path& prefix) {
        auto [a, b] = std::mismatch(prefix.begin(), prefix.end(), p.begin(), p.end());
        return a == prefix.end();
    });
}

} // namespace maketex
