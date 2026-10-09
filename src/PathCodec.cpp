#include "maketex/PathCodec.h"

namespace fs = std::filesystem;

namespace maketex {

PathCodec::PathCodec(const fs::path& root)
    : m_root(root.empty() ? fs::path { } : fs::weakly_canonical(root))
{
}

std::string PathCodec::encode(const fs::path& abs) const
{
    if (m_root.empty())
        return abs.string();
    auto rel = fs::weakly_canonical(abs).lexically_relative(m_root);
    // Only paths strictly under the root relativise; "." (the root itself)
    // and "../…" escapes stay absolute so they cannot collide or silently
    // re-anchor after a checkout move.
    if (rel.empty() || rel == "." || *rel.begin() == "..")
        return abs.string();
    return rel.generic_string();
}

fs::path PathCodec::decode(const std::string& stored) const
{
    fs::path p { stored };
    if (m_root.empty() || p.is_absolute())
        return p;
    return fs::weakly_canonical(m_root / p);
}

} // namespace maketex
