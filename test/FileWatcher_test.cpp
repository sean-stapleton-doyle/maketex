#include "FileWatcher.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stop_token>
#include <thread>

namespace fs = std::filesystem;
using maketex::cli::FileWatcher;

namespace {

class FileWatcherTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_watcher_test"; }
};

bool contains(const std::vector<fs::path>& haystack, const fs::path& needle)
{
    return std::ranges::find(haystack, needle) != haystack.end();
}

} // namespace

TEST_F(FileWatcherTest, snapshotRecordsExistenceAndSize)
{
    auto file = writeFile("a.tex", "hello");
    auto missing = m_dir / "missing.tex";

    auto entries = FileWatcher::snapshot({ file, missing });
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_TRUE(entries[0].exists);
    EXPECT_EQ(entries[0].size, 5u);
    // Only compared for equality; libstdc++'s file clock has a different
    // epoch, so the value may be negative.
    EXPECT_NE(entries[0].mtimeNs, 0);
    EXPECT_FALSE(entries[1].exists);
}

TEST_F(FileWatcherTest, diffIsEmptyForIdenticalSnapshots)
{
    auto file = writeFile("a.tex", "hello");
    auto before = FileWatcher::snapshot({ file });
    auto after = FileWatcher::snapshot({ file });
    EXPECT_TRUE(FileWatcher::diff(before, after).empty());
}

TEST_F(FileWatcherTest, diffSeesContentGrowth)
{
    auto file = writeFile("a.tex", "hello");
    auto before = FileWatcher::snapshot({ file });
    writeFile("a.tex", "hello world");
    auto after = FileWatcher::snapshot({ file });

    auto changed = FileWatcher::diff(before, after);
    EXPECT_TRUE(contains(changed, file));
}

TEST_F(FileWatcherTest, diffSeesDeletion)
{
    auto file = writeFile("a.tex", "hello");
    auto before = FileWatcher::snapshot({ file });
    fs::remove(file);
    auto after = FileWatcher::snapshot({ file });

    auto changed = FileWatcher::diff(before, after);
    EXPECT_TRUE(contains(changed, file));
}

TEST_F(FileWatcherTest, diffSeesCreation)
{
    auto missing = m_dir / "new.tex";
    auto before = FileWatcher::snapshot({ missing });
    writeFile("new.tex", "x");
    auto after = FileWatcher::snapshot({ missing });

    auto changed = FileWatcher::diff(before, after);
    EXPECT_TRUE(contains(changed, missing));
}

TEST_F(FileWatcherTest, diffSeesPathsLeavingTheWatchSet)
{
    auto a = writeFile("a.tex", "x");
    auto b = writeFile("b.tex", "x");
    auto before = FileWatcher::snapshot({ a, b });
    auto after = FileWatcher::snapshot({ a });

    auto changed = FileWatcher::diff(before, after);
    EXPECT_TRUE(contains(changed, b));
    EXPECT_FALSE(contains(changed, a));
}

TEST_F(FileWatcherTest, untouchedSiblingsAreNotReported)
{
    auto a = writeFile("a.tex", "x");
    auto b = writeFile("b.tex", "x");
    auto before = FileWatcher::snapshot({ a, b });
    writeFile("a.tex", "xx");
    auto after = FileWatcher::snapshot({ a, b });

    auto changed = FileWatcher::diff(before, after);
    EXPECT_TRUE(contains(changed, a));
    EXPECT_FALSE(contains(changed, b));
}

TEST_F(FileWatcherTest, changeBeforeWaitingIsSeenAgainstAnEarlierBaseline)
{
    // A save made while a build runs happens between captureBaseline() and
    // waitForChange(): it must still be reported.
    auto file = m_dir / "main.tex";
    std::ofstream(file) << "v1";
    FileWatcher watcher(std::chrono::milliseconds(20), std::chrono::milliseconds(20));
    watcher.setWatchSet({ file });
    auto preBuild = watcher.captureBaseline();

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::ofstream(file) << "v2, saved mid-build";

    std::stop_source stop;
    auto changed = watcher.waitForChange(stop.get_token(), preBuild);
    ASSERT_EQ(changed.size(), 1u);
    EXPECT_EQ(changed[0], file);
}

TEST_F(FileWatcherTest, pathsRevealedByTheBuildAreNotChanges)
{
    auto tex = m_dir / "main.tex";
    auto chapter = m_dir / "chapter.tex";
    std::ofstream(tex) << "x";
    std::ofstream(chapter) << "y";
    FileWatcher watcher(std::chrono::milliseconds(20), std::chrono::milliseconds(20));
    watcher.setWatchSet({ tex });
    auto preBuild = watcher.captureBaseline();
    watcher.setWatchSet({ tex, chapter }); // the build found chapter.tex

    std::stop_source stop;
    std::jthread stopper([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        stop.request_stop();
    });
    EXPECT_TRUE(watcher.waitForChange(stop.get_token(), preBuild).empty());
}

TEST_F(FileWatcherTest, exclusionAppliesInsideWatchedDirectories)
{
    auto pdf = m_dir / "main.pdf";
    std::ofstream(pdf) << "%PDF";
    FileWatcher watcher(std::chrono::milliseconds(20), std::chrono::milliseconds(20));
    watcher.setWatchSet({ m_dir }, [&](const fs::path& p) { return p.extension() == ".pdf"; });
    EXPECT_TRUE(watcher.captureBaseline().empty());
}
