#include "maketex/build/BuildLock.h"

#include "maketex/Build.h"

#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace maketex::build {

BuildLock::BuildLock(const fs::path& path)
    : m_fd(::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600))
{
    if (m_fd >= 0)
        while (::flock(m_fd, LOCK_EX) != 0 && errno == EINTR) { }
}

BuildLock::~BuildLock()
{
    if (m_fd >= 0)
        ::close(m_fd); // releases the lock
}

} // namespace maketex::build

namespace maketex {

fs::path buildLockPath(const fs::path& outDir, const std::string& jobName)
{
    return outDir / ("." + jobName + ".lock");
}

} // namespace maketex
