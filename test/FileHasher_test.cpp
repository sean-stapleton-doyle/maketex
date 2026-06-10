#include "Blake3Hasher.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

using maketex::Blake3Hasher;
using maketex::Hash;

namespace fs = std::filesystem;

namespace {

class FileHasherTest : public ::testing::Test {
protected:
    fs::path m_dir;

    void SetUp() override
    {
        m_dir = fs::temp_directory_path() / "maketex_hasher_test";
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

TEST_F(FileHasherTest, sameContentSameHash)
{
    Blake3Hasher h;
    auto a = write("a.tex", "hello world");
    auto b = write("b.tex", "hello world");
    auto ha = h.hashFile(a);
    auto hb = h.hashFile(b);
    ASSERT_TRUE(ha.has_value());
    ASSERT_TRUE(hb.has_value());
    EXPECT_EQ(ha.value(), hb.value());
}

TEST_F(FileHasherTest, differentContentDifferentHash)
{
    Blake3Hasher h;
    auto a = write("a.tex", "hello");
    auto b = write("b.tex", "world");
    auto ha = h.hashFile(a);
    auto hb = h.hashFile(b);
    ASSERT_TRUE(ha.has_value());
    ASSERT_TRUE(hb.has_value());
    EXPECT_NE(ha.value(), hb.value());
}

TEST_F(FileHasherTest, emptyFileHasValidHash)
{
    Blake3Hasher h;
    auto p = write("empty.tex", "");
    auto result = h.hashFile(p);
    EXPECT_TRUE(result.has_value());
}

TEST_F(FileHasherTest, missingFileReturnsError)
{
    Blake3Hasher h;
    auto result = h.hashFile(m_dir / "nonexistent.tex");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), std::errc::no_such_file_or_directory);
}

TEST_F(FileHasherTest, ignorePatternExcludesMatchingLines)
{
    Blake3Hasher h;
    std::regex creationDate { R"(^%%CreationDate: )" };

    // Two EPS files identical except for the creation date line.
    auto a = write("a.eps", "%%CreationDate: Mon Jan  1 00:00:00 2024\n%%Content\n");
    auto b = write("b.eps", "%%CreationDate: Fri Dec 31 23:59:59 2024\n%%Content\n");

    auto ha = h.hashFile(a, creationDate);
    auto hb = h.hashFile(b, creationDate);
    ASSERT_TRUE(ha.has_value());
    ASSERT_TRUE(hb.has_value());
    EXPECT_EQ(ha.value(), hb.value());
}

TEST_F(FileHasherTest, textModeMatchesBinaryWhenNoLinesIgnored)
{
    // When the ignore pattern matches nothing, text-mode hashing must produce
    // the same digest as binary mode — for files with AND without a trailing
    // newline. Previously the text-mode loop unconditionally appended "\n"
    // after every getline, so files without a trailing newline diverged.
    Blake3Hasher h;
    std::regex never { R"(NEVER_MATCHES_ANYTHING)" };

    auto withNewline = write("a.txt", "hello\nworld\n");
    auto withoutNewline = write("b.txt", "hello\nworld");

    auto bin1 = h.hashFile(withNewline);
    auto txt1 = h.hashFile(withNewline, never);
    auto bin2 = h.hashFile(withoutNewline);
    auto txt2 = h.hashFile(withoutNewline, never);
    ASSERT_TRUE(bin1.has_value() && txt1.has_value() && bin2.has_value() && txt2.has_value());

    EXPECT_EQ(bin1.value(), txt1.value());
    EXPECT_EQ(bin2.value(), txt2.value());
}

TEST_F(FileHasherTest, textModeNoTrailingNewline_differsFromTrailingNewline)
{
    Blake3Hasher h;
    std::regex never { R"(NEVER_MATCHES_ANYTHING)" };

    auto a = write("a.txt", "hello");
    auto b = write("b.txt", "hello\n");

    auto ha = h.hashFile(a, never);
    auto hb = h.hashFile(b, never);
    ASSERT_TRUE(ha.has_value() && hb.has_value());
    EXPECT_NE(ha.value(), hb.value());
}

TEST_F(FileHasherTest, ignorePatternDistinguishesNonMatchingDifferences)
{
    Blake3Hasher h;
    std::regex creationDate { R"(^%%CreationDate: )" };

    auto a = write("a.eps", "%%CreationDate: Mon Jan 1 2024\n%%Content A\n");
    auto b = write("b.eps", "%%CreationDate: Mon Jan 1 2024\n%%Content B\n");

    auto ha = h.hashFile(a, creationDate);
    auto hb = h.hashFile(b, creationDate);
    ASSERT_TRUE(ha.has_value());
    ASSERT_TRUE(hb.has_value());
    EXPECT_NE(ha.value(), hb.value());
}
