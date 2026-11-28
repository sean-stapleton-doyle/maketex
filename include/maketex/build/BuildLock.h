#pragma once

#include <filesystem>

namespace maketex::build {

// An exclusive flock(2) on a lock file, released on destruction. Lock
// trouble (an unwritable directory) is not fatal: the build proceeds
// unlocked, as before locks existed.
class BuildLock {
public:
    explicit BuildLock(const std::filesystem::path& path);
    ~BuildLock();

    BuildLock(const BuildLock&) = delete;
    BuildLock& operator=(const BuildLock&) = delete;

private:
    int m_fd;
};

} // namespace maketex::build
