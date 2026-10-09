#include "Config.h"

#include "test_utils.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using maketex::EngineKind;
using maketex::ToolKind;
using maketex::cli::findConfigFile;
using maketex::cli::parseConfig;

namespace {

constexpr const char* kSource = "/project/maketex.toml";

} // namespace

TEST(ParseConfig, fullSchema)
{
    auto config = parseConfig(R"(
main = "thesis.tex"
engine = "lualatex"
output-dir = "build"
tools = ["biber", "makeindex"]
shell-escape = true
synctex = true
halt-on-error = false
max-passes = 4

[engine-args]
program = "lualatex-dev"
extra-options = ["-cnf-line=max_print_line=1000"]

[watch]
interval-ms = 150
extra-paths = ["figures"]

[clean]
extra-extensions = [".nav", "snm"]
)",
        kSource);

    ASSERT_TRUE(config.has_value()) << config.error();
    EXPECT_EQ(config->main, fs::path("/project/thesis.tex"));
    EXPECT_EQ(config->engine, EngineKind::LuaLaTeX);
    EXPECT_EQ(config->outputDir, fs::path("/project/build"));
    EXPECT_EQ(config->tools,
        (std::vector<ToolKind> { ToolKind::Biber, ToolKind::Makeindex }));
    EXPECT_EQ(config->shellEscape, true);
    EXPECT_EQ(config->synctex, true);
    EXPECT_EQ(config->haltOnError, false);
    EXPECT_EQ(config->maxPasses, 4);
    EXPECT_EQ(config->engineProgram, "lualatex-dev");
    EXPECT_EQ(config->engineOptions,
        (std::vector<std::string> { "-cnf-line=max_print_line=1000" }));
    EXPECT_EQ(config->watchIntervalMs, 150);
    ASSERT_EQ(config->watchExtraPaths.size(), 1u);
    EXPECT_EQ(config->watchExtraPaths[0], fs::path("/project/figures"));
    // Extensions are normalized to a leading dot.
    EXPECT_EQ(config->cleanExtraExtensions,
        (std::vector<std::string> { ".nav", ".snm" }));
    EXPECT_TRUE(config->warnings.empty());
}

TEST(ParseConfig, emptyConfigIsValid)
{
    auto config = parseConfig("", kSource);
    ASSERT_TRUE(config.has_value());
    EXPECT_FALSE(config->main.has_value());
    EXPECT_FALSE(config->engine.has_value());
    EXPECT_TRUE(config->warnings.empty());
}

TEST(ParseConfig, absolutePathsAreKept)
{
    auto config = parseConfig(R"(main = "/elsewhere/doc.tex")", kSource);
    ASSERT_TRUE(config.has_value());
    EXPECT_EQ(config->main, fs::path("/elsewhere/doc.tex"));
}

TEST(ParseConfig, unknownKeysWarnButDoNotFail)
{
    auto config = parseConfig("typo-key = 1\n\n[watch]\nintervall = 2\n", kSource);
    ASSERT_TRUE(config.has_value());
    ASSERT_EQ(config->warnings.size(), 2u);
    EXPECT_NE(config->warnings[0].find("typo-key"), std::string::npos);
    EXPECT_NE(config->warnings[0].find("maketex.toml:1"), std::string::npos);
    EXPECT_NE(config->warnings[1].find("watch.intervall"), std::string::npos);
}

TEST(ParseConfig, unknownEngineFailsWithLine)
{
    auto config = parseConfig("engine = \"tectonic\"\n", kSource);
    ASSERT_FALSE(config.has_value());
    EXPECT_NE(config.error().find("tectonic"), std::string::npos);
    EXPECT_NE(config.error().find("maketex.toml:1"), std::string::npos);
}

TEST(ParseConfig, unknownToolFails)
{
    auto config = parseConfig("tools = [\"biber\", \"frobnicate\"]\n", kSource);
    ASSERT_FALSE(config.has_value());
    EXPECT_NE(config.error().find("frobnicate"), std::string::npos);
}

TEST(ParseConfig, wrongTypeFails)
{
    EXPECT_FALSE(parseConfig("main = 3\n", kSource).has_value());
    EXPECT_FALSE(parseConfig("tools = \"biber\"\n", kSource).has_value());
    EXPECT_FALSE(parseConfig("synctex = \"yes\"\n", kSource).has_value());
}

TEST(ParseConfig, nonPositiveMaxPassesFails)
{
    EXPECT_FALSE(parseConfig("max-passes = 0\n", kSource).has_value());
}

TEST(ParseConfig, tomlSyntaxErrorFails)
{
    auto config = parseConfig("main = \n", kSource);
    ASSERT_FALSE(config.has_value());
    EXPECT_NE(config.error().find("maketex.toml:"), std::string::npos);
}

namespace {

class FindConfigFileTest : public maketex::test_utils::TempDirFixture {
protected:
    const char* dirName() const override { return "maketex_findconfig_test"; }
};

} // namespace

TEST_F(FindConfigFileTest, walksUpToTheNearestConfig)
{
    fs::create_directories(m_dir / "a" / "b" / "c");
    std::ofstream(m_dir / "a" / "maketex.toml") << "";

    auto found = findConfigFile(m_dir / "a" / "b" / "c");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, m_dir / "a" / "maketex.toml");
}

TEST_F(FindConfigFileTest, prefersTheClosestConfig)
{
    fs::create_directories(m_dir / "a" / "b");
    std::ofstream(m_dir / "maketex.toml") << "";
    std::ofstream(m_dir / "a" / "maketex.toml") << "";

    auto found = findConfigFile(m_dir / "a" / "b");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(*found, m_dir / "a" / "maketex.toml");
}
