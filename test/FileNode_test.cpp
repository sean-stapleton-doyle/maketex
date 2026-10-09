#include "maketex/graph/FileNode.h"

#include "maketex/IFileHasher.h"
#include "maketex/StateDatabase.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <system_error>

using maketex::FileNode;
using maketex::FileRole;
using maketex::FileStateTracker;
using maketex::Hash;
using maketex::IFileHasher;
using maketex::StateDatabase;
namespace fs = std::filesystem;

namespace {

class MockHasher : public IFileHasher {
public:
    std::expected<Hash, std::error_code> hashFile(
        const fs::path&, const std::optional<std::regex>&) const override
    {
        ++callCount;
        return returnHash;
    }

    mutable int callCount = 0;
    Hash returnHash { };
};

std::unique_ptr<SQLite::Database> makeMemoryDb()
{
    return std::make_unique<SQLite::Database>(
        ":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
}

class FileNodeTest : public ::testing::Test {
protected:
    fs::path m_dir;
    StateDatabase m_db { makeMemoryDb() };
    MockHasher m_hasher;

    void SetUp() override
    {
        m_dir = maketex::test_utils::uniqueTempDir("maketex_filenode_test");
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

TEST_F(FileNodeTest, constructorStoresPathAndRole)
{
    FileNode node(m_dir / "main.tex", FileRole::Source);

    EXPECT_EQ(node.path(), m_dir / "main.tex");
    EXPECT_EQ(node.role(), FileRole::Source);
}

TEST_F(FileNodeTest, initialStateIsAbsent)
{
    FileNode node(m_dir / "main.aux", FileRole::Generated);

    EXPECT_FALSE(node.state().exists);
}

TEST_F(FileNodeTest, refreshUpdatesStateForPresentFile)
{
    auto p = write("main.aux", "\\relax");
    FileStateTracker tracker(m_db, m_hasher);
    FileNode node(p, FileRole::Generated);

    auto result = node.refresh(tracker);

    ASSERT_TRUE(result.has_value());
    EXPECT_TRUE(result->exists);
    EXPECT_EQ(result->hash, node.state().hash); // refresh caches what it returns
}

TEST_F(FileNodeTest, refreshReturnsAbsentForMissingFile)
{
    FileStateTracker tracker(m_db, m_hasher);
    FileNode node(m_dir / "nonexistent.aux", FileRole::Generated);

    auto result = node.refresh(tracker);

    ASSERT_TRUE(result.has_value());
    EXPECT_FALSE(result->exists);
}
