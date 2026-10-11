#include "EffectiveOptions.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <expected>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using maketex::EngineKind;
using maketex::ToolKind;
using maketex::cli::assembleOptions;
using maketex::cli::CliArgs;
using maketex::cli::Command;
using maketex::cli::Config;

namespace {

class EffectiveOptionsTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_effopts_test"; }

    void SetUp() override
    {
        TempDirFixture::SetUp();
        m_texFile = writeFile("main.tex", "\\documentclass{article}");
    }

    CliArgs argsWithFile() const
    {
        CliArgs args;
        args.texFile = m_texFile;
        return args;
    }

    fs::path m_texFile;
};

} // namespace

TEST_F(EffectiveOptionsTest, defaultsHoldWithoutFlagsOrConfig)
{
    auto options = assembleOptions(argsWithFile(), std::nullopt);
    ASSERT_TRUE(options.has_value()) << options.error();
    EXPECT_EQ(options->build.texFile, fs::absolute(m_texFile));
    EXPECT_FALSE(options->build.engine.has_value()); // detected at build time
    EXPECT_TRUE(options->build.haltOnError);
    EXPECT_FALSE(options->build.shellEscape);
    EXPECT_TRUE(options->build.synctex);
    EXPECT_EQ(options->build.maxPasses, maketex::BuildDriver::kDefaultMaxPasses);
    EXPECT_EQ(options->extras.watchIntervalMs, 300);
}

TEST_F(EffectiveOptionsTest, configBeatsDefaults)
{
    Config config;
    config.engine = EngineKind::XeLaTeX;
    config.synctex = true;
    config.maxPasses = 4;
    config.watchIntervalMs = 100;
    config.tools = std::vector<ToolKind> { ToolKind::Biber };

    auto options = assembleOptions(argsWithFile(), config);
    ASSERT_TRUE(options.has_value()) << options.error();
    EXPECT_EQ(options->build.engine, EngineKind::XeLaTeX);
    EXPECT_TRUE(options->build.synctex);
    EXPECT_EQ(options->build.maxPasses, 4);
    EXPECT_EQ(options->extras.watchIntervalMs, 100);
    EXPECT_EQ(options->build.tools, (std::vector<ToolKind> { ToolKind::Biber }));
}

TEST_F(EffectiveOptionsTest, flagsBeatConfig)
{
    Config config;
    config.engine = EngineKind::XeLaTeX;
    config.maxPasses = 4;
    config.haltOnError = true;
    config.watchIntervalMs = 100;

    CliArgs args = argsWithFile();
    args.engine = "lualatex";
    args.maxPasses = 2;
    args.haltOnError = false;
    args.intervalMs = 50;
    args.tools = "makeindex";

    auto options = assembleOptions(args, config);
    ASSERT_TRUE(options.has_value()) << options.error();
    EXPECT_EQ(options->build.engine, EngineKind::LuaLaTeX);
    EXPECT_EQ(options->build.maxPasses, 2);
    EXPECT_FALSE(options->build.haltOnError);
    EXPECT_EQ(options->extras.watchIntervalMs, 50);
    EXPECT_EQ(options->build.tools,
        (std::vector<ToolKind> { ToolKind::Makeindex }));
}

TEST_F(EffectiveOptionsTest, configMainUsedWithoutPositional)
{
    Config config;
    config.main = m_texFile;

    auto options = assembleOptions(CliArgs { }, config);
    ASSERT_TRUE(options.has_value()) << options.error();
    EXPECT_EQ(options->build.texFile, m_texFile);
}

TEST_F(EffectiveOptionsTest, positionalBeatsConfigMain)
{
    auto other = writeFile("other.tex", "x");
    Config config;
    config.main = other;

    auto options = assembleOptions(argsWithFile(), config);
    ASSERT_TRUE(options.has_value()) << options.error();
    EXPECT_EQ(options->build.texFile, fs::absolute(m_texFile));
}

TEST_F(EffectiveOptionsTest, nonexistentFileIsAnError)
{
    CliArgs args;
    args.texFile = m_dir / "ghost.tex";
    auto options = assembleOptions(args, std::nullopt);
    ASSERT_FALSE(options.has_value());
    EXPECT_NE(options.error().find("not found"), std::string::npos);
}

TEST_F(EffectiveOptionsTest, unknownEngineIsAnError)
{
    CliArgs args = argsWithFile();
    args.engine = "tectonic";
    auto options = assembleOptions(args, std::nullopt);
    ASSERT_FALSE(options.has_value());
    EXPECT_NE(options.error().find("tectonic"), std::string::npos);
}

TEST_F(EffectiveOptionsTest, unknownToolIsAnError)
{
    CliArgs args = argsWithFile();
    args.tools = "biber,frobnicate";
    auto options = assembleOptions(args, std::nullopt);
    ASSERT_FALSE(options.has_value());
    EXPECT_NE(options.error().find("frobnicate"), std::string::npos);
}

TEST_F(EffectiveOptionsTest, outputDirCreatedForBuildButNotClean)
{
    CliArgs args = argsWithFile();
    args.outputDir = m_dir / "out";

    auto build = assembleOptions(args, std::nullopt);
    ASSERT_TRUE(build.has_value()) << build.error();
    EXPECT_TRUE(fs::is_directory(m_dir / "out"));

    fs::remove_all(m_dir / "out");
    args.command = Command::Clean;
    auto clean = assembleOptions(args, std::nullopt);
    ASSERT_TRUE(clean.has_value()) << clean.error();
    EXPECT_FALSE(fs::exists(m_dir / "out"));
}

TEST_F(EffectiveOptionsTest, extrasCarriedThrough)
{
    Config config;
    config.watchExtraPaths = { m_dir / "figures" };
    config.cleanExtraExtensions = { ".nav" };

    CliArgs args = argsWithFile();
    args.command = Command::Clean;
    args.verbose = true;
    args.plain = true;
    args.cleanAll = true;
    args.dryRun = true;
    args.noTui = true;

    auto options = assembleOptions(args, config);
    ASSERT_TRUE(options.has_value()) << options.error();
    EXPECT_TRUE(options->extras.verbose);
    EXPECT_TRUE(options->extras.plain);
    EXPECT_TRUE(options->extras.cleanAll);
    EXPECT_TRUE(options->extras.dryRun);
    EXPECT_TRUE(options->extras.noTui);
    EXPECT_EQ(options->extras.watchExtraPaths,
        (std::vector<fs::path> { m_dir / "figures" }));
    EXPECT_EQ(options->extras.cleanExtraExtensions,
        (std::vector<std::string> { ".nav" }));
}

TEST_F(EffectiveOptionsTest, formatDefaultsToHuman)
{
    auto options = assembleOptions(argsWithFile(), std::nullopt);
    ASSERT_TRUE(options.has_value()) << options.error();
    EXPECT_EQ(options->extras.format, maketex::cli::OutputFormat::Human);
}

TEST_F(EffectiveOptionsTest, formatParsesJsonAndGithub)
{
    auto args = argsWithFile();
    args.format = "json";
    auto options = assembleOptions(args, std::nullopt);
    ASSERT_TRUE(options.has_value()) << options.error();
    EXPECT_EQ(options->extras.format, maketex::cli::OutputFormat::Json);

    args.format = "github";
    options = assembleOptions(args, std::nullopt);
    ASSERT_TRUE(options.has_value()) << options.error();
    EXPECT_EQ(options->extras.format, maketex::cli::OutputFormat::Github);
}

TEST_F(EffectiveOptionsTest, unknownFormatIsRejected)
{
    auto args = argsWithFile();
    args.format = "yaml";
    auto options = assembleOptions(args, std::nullopt);
    ASSERT_FALSE(options.has_value());
    EXPECT_NE(options.error().find("unknown format"), std::string::npos);
}

// ── Document discovery ───────────────────────────────────────────────────────

namespace {

class DocumentDiscoveryTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_discovery_test"; }

    void SetUp() override
    {
        TempDirFixture::SetUp();
        m_previousCwd = fs::current_path();
        fs::current_path(m_dir);
    }

    void TearDown() override
    {
        fs::current_path(m_previousCwd);
        TempDirFixture::TearDown();
    }

    std::expected<fs::path, std::string> resolve(CliArgs args = { }) const
    {
        auto options = assembleOptions(args, std::nullopt);
        if (!options)
            return std::unexpected(options.error());
        return options->build.texFile;
    }

    fs::path m_previousCwd;
};

} // namespace

TEST_F(DocumentDiscoveryTest, singleDocumentInCwdIsBuilt)
{
    writeFile("paper.tex", "% a header comment\n\\documentclass{article}\n");
    writeFile("chapter.tex", "\\section{Not a root}\n");
    auto found = resolve();
    ASSERT_TRUE(found.has_value()) << found.error();
    EXPECT_EQ(found->filename(), "paper.tex");
}

TEST_F(DocumentDiscoveryTest, mainTexBreaksATie)
{
    writeFile("main.tex", "\\documentclass{article}\n");
    writeFile("notes.tex", "\\documentclass{article}\n");
    auto found = resolve();
    ASSERT_TRUE(found.has_value()) << found.error();
    EXPECT_EQ(found->filename(), "main.tex");
}

TEST_F(DocumentDiscoveryTest, ambiguousDocumentsAreListed)
{
    writeFile("a.tex", "\\documentclass{article}\n");
    writeFile("b.tex", "\\documentclass{article}\n");
    auto found = resolve();
    ASSERT_FALSE(found.has_value());
    EXPECT_NE(found.error().find("a.tex, b.tex"), std::string::npos) << found.error();
}

TEST_F(DocumentDiscoveryTest, emptyDirectoryIsAnError)
{
    auto found = resolve();
    ASSERT_FALSE(found.has_value());
    EXPECT_NE(found.error().find("no LaTeX document"), std::string::npos) << found.error();
}

TEST_F(DocumentDiscoveryTest, commentedDocumentclassDoesNotCount)
{
    writeFile("draft.tex", "% \\documentclass{article}\n");
    EXPECT_FALSE(resolve().has_value());
}

TEST_F(DocumentDiscoveryTest, extensionIsOptional)
{
    writeFile("thesis.tex", "\\documentclass{book}\n");
    CliArgs args;
    args.texFile = "thesis";
    auto found = resolve(args);
    ASSERT_TRUE(found.has_value()) << found.error();
    EXPECT_EQ(found->filename(), "thesis.tex");
}

TEST_F(DocumentDiscoveryTest, texRootCommentRedirectsToTheRoot)
{
    // What an editor does when the user builds from a chapter.
    writeFile("thesis.tex", "\\documentclass{book}\n\\include{chapters/one}\n");
    fs::create_directories(m_dir / "chapters");
    writeFile("chapters/one.tex", "% !TEX root = ../thesis.tex\n\\chapter{One}\n");
    CliArgs args;
    args.texFile = "chapters/one.tex";
    auto found = resolve(args);
    ASSERT_TRUE(found.has_value()) << found.error();
    EXPECT_EQ(fs::weakly_canonical(*found), fs::weakly_canonical(m_dir / "thesis.tex"));
}

// ── install-missing resolution ───────────────────────────────────────────────

TEST_F(EffectiveOptionsTest, installMissingIsOnByDefaultButNotRequested)
{
    auto options = assembleOptions(argsWithFile(), std::nullopt);
    ASSERT_TRUE(options.has_value()) << options.error();
    EXPECT_TRUE(options->extras.installMissing);
    // Not requested: sudo installs are confirmed, doctor doesn't install.
    EXPECT_FALSE(options->extras.installMissingRequested);
}

TEST_F(EffectiveOptionsTest, installMissingFlagOrConfigIsARequest)
{
    auto args = argsWithFile();
    args.installMissing = true;
    auto fromFlag = assembleOptions(args, std::nullopt);
    ASSERT_TRUE(fromFlag.has_value());
    EXPECT_TRUE(fromFlag->extras.installMissingRequested);

    Config config;
    config.installMissing = true;
    auto fromConfig = assembleOptions(argsWithFile(), config);
    ASSERT_TRUE(fromConfig.has_value());
    EXPECT_TRUE(fromConfig->extras.installMissingRequested);
}

TEST_F(EffectiveOptionsTest, noInstallMissingTurnsItOff)
{
    auto args = argsWithFile();
    args.installMissing = false;
    Config config;
    config.installMissing = true; // the flag wins over the config
    auto options = assembleOptions(args, config);
    ASSERT_TRUE(options.has_value());
    EXPECT_FALSE(options->extras.installMissing);
    EXPECT_FALSE(options->extras.installMissingRequested);
}
