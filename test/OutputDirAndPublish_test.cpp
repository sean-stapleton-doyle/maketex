#include "maketex/build/BuildFileSets.h"
#include "maketex/build/BuildLock.h"
#include "maketex/build/EngineMemory.h"
#include "maketex/build/OutputDir.h"
#include "maketex/build/Publish.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using maketex::BuildOptions;
using maketex::EngineKind;
using maketex::FlsRecord;
using maketex::StateDatabase;
using maketex::build::BuildLock;
using maketex::build::canReusePublished;
using maketex::build::ignoreInVersionControl;
using maketex::build::isUnderRoot;
using maketex::build::loadDetectedEngine;
using maketex::build::mirrorSourceDirectories;
using maketex::build::publishedStamp;
using maketex::build::publishResultArtifacts;
using maketex::build::saveDetectedEngine;
using maketex::build::shouldMirrorForFile;
using maketex::build::shouldSkipMirrorDir;
using maketex::build::sourcesFromFls;

namespace {

class OutputDirAndPublishTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_outdir_publish_test"; }
};

std::string slurp(const fs::path& p)
{
    std::ostringstream s;
    s << std::ifstream(p).rdbuf();
    return s.str();
}

} // namespace

TEST(OutputDirPredicatesTest, mirrorFileExtensionsAndSkipDirs)
{
    for (const char* name : {
             "a.tex",
             "a.eps",
             "a.ps",
             "a.svg",
             "a.pdf",
             "a.png",
             "a.jpg",
             "a.jpeg",
             "a.tikz",
             "a.pgf",
             "a.bib",
         }) {
        EXPECT_TRUE(shouldMirrorForFile(name)) << name;
    }
    for (const char* name : { "a.aux", "a.log", "a.txt", "a.cls", "README" })
        EXPECT_FALSE(shouldMirrorForFile(name)) << name;

    EXPECT_TRUE(shouldSkipMirrorDir(".git"));
    EXPECT_TRUE(shouldSkipMirrorDir(".maketex"));
    EXPECT_TRUE(shouldSkipMirrorDir("node_modules"));
    EXPECT_TRUE(shouldSkipMirrorDir("__pycache__"));
    EXPECT_FALSE(shouldSkipMirrorDir("chapters"));
    EXPECT_FALSE(shouldSkipMirrorDir("figures"));
}

TEST_F(OutputDirAndPublishTest, mirrorSourceDirectoriesAndGitignore)
{
    fs::path outDir = m_dir / ".maketex";
    fs::create_directories(outDir);
    fs::create_directories(m_dir / "chapters" / "sub");
    fs::create_directories(m_dir / "notes");
    fs::create_directories(m_dir / ".hidden");
    fs::create_directories(m_dir / "node_modules" / "pkg");

    writeFile("chapters/sub/one.tex", "hello");
    writeFile("notes/readme.txt", "ignored");
    writeFile(".hidden/secret.tex", "skip");
    writeFile("node_modules/pkg/a.tex", "skip");

    mirrorSourceDirectories(m_dir, outDir);

    EXPECT_TRUE(fs::is_directory(outDir / "chapters" / "sub"));
    EXPECT_FALSE(fs::exists(outDir / "notes"));
    EXPECT_FALSE(fs::exists(outDir / ".hidden"));
    EXPECT_FALSE(fs::exists(outDir / "node_modules"));

    // Same directory is a no-op.
    mirrorSourceDirectories(m_dir, m_dir);

    ignoreInVersionControl(outDir);
    fs::path gitignore = outDir / ".gitignore";
    ASSERT_TRUE(fs::is_regular_file(gitignore));
    EXPECT_NE(slurp(gitignore).find("*\n"), std::string::npos);

    // Existing .gitignore is preserved untouched.
    std::ofstream(gitignore) << "custom\n";
    ignoreInVersionControl(outDir);
    EXPECT_EQ(slurp(gitignore), "custom\n");
}

TEST(PublishPureTest, canReusePublishedChecksAllConditions)
{
    EXPECT_TRUE(canReusePublished(true, std::optional<std::string>("stamp-1"), "stamp-1"));
    EXPECT_FALSE(canReusePublished(false, std::optional<std::string>("stamp-1"), "stamp-1"));
    EXPECT_FALSE(canReusePublished(true, std::nullopt, "stamp-1"));
    EXPECT_FALSE(canReusePublished(true, std::optional<std::string>(""), ""));
    EXPECT_FALSE(canReusePublished(true, std::optional<std::string>("stamp-1"), "stamp-2"));
}

TEST_F(OutputDirAndPublishTest, publishResultArtifactsReusesOrRepublishes)
{
    fs::path outDir = m_dir / ".maketex";
    fs::create_directories(outDir);
    std::ofstream(outDir / "main.pdf") << "pdf-v1";
    std::ofstream(outDir / "main.synctex.gz") << "sync-v1";

    // Missing dest copy -> empty stamp.
    EXPECT_TRUE(publishedStamp(outDir, m_dir, "main").empty());

    StateDatabase stateDb(maketex::test_utils::makeMemoryDb());
    auto first = publishResultArtifacts(false, outDir, m_dir, "main", stateDb);
    ASSERT_EQ(first.size(), 2u);
    EXPECT_TRUE(stateDb.getJobMeta("main", "published_stamp").has_value());
    EXPECT_FALSE(publishedStamp(outDir, m_dir, "main").empty());

    // Up-to-date run with matching stamp reuses existing published artifacts.
    auto reused = publishResultArtifacts(true, outDir, m_dir, "main", stateDb);
    ASSERT_EQ(reused.size(), 2u);
    EXPECT_EQ(reused[0], m_dir / "main.pdf");
    EXPECT_EQ(reused[1], m_dir / "main.synctex.gz");

    // User deletes published PDF: up-to-date stamp check misses and republishes.
    fs::remove(m_dir / "main.pdf");
    auto republished = publishResultArtifacts(true, outDir, m_dir, "main", stateDb);
    ASSERT_EQ(republished.size(), 2u);
    EXPECT_EQ(slurp(m_dir / "main.pdf"), "pdf-v1");
}

TEST(BuildFileSetsPureTest, isUnderRootAndSourcesFromFls)
{
    EXPECT_TRUE(isUnderRoot("/project/docs/a.tex", "/project/docs"));
    EXPECT_TRUE(isUnderRoot("/project/docs", "/project/docs"));
    EXPECT_FALSE(isUnderRoot("/project/docs2/a.tex", "/project/docs"));
    EXPECT_FALSE(isUnderRoot("/other/a.tex", "/project/docs"));

    FlsRecord record {
        .pwd = "/project",
        .inputs = {
            "/project/main.tex",
            "/project/chap.tex",
            "/project/main.aux",
            "/project/chap.tex",
        },
        .outputs = {
            "/project/main.aux",
            "/project/main.pdf",
        },
    };
    auto sources = sourcesFromFls(record);
    EXPECT_EQ(sources,
        (std::vector<fs::path> { "/project/main.tex", "/project/chap.tex" }));
}

TEST_F(OutputDirAndPublishTest, engineMemoryAndBuildLockRoundTrip)
{
    fs::path outDir = m_dir / ".maketex";
    fs::create_directories(outDir);
    BuildOptions opts { .texFile = m_dir / "main.tex" };

    EXPECT_FALSE(loadDetectedEngine(opts).has_value());
    saveDetectedEngine(opts, EngineKind::LuaLaTeX);
    EXPECT_EQ(loadDetectedEngine(opts), EngineKind::LuaLaTeX);

    saveDetectedEngine(opts, EngineKind::XeLaTeX);
    EXPECT_EQ(loadDetectedEngine(opts), EngineKind::XeLaTeX);

    // Lock acquisition on valid path and non-fatal handling on unwritable path.
    {
        BuildLock lock(outDir / ".main.lock");
        EXPECT_TRUE(fs::exists(outDir / ".main.lock"));
    }
    BuildLock missingDirLock(m_dir / "no_such_dir" / ".main.lock");
}
