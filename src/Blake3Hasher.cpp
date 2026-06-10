#include "Blake3Hasher.h"

#include <blake3.h>

#include <array>
#include <cerrno>
#include <fstream>
#include <string>

namespace maketex {

namespace {

std::error_code openError()
{
    return std::error_code(errno, std::system_category());
}

} // namespace

std::expected<Hash, std::error_code> Blake3Hasher::hashFile(
    const std::filesystem::path& path,
    const std::optional<std::regex>& ignorePattern) const
{
    blake3_hasher ctx;
    blake3_hasher_init(&ctx);

    if (ignorePattern) {
        // Text mode: feed lines that don't match the ignore pattern. Preserve
        // the input's final-newline state so a file with no ignored lines
        // hashes the same in text mode and binary mode. getline strips the
        // delimiter unconditionally; eof() set after the read means we ran
        // out of bytes before finding one — i.e. no trailing newline on
        // that line.
        std::ifstream file(path);
        if (!file)
            return std::unexpected(openError());
        std::string line;
        while (std::getline(file, line)) {
            bool trailingNewline = !file.eof();
            if (!std::regex_search(line, *ignorePattern)) {
                blake3_hasher_update(&ctx, line.data(), line.size());
                if (trailingNewline)
                    blake3_hasher_update(&ctx, "\n", 1);
            }
        }
    } else {
        // Binary mode: feed raw bytes in chunks.
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return std::unexpected(openError());
        std::array<char, 65536> buf;
        while (file.read(buf.data(), static_cast<std::streamsize>(buf.size()))
            || file.gcount() > 0)
            blake3_hasher_update(&ctx, buf.data(),
                static_cast<size_t>(file.gcount()));
    }

    Hash result {};
    blake3_hasher_finalize(&ctx, result.data(), result.size());
    return result;
}

} // namespace maketex
