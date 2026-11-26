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

TEST(ParseConfig, hugeMaxPassesFailsInsteadOfWrapping)
{
    // 2^32 + 1 used to narrow to 1.
    EXPECT_FALSE(parseConfig("max-passes = 4294967297\n", kSource).has_value());
}

TEST(ParseConfig, timeoutIsRead)
{
    auto config = parseConfig("timeout = 120\n", kSource);
    ASSERT_TRUE(config.has_value()) << config.error();
    EXPECT_EQ(config->timeoutSeconds, 120);
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

TEST_F(FindConfigFileTest, stopsAtTheRepositoryRoot)
{
    // A config above the repository belongs to some other project.
    fs::create_directories(m_dir / "repo" / ".git");
    fs::create_directories(m_dir / "repo" / "paper");
    std::ofstream(m_dir / "maketex.toml") << "";

    EXPECT_FALSE(findConfigFile(m_dir / "repo" / "paper").has_value());
}

TEST_F(FindConfigFileTest, neverReadsTheHomeDirectory)
{
    fs::create_directories(m_dir / "home" / "paper");
    std::ofstream(m_dir / "home" / "maketex.toml") << "main = \"other.tex\"\n";
    const char* savedHome = std::getenv("HOME");
    std::string saved = savedHome ? savedHome : "";
    ::setenv("HOME", (m_dir / "home").c_str(), 1);

    auto found = findConfigFile(m_dir / "home" / "paper");
    ::setenv("HOME", saved.c_str(), 1);

    EXPECT_FALSE(found.has_value());
}

TEST(ParseConfig, parsesCustomToolsSearchPathsEnvContentFiltersAndDiagnostics)
{
    auto config = parseConfig(R"(
jobname = "paper"
preamble-cache = true
texinputs = ["styles//", "../common"]
bibinputs = ["../bib"]
bstinputs = ["styles"]
indexstyle = ["styles"]

[env]
FOO = "bar"

[content-filters]
".aux" = "^\\\\myStamp\\{"
eps = "^%%CreationDate: "

[diagnostics]
warnings-as-errors = true
fail-on = ["undefined-citation", "undefined-reference"]
ignore = ["overfull-hbox", "underfull-hbox"]

[[tool]]
name = "nomencl"
command = ["makeindex", "$job.nlo", "-s", "nomencl.ist", "-o", "$job.nls"]
triggers = ["nlo"]
outputs = [".nls"]
working-dir = "output"

[[tool]]
name = "custom-src"
command = ["mytool", "$job"]
triggers = [".raw"]
working-dir = "source"
)",
        kSource);

    ASSERT_TRUE(config.has_value()) << config.error();
    EXPECT_TRUE(config->warnings.empty());
    EXPECT_EQ(config->jobName, "paper");
    EXPECT_EQ(config->preambleCache, true);
    EXPECT_EQ(config->searchPaths.texInputs,
        (std::vector<std::string> { "/project/styles//", "/common" }));
    EXPECT_EQ(config->searchPaths.bibInputs, (std::vector<std::string> { "/bib" }));
    EXPECT_EQ(config->searchPaths.bstInputs, (std::vector<std::string> { "/project/styles" }));
    EXPECT_EQ(config->searchPaths.indexStyle, (std::vector<std::string> { "/project/styles" }));
    ASSERT_EQ(config->extraEnv.size(), 1u);
    EXPECT_EQ(config->extraEnv[0], (std::pair<std::string, std::string> { "FOO", "bar" }));
    EXPECT_EQ(config->contentFilters.size(), 2u);
    EXPECT_EQ(config->warningsAsErrors, true);
    EXPECT_EQ(config->failOn,
        (std::vector<std::string> { "undefined-citation", "undefined-reference" }));
    EXPECT_EQ(config->ignoreWarnings,
        (std::vector<std::string> { "overfull-hbox", "underfull-hbox" }));

    ASSERT_EQ(config->customTools.size(), 2u);
    EXPECT_EQ(config->customTools[0].name, "nomencl");
    EXPECT_EQ(config->customTools[0].triggerExtensions, (std::vector<std::string> { ".nlo" }));
    EXPECT_EQ(config->customTools[0].outputExtensions, (std::vector<std::string> { ".nls" }));
    EXPECT_EQ(config->customTools[0].workingDir, maketex::ToolWorkingDir::Output);
    EXPECT_EQ(config->customTools[1].workingDir, maketex::ToolWorkingDir::Source);
    EXPECT_TRUE(config->customTools[1].outputExtensions.empty());
}

TEST(ParseConfig, rejectsInvalidCustomToolRegexAndJobname)
{
    EXPECT_FALSE(parseConfig("jobname = \"\"\n", kSource).has_value());
    EXPECT_FALSE(parseConfig("[content-filters]\naux = \"[unclosed\"\n", kSource).has_value());
    EXPECT_FALSE(parseConfig("[env]\nNUM = 42\n", kSource).has_value());
    EXPECT_FALSE(parseConfig("[[tool]]\nname = \"x\"\n", kSource).has_value());
    EXPECT_FALSE(parseConfig(
        "[[tool]]\nname = \"x\"\ncommand = [\"echo\"]\ntriggers = [\".in\"]\nworking-dir = \"bad\"\n",
        kSource)
            .has_value());
}
