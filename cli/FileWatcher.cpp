#include "FileWatcher.h"

#include <algorithm>
#include <map>
#include <utility>

namespace fs = std::filesystem;

namespace maketex::cli {

FileWatcher::FileWatcher(std::chrono::milliseconds pollInterval,
    std::chrono::milliseconds quietPeriod)
    : m_pollInterval(pollInterval)
    , m_quietPeriod(quietPeriod)
{
}

void FileWatcher::setWatchSet(std::vector<fs::path> paths, Exclusion exclude)
{
    std::lock_guard lock(m_mutex);
    m_watchSet = std::move(paths);
    m_exclude = std::move(exclude);
}

std::vector<FileWatcher::Entry> FileWatcher::captureBaseline() const
{
    return snapshot(expandedWatchSet());
}

std::vector<fs::path> FileWatcher::expandedWatchSet() const
{
    std::vector<fs::path> roots;
    Exclusion exclude;
    {
        std::lock_guard lock(m_mutex);
        roots = m_watchSet;
        exclude = m_exclude;
    }
    std::vector<fs::path> files;
    for (const auto& p : roots) {
        std::error_code ec;
        if (fs::is_directory(p, ec)) {
            for (const auto& entry : fs::directory_iterator(p, ec)) {
                if (entry.is_regular_file(ec))
                    files.push_back(entry.path());
            }
        } else {
            files.push_back(p);
        }
    }
    if (exclude)
        std::erase_if(files, exclude);
    std::ranges::sort(files);
    files.erase(std::ranges::unique(files).begin(), files.end());
    return files;
}

std::vector<FileWatcher::Entry> FileWatcher::snapshot(
    const std::vector<fs::path>& files)
{
    std::vector<Entry> entries;
    entries.reserve(files.size());
    for (const auto& p : files) {
        Entry entry { .path = p };
        std::error_code ec;
        auto mtime = fs::last_write_time(p, ec);
        if (!ec) {
            entry.exists = true;
            entry.mtimeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                mtime.time_since_epoch())
                                .count();
            entry.size = fs::file_size(p, ec);
            if (ec)
                entry.size = 0;
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<fs::path> FileWatcher::diff(const std::vector<Entry>& before,
    const std::vector<Entry>& after)
{
    std::map<fs::path, const Entry*> byPath;
    for (const auto& entry : before)
        byPath[entry.path] = &entry;

    std::vector<fs::path> changed;
    for (const auto& entry : after) {
        auto it = byPath.find(entry.path);
        if (it == byPath.end()) {
            changed.push_back(entry.path); // newly watched / newly created
        } else {
            if (*it->second != entry)
                changed.push_back(entry.path);
            byPath.erase(it);
        }
    }
    for (const auto& [path, entry] : byPath)
        changed.push_back(path); // no longer present in `after`

    std::ranges::sort(changed);
    return changed;
}

bool FileWatcher::sleepFor(std::chrono::milliseconds duration,
    std::stop_token& stopToken)
{
    std::unique_lock lock(m_mutex);
    bool woken = m_wake.wait_for(lock, stopToken, duration,
        [this] { return m_nudged; });
    if (m_nudged) {
        m_nudged = false;
        return false;
    }
    return !stopToken.stop_requested() && !woken;
}

void FileWatcher::nudge()
{
    {
        std::lock_guard lock(m_mutex);
        m_nudged = true;
    }
    m_wake.notify_all();
}

std::vector<fs::path> FileWatcher::waitForChange(std::stop_token stopToken,
    const std::optional<std::vector<Entry>>& since)
{
    auto watched = expandedWatchSet();
    auto baseline = snapshot(watched);
    if (since) {
        // Keep the pre-build state of every path that existed in `since`;
        // paths only the build revealed keep their current state.
        std::map<fs::path, const Entry*> earlier;
        for (const auto& entry : *since)
            earlier[entry.path] = &entry;
        for (auto& entry : baseline)
            if (auto it = earlier.find(entry.path); it != earlier.end())
                entry = *it->second;
    }

    while (true) {
        if (!sleepFor(m_pollInterval, stopToken))
            return { };

        watched = expandedWatchSet();
        auto current = snapshot(watched);
        auto changed = diff(baseline, current);
        if (changed.empty())
            continue;

        // Quiet period: wait until two consecutive polls agree, so a burst
        // of editor writes triggers one rebuild.
        while (true) {
            if (!sleepFor(m_quietPeriod, stopToken))
                return { };
            auto settled = snapshot(expandedWatchSet());
            if (settled == current)
                break;
            current = std::move(settled);
        }
        return diff(baseline, current);
    }
}

} // namespace maketex::cli
