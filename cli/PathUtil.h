#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace maketex::cli {

// `p` relative to `dir` when p lies strictly inside it, comparing canonical
// forms (macOS /tmp is /private/tmp); nullopt otherwise.
inline std::optional<std::filesystem::path> relativeInside(
    const std::filesystem::path& p, const std::filesystem::path& dir)
{
    std::error_code ec;
    auto rel = std::filesystem::weakly_canonical(p, ec)
                   .lexically_relative(std::filesystem::weakly_canonical(dir, ec));
    if (rel.empty() || rel == "." || *rel.begin() == "..")
        return std::nullopt;
    return rel;
}

inline bool isInside(const std::filesystem::path& p, const std::filesystem::path& dir)
{
    return relativeInside(p, dir).has_value();
}

// "1 pass", "3 passes".
inline std::string passCount(int passes)
{
    return std::to_string(passes) + (passes == 1 ? " pass" : " passes");
}

} // namespace maketex::cli
