#include "EffectiveOptions.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <filesystem>

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
    EXPECT_EQ(options->build.engine, EngineKind::PdfLaTeX);
    EXPECT_TRUE(options->build.haltOnError);
    EXPECT_FALSE(options->build.shellEscape);
    EXPECT_FALSE(options->build.synctex);
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

TEST_F(EffectiveOptionsTest, missingInputFileIsAnError)
{
    auto options = assembleOptions(CliArgs { }, std::nullopt);
    ASSERT_FALSE(options.has_value());
    EXPECT_NE(options.error().find("no input file"), std::string::npos);
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
