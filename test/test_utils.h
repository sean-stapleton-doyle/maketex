#pragma once

#include "maketex/IFileHasher.h"
#include "maketex/StateDatabase.h"

#include <gtest/gtest.h>

#include <SQLiteCpp/SQLiteCpp.h>

#include <expected>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <regex>
#include <string>
#include <system_error>

namespace maketex::test_utils {

namespace fs = std::filesystem;

inline std::unique_ptr<SQLite::Database> makeMemoryDb()
{
    return std::make_unique<SQLite::Database>(
        ":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
}

// IFileHasher fake: returns a preset hash and counts invocations.
class MockHasher : public IFileHasher {
public:
    std::expected<Hash, std::error_code> hashFile(
        const fs::path&, const std::optional<std::regex>&) const override
    {
        ++callCount;
        return returnHash;
    }

    mutable int callCount = 0;
    Hash returnHash {};
};

// GoogleTest fixture base providing a per-test temp directory under
// std::filesystem::temp_directory_path() / dirName(). Subclasses implement
// dirName() to pick a unique name.
class TempDirFixture : public ::testing::Test {
protected:
    fs::path m_dir;

    virtual const char* dirName() const = 0;

    void SetUp() override
    {
        m_dir = fs::temp_directory_path() / dirName();
        fs::create_directories(m_dir);
    }

    void TearDown() override { fs::remove_all(m_dir); }

    fs::path writeFile(const std::string& name, const std::string& content) const
    {
        fs::path p = m_dir / name;
        std::ofstream(p) << content;
        return p;
    }
};

} // namespace maketex::test_utils
