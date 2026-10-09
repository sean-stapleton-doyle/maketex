#include "render/SourceSnippet.h"

#include "test_utils.h"

#include <gtest/gtest.h>

using maketex::cli::readSourceLine;

namespace {

class SourceSnippetTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_snippet_test"; }
};

} // namespace

TEST_F(SourceSnippetTest, readsTheRequestedLine)
{
    auto p = writeFile("main.tex", "line one\nline two\nline three\n");
    EXPECT_EQ(readSourceLine(p, 1), "line one");
    EXPECT_EQ(readSourceLine(p, 3), "line three");
}

TEST_F(SourceSnippetTest, outOfRangeLineIsNullopt)
{
    auto p = writeFile("main.tex", "only\n");
    EXPECT_EQ(readSourceLine(p, 2), std::nullopt);
    EXPECT_EQ(readSourceLine(p, 0), std::nullopt);
    EXPECT_EQ(readSourceLine(p, -1), std::nullopt);
}

TEST_F(SourceSnippetTest, missingFileIsNullopt)
{
    EXPECT_EQ(readSourceLine(m_dir / "nope.tex", 1), std::nullopt);
    EXPECT_EQ(readSourceLine({ }, 1), std::nullopt);
}
