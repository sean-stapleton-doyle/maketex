#include "FileWatcher.h"

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <map>
#include <set>
#include <unistd.h>
#include <utility>

#if defined(__APPLE__)
#include <sys/event.h>
#include <sys/time.h>
#include <sys/types.h>
#elif defined(__linux__)
#include <poll.h>
#include <sys/inotify.h>
#endif

namespace fs = std::filesystem;

namespace maketex::cli {

namespace {

    struct FdCloser {
        int fd = -1;
        ~FdCloser()
        {
            if (fd >= 0)
                ::close(fd);
        }
        FdCloser(const FdCloser&) = delete;
        FdCloser& operator=(const FdCloser&) = delete;
        FdCloser(FdCloser&& other) noexcept
            : fd(std::exchange(other.fd, -1))
        {
        }
        FdCloser& operator=(FdCloser&& other) noexcept
        {
            if (this != &other) {
                if (fd >= 0)
                    ::close(fd);
                fd = std::exchange(other.fd, -1);
            }
            return *this;
        }
        explicit FdCloser(int f = -1)
            : fd(f)
        {
        }
    };

    void drainPipe(int fd) noexcept
    {
        char buf[64];
        while (::read(fd, buf, sizeof(buf)) > 0) { }
    }

    // Collects unique parent directories of watched files (and watched
    // directories themselves) to receive creation/rename/modify notifications.
    std::vector<fs::path> watchDirectories(const std::vector<fs::path>& roots,
        const std::vector<fs::path>& expandedFiles)
    {
        std::set<fs::path> dirs;
        std::error_code ec;
        for (const auto& r : roots) {
            if (fs::is_directory(r, ec))
                dirs.insert(r);
            else if (r.has_parent_path() && fs::is_directory(r.parent_path(), ec))
                dirs.insert(r.parent_path());
        }
        for (const auto& f : expandedFiles) {
            if (f.has_parent_path() && fs::is_directory(f.parent_path(), ec))
                dirs.insert(f.parent_path());
        }
        return { dirs.begin(), dirs.end() };
    }

#if defined(__APPLE__) || defined(__linux__)
    class NativeWaiter {
    public:
        NativeWaiter(const std::vector<fs::path>& roots,
            const std::vector<fs::path>& files)
        {
            int pipeFds[2] = { -1, -1 };
            if (::pipe(pipeFds) != 0)
                return;
            for (int fd : pipeFds) {
                ::fcntl(fd, F_SETFD, FD_CLOEXEC);
                ::fcntl(fd, F_SETFL, O_NONBLOCK);
            }
            m_wakeRead = FdCloser(pipeFds[0]);
            m_wakeWrite = FdCloser(pipeFds[1]);

            auto dirs = watchDirectories(roots, files);
            constexpr std::size_t kMaxFds = 256;

#if defined(__APPLE__)
            int kq = ::kqueue();
            if (kq < 0)
                return;
            ::fcntl(kq, F_SETFD, FD_CLOEXEC);
            m_backend = FdCloser(kq);

            std::vector<struct kevent> changes;
            struct kevent wakeEv { };
            EV_SET(&wakeEv, static_cast<uintptr_t>(m_wakeRead.fd), EVFILT_READ,
                EV_ADD | EV_CLEAR, 0, 0, nullptr);
            changes.push_back(wakeEv);

            auto addVnode = [&](const fs::path& p) {
                if (m_watchedFds.size() >= kMaxFds)
                    return;
                int fd = ::open(p.c_str(), O_EVTONLY | O_CLOEXEC);
                if (fd < 0)
                    return;
                m_watchedFds.emplace_back(fd);
                struct kevent ev { };
                EV_SET(&ev, static_cast<uintptr_t>(fd), EVFILT_VNODE,
                    EV_ADD | EV_CLEAR,
                    NOTE_WRITE | NOTE_EXTEND | NOTE_DELETE | NOTE_RENAME | NOTE_ATTRIB,
                    0, nullptr);
                changes.push_back(ev);
            };
            for (const auto& d : dirs)
                addVnode(d);
            for (const auto& f : files)
                addVnode(f);

            if (!changes.empty()) {
                struct timespec zero { 0, 0 };
                ::kevent(m_backend.fd, changes.data(), static_cast<int>(changes.size()),
                    nullptr, 0, &zero);
            }
            m_ready = !m_watchedFds.empty();
#elif defined(__linux__)
            int ifd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
            if (ifd < 0)
                return;
            m_backend = FdCloser(ifd);
            constexpr uint32_t kMask = IN_MODIFY | IN_CLOSE_WRITE | IN_MOVED_TO
                | IN_CREATE | IN_DELETE | IN_ATTRIB | IN_DELETE_SELF | IN_MOVE_SELF;
            int added = 0;
            for (const auto& d : dirs) {
                if (static_cast<std::size_t>(added) >= kMaxFds)
                    break;
                if (::inotify_add_watch(ifd, d.c_str(), kMask) >= 0)
                    ++added;
            }
            m_ready = added > 0;
#endif
        }

        bool ready() const noexcept { return m_ready; }
        int wakeWriteFd() const noexcept { return m_wakeWrite.fd; }

        // Waits up to `timeout` for a filesystem event or wake-pipe signal.
        void wait(std::chrono::milliseconds timeout)
        {
#if defined(__APPLE__)
            struct timespec ts {
                .tv_sec = static_cast<time_t>(timeout.count() / 1000),
                .tv_nsec = static_cast<long>((timeout.count() % 1000) * 1'000'000),
            };
            struct kevent events[16];
            int n = ::kevent(m_backend.fd, nullptr, 0, events, 16, &ts);
            if (n > 0) {
                for (int i = 0; i < n; ++i)
                    if (events[i].filter == EVFILT_READ)
                        drainPipe(m_wakeRead.fd);
            }
#elif defined(__linux__)
            struct pollfd fds[2] = {
                { m_backend.fd, POLLIN, 0 },
                { m_wakeRead.fd, POLLIN, 0 },
            };
            int n = ::poll(fds, 2, static_cast<int>(timeout.count()));
            if (n > 0) {
                if (fds[0].revents & POLLIN) {
                    alignas(struct inotify_event) char buf[4096];
                    while (::read(m_backend.fd, buf, sizeof(buf)) > 0) { }
                }
                if (fds[1].revents & POLLIN)
                    drainPipe(m_wakeRead.fd);
            }
#endif
        }

    private:
        FdCloser m_backend;
        FdCloser m_wakeRead;
        FdCloser m_wakeWrite;
        std::vector<FdCloser> m_watchedFds;
        bool m_ready = false;
    };
#endif

} // namespace

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

void FileWatcher::wakeNativeLocked() noexcept
{
    if (m_wakeWriteFd >= 0) {
        char byte = 1;
        [[maybe_unused]] ssize_t ignored = ::write(m_wakeWriteFd, &byte, 1);
    }
}

bool FileWatcher::sleepFor(std::chrono::milliseconds duration,
    std::stop_token& stopToken)
{
    std::stop_callback onStop(stopToken, [this] {
        {
            std::lock_guard lock(m_mutex);
        }
        m_wake.notify_all();
    });
    std::unique_lock lock(m_mutex);
    bool woken = m_wake.wait_for(lock, duration,
        [&] { return m_nudged || stopToken.stop_requested(); });
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
        wakeNativeLocked();
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

    auto consumeNudgeOrStop = [&] {
        std::lock_guard lock(m_mutex);
        if (m_nudged) {
            m_nudged = false;
            return true;
        }
        return stopToken.stop_requested();
    };

    auto settle = [&](std::vector<Entry> current) -> std::vector<fs::path> {
        // Quiet period: wait until two consecutive snapshots agree, so a burst
        // of editor writes (write temp + rename) triggers one rebuild.
        while (true) {
            if (!sleepFor(m_quietPeriod, stopToken))
                return { };
            auto settled = snapshot(expandedWatchSet());
            if (settled == current)
                break;
            current = std::move(settled);
        }
        return diff(baseline, current);
    };

    // Fast check: a save that landed while the build ran is already visible.
    {
        auto current = snapshot(watched);
        if (!diff(baseline, current).empty())
            return settle(std::move(current));
    }

#if defined(__APPLE__) || defined(__linux__)
    std::vector<fs::path> roots;
    {
        std::lock_guard lock(m_mutex);
        roots = m_watchSet;
    }
    NativeWaiter native(roots, watched);
    struct WakeRegistration {
        FileWatcher* self;
        WakeRegistration(FileWatcher* s, int fd)
            : self(s)
        {
            std::lock_guard lock(self->m_mutex);
            self->m_wakeWriteFd = fd;
        }
        ~WakeRegistration()
        {
            std::lock_guard lock(self->m_mutex);
            self->m_wakeWriteFd = -1;
        }
    };
    std::optional<WakeRegistration> reg;
    std::optional<std::stop_callback<std::function<void()>>> stopWake;
    if (native.ready()) {
        reg.emplace(this, native.wakeWriteFd());
        stopWake.emplace(stopToken, std::function<void()>([this] {
            std::lock_guard lock(m_mutex);
            wakeNativeLocked();
        }));
    }
#endif

    while (true) {
        if (consumeNudgeOrStop())
            return { };

#if defined(__APPLE__) || defined(__linux__)
        if (native.ready()) {
            native.wait(m_pollInterval);
            if (consumeNudgeOrStop())
                return { };
        } else if (!sleepFor(m_pollInterval, stopToken)) {
            return { };
        }
#else
        if (!sleepFor(m_pollInterval, stopToken))
            return { };
#endif

        watched = expandedWatchSet();
        auto current = snapshot(watched);
        if (diff(baseline, current).empty())
            continue;

        return settle(std::move(current));
    }
}

} // namespace maketex::cli
