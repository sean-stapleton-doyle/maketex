#pragma once

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <vector>

namespace maketex::cli {

// Watches a fixed set of files for mtime/size/existence changes using native
// OS file notifications (kqueue on macOS, inotify on Linux) with a periodic
// stat fallback so save-to-rebuild latency is near-instant while remaining
// reliable on network filesystems or large watch sets.
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
        std::chrono::milliseconds quietPeriod = std::chrono::milliseconds(80));

    // Paths for which this returns true are never watched — applied after
    // directories are expanded, so a watched directory cannot smuggle in
    // the build's own outputs.
    using Exclusion = std::function<bool(const std::filesystem::path&)>;

    // Replaces the watched paths. Directories contribute their regular files
    // (non-recursive) at poll time, so newly created files are seen.
    void setWatchSet(std::vector<std::filesystem::path> paths, Exclusion exclude = { });

    // The current state of the watch set, to pass to waitForChange later.
    std::vector<Entry> captureBaseline() const;

    // Blocks until at least one watched file changes, then keeps polling
    // until a quiet period passes so editor save sequences coalesce; returns
    // the changed paths. Returns empty when stopToken was triggered or
    // nudge() was called — callers distinguish via stopToken.
    //
    // Changes are measured against `since` when given — a baseline captured
    // before a build, so a save made while it ran still counts. Paths not in
    // `since` (discovered by that build) start from their current state.
    std::vector<std::filesystem::path> waitForChange(std::stop_token stopToken,
        const std::optional<std::vector<Entry>>& since = std::nullopt);

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

    // Signals an active native event waiter (if any) to wake immediately.
    void wakeNativeLocked() noexcept;

    std::chrono::milliseconds m_pollInterval;
    std::chrono::milliseconds m_quietPeriod;
    std::vector<std::filesystem::path> m_watchSet;
    Exclusion m_exclude;

    mutable std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_nudged = false;
    int m_wakeWriteFd = -1;
};

} // namespace maketex::cli
