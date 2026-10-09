#pragma once

#include <filesystem>
#include <string>

namespace maketex {

// Encodes absolute paths for storage in the state database so a cache
// restored at a different checkout path still hits: paths under the project
// root are stored relative (generic separators), everything else — the TeX
// distribution, other mounts — stays absolute. A default-constructed codec
// is the identity (used by tests and tools without a project notion).
class PathCodec {
public:
    PathCodec() = default;
    explicit PathCodec(const std::filesystem::path& root);

    std::string encode(const std::filesystem::path& abs) const;
    std::filesystem::path decode(const std::string& stored) const;

private:
    std::filesystem::path m_root; // weakly canonical; empty = identity
};

} // namespace maketex
