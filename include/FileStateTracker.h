#pragma once

#include "IFileHasher.h"
#include "StateDatabase.h"

#include <expected>
#include <filesystem>
#include <regex>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <unordered_map>

namespace maketex {

// Computes and caches FileState for paths, applying the (mtime, size) fast
// path to avoid rehashing files whose metadata is unchanged since the last
// persisted snapshot.
//
// The tracker is read-only with respect to the database — it uses stored
// states only as a fast-path baseline. The loop driver is responsible for
// deciding when to persist updated states back to the database.
class FileStateTracker {
public:
    FileStateTracker(const StateDatabase& db, const IFileHasher& hasher);

    // Register a per-extension ignore-line pattern for hash computation.
    // Extension without leading dot, e.g. "eps" not ".eps".
    void setIgnorePattern(std::string extension, std::regex pattern);

    // Returns the current FileState for path.
    // Fast path: if stat() mtime+size match the stored state, reuses stored hash.
    // Cold path: reads the file and calls hasher.hashFile() with any registered
    // ignore pattern for the file's extension.
    // Success cases include "file does not exist" — that returns
    // FileState::absent(path). The error channel carries system errors from
    // stat/open (permission denied, I/O error, etc.).
    std::expected<FileState, std::error_code> snapshot(
        const std::filesystem::path& path) const;

    // Nanosecond-resolution modification time as stored in FileState::mtime.
    // Whole-second granularity would let a same-size rewrite within one
    // second falsely hit the fast path and return a stale hash.
    static int64_t modificationTimeNanos(const struct stat& st);

private:
    const StateDatabase& m_db;
    const IFileHasher& m_hasher;
    std::unordered_map<std::string, std::regex> m_ignorePatterns;
};

} // namespace maketex
