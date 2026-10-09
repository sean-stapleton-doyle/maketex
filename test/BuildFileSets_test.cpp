#include "maketex/Build.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;
using maketex::BuildOptions;
using maketex::scanBuildFiles;

namespace {

class BuildFileSetsTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_buildfilesets_test"; }

    BuildOptions makeOptions() const
    {
        return BuildOptions { .texFile = m_dir / "main.tex" };
    }
};

bool contains(const std::vector<fs::path>& haystack, const fs::path& needle)
{
    return std::ranges::find(haystack, fs::weakly_canonical(needle))
        != haystack.end();
}

} // namespace

TEST_F(BuildFileSetsTest, returnsNulloptWithoutFls)
{
    EXPECT_FALSE(scanBuildFiles(makeOptions()).has_value());
}

TEST_F(BuildFileSetsTest, separatesSourcesFromGenerated)
{
    // Default output mode: the .fls lives in the hidden .maketex directory.
    fs::create_directories(m_dir / ".maketex");
    writeFile(".maketex/main.fls",
        "PWD " + m_dir.string() + "\n"
            + "INPUT main.tex\n"
            + "INPUT chapter.tex\n"
            + "OUTPUT main.aux\n"
            + "INPUT main.aux\n"
            + "OUTPUT main.pdf\n");

    auto sets = scanBuildFiles(makeOptions());
    ASSERT_TRUE(sets.has_value());

    EXPECT_TRUE(contains(sets->sources, m_dir / "main.tex"));
    EXPECT_TRUE(contains(sets->sources, m_dir / "chapter.tex"));
    // Read-back files like .aux are generated, not sources — watching them
    // would make every build retrigger itself.
    EXPECT_FALSE(contains(sets->sources, m_dir / "main.aux"));
    EXPECT_TRUE(contains(sets->generated, m_dir / "main.aux"));
    EXPECT_TRUE(contains(sets->generated, m_dir / "main.pdf"));
}

TEST_F(BuildFileSetsTest, honorsOutputDir)
{
    fs::path outDir = m_dir / "out";
    fs::create_directories(outDir);
    writeFile("out/main.fls",
        "PWD " + m_dir.string() + "\n"
            + "INPUT main.tex\n"
            + "OUTPUT out/main.pdf\n");

    BuildOptions options { .texFile = m_dir / "main.tex", .outputDir = outDir };
    auto sets = scanBuildFiles(options);
    ASSERT_TRUE(sets.has_value());
    EXPECT_TRUE(contains(sets->sources, m_dir / "main.tex"));
    EXPECT_TRUE(contains(sets->generated, outDir / "main.pdf"));
}
