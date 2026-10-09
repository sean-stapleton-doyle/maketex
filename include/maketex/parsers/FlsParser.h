#pragma once

#include <expected>
#include <filesystem>
#include <string_view>
#include <system_error>
#include <vector>

namespace maketex {

// Parsed representation of a pdflatex/xelatex/lualatex -recorder .fls file.
//
// inputs and outputs are deduplicated, absolute, weakly-canonical paths.
// A path present in both means the engine wrote and then re-read the file in
// the same run (e.g. .aux) — those are the fixed-point rerun triggers.
struct FlsRecord {
    std::filesystem::path pwd;
    std::vector<std::filesystem::path> inputs;
    std::vector<std::filesystem::path> outputs;
};

class FlsParser {
public:
    // Paths rooted under prefix are excluded from both inputs and outputs.
    // Call multiple times to add several prefixes (e.g. TeX distribution roots).
    // prefix is weakly-canonicalized at registration time.
    FlsParser& addIgnorePrefix(const std::filesystem::path& prefix);

    // Re-roots recorded paths onto pwd: relative entries resolve against it
    // instead of the .fls's PWD line, and absolute entries underneath the
    // recorded PWD get that prefix swapped. Entries outside the recorded
    // PWD (the TeX distribution) pass through untouched. This keeps a prior
    // build's .fls usable after the project directory moves — the situation
    // a restored CI cache is always in.
    FlsParser& overridePwd(const std::filesystem::path& pwd);

    std::expected<FlsRecord, std::error_code> parse(
        const std::filesystem::path& path) const;

private:
    std::filesystem::path resolvePath(const std::filesystem::path& recordedPwd,
        std::string_view raw) const;
    bool isIgnored(const std::filesystem::path& p) const;

    std::vector<std::filesystem::path> m_ignorePrefixes;
    std::filesystem::path m_pwdOverride;
};

} // namespace maketex
