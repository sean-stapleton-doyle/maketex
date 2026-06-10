#pragma once

#include "FileStateTracker.h"
#include "StateDatabase.h"

#include <filesystem>

namespace maketex {

enum class FileRole {
    Source,    // user-written: .tex, .bib, images — tracked for out-of-date detection
    Generated, // intermediate: .aux, .toc, .bbl — rerun-trigger candidates
    Output,    // final product: .pdf, .synctex.gz — not tracked for reruns
};

// A node in the build dependency graph representing a single file.
//
// refresh() snapshots the file's current state via the tracker and caches it;
// the loop driver compares snapshots across passes to detect convergence.
class FileNode {
public:
    FileNode(std::filesystem::path path, FileRole role);

    const std::filesystem::path& path() const { return m_path; }
    FileRole role() const { return m_role; }
    void setRole(FileRole role) { m_role = role; }

    // Snapshots the current state from disk. Stores and returns the new state.
    // On error, leaves the cached state untouched and returns it.
    const FileState& refresh(FileStateTracker& tracker);

    // The state from the most recent refresh(). Default (exists=false) if never refreshed.
    const FileState& state() const { return m_state; }

    // True once refresh() has run — distinguishes a real absent-on-disk state
    // from the never-snapshotted default, so callers don't persist the latter.
    bool hasFreshState() const { return m_refreshed; }

private:
    std::filesystem::path m_path;
    FileRole m_role;
    FileState m_state;
    bool m_refreshed = false;
};

} // namespace maketex
