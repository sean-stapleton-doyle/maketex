#include "maketex/parsers/FlsParser.h"

#include "maketex/parsers/TextFileParser.h"

#include <algorithm>
#include <unordered_set>

namespace maketex {

namespace fs = std::filesystem;

namespace {

    // Canonicalizes paths without throwing when a directory along the path
    // (e.g. an unreadable system TEXMFVAR) denies stat/read permissions.
    fs::path safeWeaklyCanonical(const fs::path& p)
    {
        std::error_code ec;
        auto canon = fs::weakly_canonical(p, ec);
        return ec ? p.lexically_normal() : canon;
    }

} // namespace

FlsParser& FlsParser::addIgnorePrefix(const fs::path& prefix)
{
    m_ignorePrefixes.push_back(safeWeaklyCanonical(prefix));
    return *this;
}

FlsParser& FlsParser::overridePwd(const fs::path& pwd)
{
    m_pwdOverride = safeWeaklyCanonical(pwd);
    return *this;
}

std::expected<FlsRecord, std::error_code> FlsParser::parse(const fs::path& path) const
{
    FlsRecord rec;
    std::unordered_set<std::string> seenIn, seenOut;

    auto r = detail::forEachLine(path, [&](std::string_view line) {
        if (line.starts_with("PWD ")) {
            rec.pwd = safeWeaklyCanonical(fs::path(line.substr(4)));
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

fs::path FlsParser::resolvePath(const fs::path& recordedPwd,
    std::string_view raw) const
{
    fs::path p(raw);
    if (p.is_relative()) {
        const fs::path& base
            = m_pwdOverride.empty() ? recordedPwd : m_pwdOverride;
        if (!base.empty())
            p = base / p;
        return safeWeaklyCanonical(p);
    }

    p = safeWeaklyCanonical(p);
    if (!m_pwdOverride.empty() && !recordedPwd.empty()) {
        auto rel = p.lexically_relative(recordedPwd);
        if (!rel.empty() && rel != "." && *rel.begin() != "..")
            return safeWeaklyCanonical(m_pwdOverride / rel);
    }
    return p;
}

bool FlsParser::isIgnored(const fs::path& p) const
{
    return std::ranges::any_of(m_ignorePrefixes, [&](const fs::path& prefix) {
        // ranges::mismatch stops at the end of the shorter range, so a path
        // shorter than the prefix no longer reads past p.end() (the C++14
        // 4-iterator std::mismatch overload required p to be at least as long
        // as prefix — undefined behaviour otherwise).
        auto [a, b] = std::ranges::mismatch(prefix, p);
        return a == prefix.end();
    });
}

FlsParser projectFlsParser(const fs::path& projectDir,
    const std::vector<fs::path>& ignorePrefixes)
{
    FlsParser parser;
    parser.overridePwd(projectDir);
    for (const auto& prefix : ignorePrefixes)
        parser.addIgnorePrefix(prefix);
    return parser;
}

} // namespace maketex
