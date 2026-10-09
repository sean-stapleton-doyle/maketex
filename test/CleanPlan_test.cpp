#include "CleanPlan.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;
using maketex::BuildFileSets;
using maketex::cli::computeCleanPlan;

namespace {

class CleanPlanTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_cleanplan_test"; }

    void SetUp() override
    {
        TempDirFixture::SetUp();
        m_texFile = writeFile("main.tex", "x");
    }

    bool removes(const maketex::cli::CleanPlan& plan, const fs::path& p) const
    {
        return std::ranges::find(plan.remove, p) != plan.remove.end();
    }

    fs::path m_texFile;
};

} // namespace

TEST_F(CleanPlanTest, removesAuxFilesButKeepsProducts)
{
    writeFile("main.aux", "x");
    writeFile("main.log", "x");
    writeFile("main.bbl", "x");
    writeFile("main.fls", "x");
    writeFile("main.pdf", "x");
    writeFile("main.synctex.gz", "x");
    writeFile(".maketex.db", "x");

    auto plan = computeCleanPlan(std::nullopt, m_texFile, m_dir, "main",
        /*all=*/false, { });

    EXPECT_TRUE(removes(plan, m_dir / "main.aux"));
    EXPECT_TRUE(removes(plan, m_dir / "main.log"));
    EXPECT_TRUE(removes(plan, m_dir / "main.bbl"));
    EXPECT_TRUE(removes(plan, m_dir / "main.fls"));
    EXPECT_FALSE(removes(plan, m_dir / "main.pdf"));
    EXPECT_FALSE(removes(plan, m_dir / "main.synctex.gz"));
    EXPECT_FALSE(removes(plan, m_dir / ".maketex.db"));
    EXPECT_FALSE(removes(plan, m_texFile));
}

TEST_F(CleanPlanTest, allAlsoRemovesProductsAndDatabase)
{
    writeFile("main.pdf", "x");
    writeFile("main.synctex.gz", "x");
    writeFile(".maketex.db", "x");

    auto plan = computeCleanPlan(std::nullopt, m_texFile, m_dir, "main",
        /*all=*/true, { });

    EXPECT_TRUE(removes(plan, m_dir / "main.pdf"));
    EXPECT_TRUE(removes(plan, m_dir / "main.synctex.gz"));
    EXPECT_TRUE(removes(plan, m_dir / ".maketex.db"));
    EXPECT_FALSE(removes(plan, m_texFile));
}

TEST_F(CleanPlanTest, onlyExistingFilesArePlanned)
{
    writeFile("main.aux", "x");

    auto plan = computeCleanPlan(std::nullopt, m_texFile, m_dir, "main",
        /*all=*/false, { });

    ASSERT_EQ(plan.remove.size(), 1u);
    EXPECT_EQ(plan.remove[0], m_dir / "main.aux");
}

TEST_F(CleanPlanTest, generatedFilesFromFlsAreIncluded)
{
    // A generated file the static extension list can't know about.
    writeFile("main-figure0.md5", "x");
    BuildFileSets sets;
    sets.generated = { m_dir / "main-figure0.md5" };

    auto plan = computeCleanPlan(sets, m_texFile, m_dir, "main",
        /*all=*/false, { });

    EXPECT_TRUE(removes(plan, m_dir / "main-figure0.md5"));
}

TEST_F(CleanPlanTest, generatedOutsideOutputDirIsSkipped)
{
    fs::path outside = maketex::test_utils::uniqueTempDir("maketex_cleanplan_outside.aux");
    std::ofstream(outside) << "x";
    BuildFileSets sets;
    sets.generated = { outside };

    auto plan = computeCleanPlan(sets, m_texFile, m_dir, "main",
        /*all=*/false, { });

    EXPECT_FALSE(removes(plan, outside));
    ASSERT_EQ(plan.skippedOutside.size(), 1u);
    EXPECT_EQ(plan.skippedOutside[0], outside);
    fs::remove(outside);
}

TEST_F(CleanPlanTest, texFileIsNeverRemovedEvenIfRecordedAsGenerated)
{
    BuildFileSets sets;
    sets.generated = { m_texFile };

    auto plan = computeCleanPlan(sets, m_texFile, m_dir, "main",
        /*all=*/true, { });

    EXPECT_FALSE(removes(plan, m_texFile));
}

TEST_F(CleanPlanTest, extraExtensionsAreHonored)
{
    writeFile("main.nav", "x");

    auto noExtras = computeCleanPlan(std::nullopt, m_texFile, m_dir, "main",
        /*all=*/false, { });
    // .nav is already in the static list; use one that is not.
    writeFile("main.tikz-cache", "x");
    auto plan = computeCleanPlan(std::nullopt, m_texFile, m_dir, "main",
        /*all=*/false, { ".tikz-cache" });

    EXPECT_TRUE(removes(noExtras, m_dir / "main.nav"));
    EXPECT_TRUE(removes(plan, m_dir / "main.tikz-cache"));
}

TEST_F(CleanPlanTest, pythontexDirectoryIsACandidate)
{
    fs::create_directories(m_dir / "pythontex-files-main");

    auto plan = computeCleanPlan(std::nullopt, m_texFile, m_dir, "main",
        /*all=*/false, { });

    EXPECT_TRUE(removes(plan, m_dir / "pythontex-files-main"));
}

TEST_F(CleanPlanTest, publishedArtifactsAreProtectedWithoutAll)
{
    fs::path outDir = m_dir / ".maketex";
    fs::create_directories(outDir);
    std::ofstream(outDir / "main.aux") << "x";
    auto publishedPdf = writeFile("main.pdf", "x");

    auto plan = computeCleanPlan(std::nullopt, m_texFile, outDir, "main",
        /*all=*/false, { }, { publishedPdf });

    EXPECT_TRUE(removes(plan, outDir / "main.aux"));
    EXPECT_FALSE(removes(plan, publishedPdf));
}

TEST_F(CleanPlanTest, allRemovesPublishedArtifactsAndLegacyDb)
{
    fs::path outDir = m_dir / ".maketex";
    fs::create_directories(outDir);
    auto publishedPdf = writeFile("main.pdf", "x");
    auto legacyDb = writeFile(".maketex.db", "x");

    auto plan = computeCleanPlan(std::nullopt, m_texFile, outDir, "main",
        /*all=*/true, { }, { publishedPdf, legacyDb });

    EXPECT_TRUE(removes(plan, publishedPdf));
    EXPECT_TRUE(removes(plan, legacyDb));
}

TEST_F(CleanPlanTest, publishedListNeverRemovesTheTexFile)
{
    fs::path outDir = m_dir / ".maketex";
    fs::create_directories(outDir);

    // Defensive: even if a caller mistakenly lists the tex file as a
    // published artifact, the plan must not delete it.
    auto plan = computeCleanPlan(std::nullopt, m_texFile, outDir, "main",
        /*all=*/true, { }, { m_texFile });

    EXPECT_FALSE(removes(plan, m_texFile));
}
