#pragma once

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
    // Probe the path with the error_code-bearing status() first: std::ifstream
    // does not reliably set errno on open failure, so reading errno after a
    // failed construction can return a stale or meaningless value. status()
    // fills ec portably across POSIX and Windows (and resolves symlinks).
    std::error_code ec;
    auto status = std::filesystem::status(path, ec);
    if (ec)
        return std::unexpected(ec);
    if (status.type() == std::filesystem::file_type::directory)
        return std::unexpected(std::make_error_code(std::errc::is_a_directory));

    std::ifstream f(path);
    if (!f)
        // status() said the path exists and is accessible, yet the stream
        // failed to open — report a generic I/O error rather than a stale
        // errno we cannot trust.
        return std::unexpected(std::make_error_code(std::errc::io_error));
    std::string line;
    while (std::getline(f, line))
        fn(line);
    return { };
}

} // namespace maketex::detail
