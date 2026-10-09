#include "FileWatcher.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

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
    EXPECT_GT(entries[0].mtimeNs, 0);
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
