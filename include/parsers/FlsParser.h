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
    FlsParser& addIgnorePrefix(std::filesystem::path prefix);

    std::expected<FlsRecord, std::error_code> parse(
        const std::filesystem::path& path) const;

private:
    static std::filesystem::path resolvePath(const std::filesystem::path& pwd,
        std::string_view raw);
    bool isIgnored(const std::filesystem::path& p) const;

    std::vector<std::filesystem::path> m_ignorePrefixes;
};

} // namespace maketex
