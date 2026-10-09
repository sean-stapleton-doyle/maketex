#include "maketex/graph/FileNode.h"

namespace maketex {

FileNode::FileNode(std::filesystem::path path, FileRole role)
    : m_path(std::move(path))
    , m_role(role)
    , m_state(FileState::absent(m_path.string()))
{
}

std::expected<FileState, std::error_code> FileNode::refresh(FileStateTracker& tracker)
{
    auto snap = tracker.snapshot(m_path);
    if (!snap)
        return std::unexpected(snap.error());
    m_state = std::move(*snap);
    m_refreshed = true;
    return m_state;
}

} // namespace maketex
