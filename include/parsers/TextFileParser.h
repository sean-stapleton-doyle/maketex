#pragma once

#include <cerrno>
#include <expected>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace maketex::detail {

// Opens path and calls fn(line) for every line in order.
// Returns an error_code if the file cannot be opened.
//
// Templated on Fn so the callback is monomorphized into the calling TU —
// avoids the per-line std::function indirection that would otherwise dominate
// parsing cost on large .log files.
template <typename Fn>
std::expected<void, std::error_code> forEachLine(
    const std::filesystem::path& path, Fn&& fn)
{
    std::ifstream f(path);
    if (!f)
        return std::unexpected(std::error_code(errno, std::system_category()));
    std::string line;
    while (std::getline(f, line))
        fn(line);
    return {};
}

} // namespace maketex::detail
