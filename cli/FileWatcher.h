#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <stop_token>
#include <vector>

namespace maketex::cli {

// Polls a fixed set of files for mtime/size/existence changes. Watch sets
// come from the .fls input list and are small, so stat-ing them every
// interval is cheap and fully portable — latexmk -pvc polls the same way.
class FileWatcher {
public:
    struct Entry {
        std::filesystem::path path;
        std::int64_t mtimeNs = 0;
        std::uintmax_t size = 0;
        bool exists = false;

        bool operator==(const Entry&) const = default;
    };

    explicit FileWatcher(std::chrono::milliseconds pollInterval,
        std::chrono::milliseconds quietPeriod = std::chrono::milliseconds(200));

    // Replaces the watched paths. Directories contribute their regular files
    // (non-recursive) at poll time, so newly created files are seen.
    void setWatchSet(std::vector<std::filesystem::path> paths);

    // Blocks until at least one watched file changes, then keeps polling
    // until a quiet period passes so editor save sequences coalesce; returns
    // the changed paths. Returns empty when stopToken was triggered or
    // nudge() was called — callers distinguish via stopToken.
    std::vector<std::filesystem::path> waitForChange(std::stop_token stopToken);

    // Wakes a pending waitForChange immediately (forced rebuild).
    void nudge();

    static std::vector<Entry> snapshot(
        const std::vector<std::filesystem::path>& files);

    // Paths whose entries differ between the two snapshots, including files
    // that appear in only one of them.
    static std::vector<std::filesystem::path> diff(
        const std::vector<Entry>& before, const std::vector<Entry>& after);

private:
    // The current watch set with directories expanded to their files.
    std::vector<std::filesystem::path> expandedWatchSet() const;

    // Sleeps for duration; returns false when woken by stop or nudge.
    bool sleepFor(std::chrono::milliseconds duration, std::stop_token& stopToken);

    std::chrono::milliseconds m_pollInterval;
    std::chrono::milliseconds m_quietPeriod;
    std::vector<std::filesystem::path> m_watchSet;

    mutable std::mutex m_mutex;
    std::condition_variable_any m_wake;
    bool m_nudged = false;
};

} // namespace maketex::cli
