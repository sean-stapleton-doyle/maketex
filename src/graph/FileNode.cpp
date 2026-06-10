#include "graph/FileNode.h"

namespace maketex {

FileNode::FileNode(std::filesystem::path path, FileRole role)
    : m_path(std::move(path))
    , m_role(role)
    , m_state(FileState::absent(m_path.string()))
{
}

const FileState& FileNode::refresh(FileStateTracker& tracker)
{
    auto snap = tracker.snapshot(m_path);
    if (snap.has_value())
        m_state = std::move(snap.value());
    return m_state;
}

bool FileNode::changed(const FileState& before, const FileState& after)
{
    if (before.exists != after.exists)
        return true;
    if (!before.exists && !after.exists)
        return false;
    return before.hash != after.hash;
}

} // namespace maketex
