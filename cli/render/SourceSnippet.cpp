#include "render/SourceSnippet.h"

#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace maketex::cli {

std::optional<std::string> readSourceLine(const fs::path& path, int line)
{
    if (line <= 0 || path.empty())
        return std::nullopt;

    // Guard against pathological inputs: a diagnostic pointing into a
    // multi-megabyte generated file is not worth a snippet.
    constexpr std::uintmax_t kMaxFileSize = 4ULL * 1024 * 1024;
    std::error_code ec;
    auto size = fs::file_size(path, ec);
    if (ec || size > kMaxFileSize)
        return std::nullopt;

    std::ifstream in(path);
    if (!in)
        return std::nullopt;

    std::string current;
    for (int i = 0; i < line; ++i)
        if (!std::getline(in, current))
            return std::nullopt;
    return current;
}

} // namespace maketex::cli
