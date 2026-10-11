#include "maketex/FileStateTracker.h"

#include <cerrno>
#include <chrono>
#include <sys/stat.h>

namespace maketex {

FileStateTracker::FileStateTracker(const StateDatabase& db, const IFileHasher& hasher)
    : m_db(db)
    , m_hasher(hasher)
{
}

void FileStateTracker::setIgnorePattern(std::string extension, std::regex pattern)
{
    m_ignorePatterns.insert_or_assign(std::move(extension), std::move(pattern));
}

int64_t FileStateTracker::modificationTimeNanos(const struct stat& st)
{
#ifdef __APPLE__
    const auto& ts = st.st_mtimespec;
#else
    const auto& ts = st.st_mtim;
#endif
    return static_cast<int64_t>(ts.tv_sec) * 1'000'000'000
        + static_cast<int64_t>(ts.tv_nsec);
}

std::expected<FileState, std::error_code> FileStateTracker::snapshot(
    const std::filesystem::path& path) const
{
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) {
        if (errno == ENOENT)
            return FileState::absent(path.string());
        return std::unexpected(std::error_code(errno, std::system_category()));
    }

    auto mtime = modificationTimeNanos(st);
    auto size = static_cast<int64_t>(st.st_size);

    // Fast path: reuse the stored hash when mtime and size are unchanged.
    if (auto stored = m_db.getFileState(path.string());
        stored && stored->exists && stored->mtime == mtime && stored->size == size)
        return *stored;

    // Cold path: look up the ignore pattern for this extension, then hash.
    std::optional<std::regex> ignorePattern;
    auto ext = path.extension().string();
    if (!ext.empty() && ext.front() == '.')
        ext = ext.substr(1);
    if (auto it = m_ignorePatterns.find(ext); it != m_ignorePatterns.end())
        ignorePattern = it->second;

    auto hashed = m_hasher.hashFile(path, ignorePattern);
    if (!hashed.has_value())
        return std::unexpected(hashed.error());

    // A file modified within a timestamp tick of now could be rewritten
    // again with the same size and the same mtime (Linux stamps files with
    // a coarse clock, HFS+ to the second, FAT to two seconds), and the fast
    // path would then return this hash for the new content. Like git's
    // "racily clean" entries, such a state is kept without its mtime, so
    // the next lookup hashes again.
    constexpr int64_t kRacyWindowNanos = 3'000'000'000;
    auto nowNanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    bool racy = nowNanos - mtime < kRacyWindowNanos; // also true for future mtimes

    return FileState {
        .path = path.string(),
        .exists = true,
        .mtime = racy ? 0 : mtime,
        .size = size,
        .hash = hashed.value(),
    };
}

} // namespace maketex
