#include "maketex/FileStateTracker.h"

#include "maketex/Blake3Hasher.h"
#include "maketex/IFileHasher.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <chrono>
#include <expected>
#include <filesystem>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <system_error>

using maketex::Blake3Hasher;
using maketex::FileState;
using maketex::FileStateTracker;
using maketex::Hash;
using maketex::IFileHasher;
using maketex::StateDatabase;

namespace fs = std::filesystem;

using maketex::test_utils::makeMemoryDb;
using maketex::test_utils::MockHasher;

namespace {

class FileStateTrackerTest : public ::testing::Test {
protected:
    fs::path m_dir;
    StateDatabase m_db { makeMemoryDb() };
    MockHasher m_hasher;

    void SetUp() override
    {
        m_dir = maketex::test_utils::uniqueTempDir("maketex_tracker_test");
        fs::create_directories(m_dir);
    }

    void TearDown() override { fs::remove_all(m_dir); }

    fs::path write(const std::string& name, const std::string& content)
    {
        fs::path p = m_dir / name;
        std::ofstream(p) << content;
        return p;
    }
};

} // namespace

TEST_F(FileStateTrackerTest, absentFileReturnsAbsentState)
{
    FileStateTracker tracker(m_db, m_hasher);
    auto snap = tracker.snapshot(m_dir / "nonexistent.aux");

    ASSERT_TRUE(snap.has_value());
    EXPECT_FALSE(snap.value().exists);
    EXPECT_EQ(m_hasher.callCount, 0);
}

TEST_F(FileStateTrackerTest, presentFileWithNoStoredState_callsHasher)
{
    FileStateTracker tracker(m_db, m_hasher);
    auto p = write("main.aux", "\\relax");

    auto snap = tracker.snapshot(p);

    ASSERT_TRUE(snap.has_value());
    EXPECT_TRUE(snap.value().exists);
    EXPECT_EQ(snap.value().path, p.string());
    EXPECT_EQ(m_hasher.callCount, 1);
}

TEST_F(FileStateTrackerTest, fastPath_skipsHasher_whenMtimeAndSizeMatch)
{
    // Write the file and get its actual mtime+size.
    auto p = write("main.aux", "\\relax");
    struct stat st;
    ASSERT_EQ(::stat(p.c_str(), &st), 0);

    Hash storedHash;
    storedHash.fill(0xCC);
    FileState stored {
        .path = p.string(),
        .exists = true,
        .mtime = FileStateTracker::modificationTimeNanos(st),
        .size = static_cast<int64_t>(st.st_size),
        .hash = storedHash,
    };
    m_db.upsertFileState(stored);

    FileStateTracker tracker(m_db, m_hasher);
    auto snap = tracker.snapshot(p);

    ASSERT_TRUE(snap.has_value());
    EXPECT_EQ(m_hasher.callCount, 0);
    EXPECT_EQ(snap.value().hash, storedHash);
}

TEST_F(FileStateTrackerTest, coldPath_callsHasher_whenMtimeDiffers)
{
    auto p = write("main.aux", "\\relax");

    // Store a state with a deliberately wrong mtime.
    FileState stale {
        .path = p.string(),
        .exists = true,
        .mtime = 0,
        .size = 6,
        .hash = { },
    };
    m_db.upsertFileState(stale);

    FileStateTracker tracker(m_db, m_hasher);
    ASSERT_TRUE(tracker.snapshot(p).has_value());

    EXPECT_EQ(m_hasher.callCount, 1);
}

TEST_F(FileStateTrackerTest, coldPath_callsHasher_whenStoredIsAbsent)
{
    auto p = write("main.aux", "\\relax");
    m_db.upsertFileState(FileState::absent(p.string()));

    FileStateTracker tracker(m_db, m_hasher);
    ASSERT_TRUE(tracker.snapshot(p).has_value());

    EXPECT_EQ(m_hasher.callCount, 1);
}

TEST_F(FileStateTrackerTest, ignorePatternPassedToHasher)
{
    // Use the real Blake3Hasher to confirm the pattern actually affects the hash.
    Blake3Hasher realHasher;
    FileStateTracker tracker(m_db, realHasher);
    tracker.setIgnorePattern("eps", std::regex { R"(^%%CreationDate: )" });

    auto a = write("a.eps", "%%CreationDate: Mon Jan  1 2024\n%%Content\n");
    auto b = write("b.eps", "%%CreationDate: Fri Dec 31 2024\n%%Content\n");

    auto snapA = tracker.snapshot(a);
    auto snapB = tracker.snapshot(b);

    ASSERT_TRUE(snapA.has_value());
    ASSERT_TRUE(snapB.has_value());

    // Different creation dates are ignored — hashes must match.
    EXPECT_EQ(snapA.value().hash, snapB.value().hash);
}

TEST_F(FileStateTrackerTest, recentlyModifiedFileIsNotTrustedByTheFastPath)
{
    // A file written just now may be rewritten within the same timestamp
    // tick with the same size: its state must not let the fast path skip
    // hashing the new content.
    auto p = write("fresh.bbl", "out v1\n");
    maketex::FileStateTracker tracker(m_db, m_hasher);
    auto first = tracker.snapshot(p);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->mtime, 0) << "racy state kept its mtime";
    m_db.upsertFileState(*first);

    write("fresh.bbl", "out v2\n"); // same size, likely the same mtime tick
    int before = m_hasher.callCount;
    ASSERT_TRUE(tracker.snapshot(p).has_value());
    EXPECT_EQ(m_hasher.callCount, before + 1) << "stale hash reused for new content";
}

TEST_F(FileStateTrackerTest, settledFileKeepsItsMtime)
{
    auto p = write("old.tex", "settled");
    fs::last_write_time(p, fs::file_time_type::clock::now() - std::chrono::minutes(1));
    maketex::FileStateTracker tracker(m_db, m_hasher);
    auto state = tracker.snapshot(p);
    ASSERT_TRUE(state.has_value());
    EXPECT_NE(state->mtime, 0);
}
