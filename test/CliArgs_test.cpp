#include "CliArgs.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using maketex::cli::CliArgs;
using maketex::cli::Command;
using maketex::cli::normalizeArgv;
using maketex::cli::parseCliArgs;

namespace {

std::vector<std::string> args(std::initializer_list<const char*> list)
{
    return { list.begin(), list.end() };
}

} // namespace

TEST(NormalizeArgv, insertsBuildBeforeBareFile)
{
    auto result = normalizeArgv(args({ "thesis.tex" }));
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0], "build");
    EXPECT_EQ(result[1], "thesis.tex");
}

TEST(NormalizeArgv, insertsBuildBeforeFlags)
{
    auto result = normalizeArgv(args({ "--verbose", "thesis.tex" }));
    EXPECT_EQ(result[0], "build");
}

TEST(NormalizeArgv, leavesSubcommandsAlone)
{
    EXPECT_EQ(normalizeArgv(args({ "watch", "a.tex" }))[0], "watch");
    EXPECT_EQ(normalizeArgv(args({ "clean" }))[0], "clean");
    EXPECT_EQ(normalizeArgv(args({ "build", "a.tex" }))[0], "build");
}

TEST(NormalizeArgv, leavesGlobalFlagsAlone)
{
    EXPECT_EQ(normalizeArgv(args({ "--help" }))[0], "--help");
    EXPECT_EQ(normalizeArgv(args({ "--version" }))[0], "--version");
}

TEST(NormalizeArgv, emptyArgsBecomeBuild)
{
    // `maketex` alone builds the config's `main` document.
    EXPECT_EQ(normalizeArgv({ }), args({ "build" }));
}

TEST(ParseCliArgs, buildWithAllFlags)
{
    auto result = parseCliArgs(args({ "build", "main.tex",
        "--engine", "lualatex", "--engine-program", "lualatex-dev",
        "--engine-opt", "-cnf-line=x", "--engine-opt", "-y",
        "--tools", "biber,makeindex", "-o", "out",
        "--no-halt-on-error", "--shell-escape", "--synctex",
        "--max-passes", "3", "-v", "--plain", "--no-config" }));

    ASSERT_TRUE(result.args.has_value());
    const CliArgs& parsed = *result.args;
    EXPECT_EQ(parsed.command, Command::Build);
    EXPECT_EQ(parsed.texFile, "main.tex");
    EXPECT_EQ(parsed.engine, "lualatex");
    EXPECT_EQ(parsed.engineProgram, "lualatex-dev");
    EXPECT_EQ(parsed.engineOptions,
        (std::vector<std::string> { "-cnf-line=x", "-y" }));
    EXPECT_EQ(parsed.tools, "biber,makeindex");
    EXPECT_EQ(parsed.outputDir, "out");
    EXPECT_EQ(parsed.haltOnError, false);
    EXPECT_EQ(parsed.shellEscape, true);
    EXPECT_EQ(parsed.synctex, true);
    EXPECT_EQ(parsed.maxPasses, 3);
    EXPECT_TRUE(parsed.verbose);
    EXPECT_TRUE(parsed.plain);
    EXPECT_TRUE(parsed.noConfig);
}

TEST(ParseCliArgs, unspecifiedOptionsStayUnset)
{
    auto result = parseCliArgs(args({ "build", "main.tex" }));
    ASSERT_TRUE(result.args.has_value());
    const CliArgs& parsed = *result.args;
    EXPECT_FALSE(parsed.engine.has_value());
    EXPECT_FALSE(parsed.engineProgram.has_value());
    EXPECT_TRUE(parsed.engineOptions.empty());
    EXPECT_FALSE(parsed.tools.has_value());
    EXPECT_FALSE(parsed.outputDir.has_value());
    EXPECT_FALSE(parsed.haltOnError.has_value());
    EXPECT_FALSE(parsed.shellEscape.has_value());
    EXPECT_FALSE(parsed.synctex.has_value());
    EXPECT_FALSE(parsed.maxPasses.has_value());
    EXPECT_FALSE(parsed.verbose);
    EXPECT_FALSE(parsed.plain);
    EXPECT_FALSE(parsed.noConfig);
}

TEST(ParseCliArgs, watchFlags)
{
    auto result = parseCliArgs(
        args({ "watch", "main.tex", "--interval-ms", "150", "--no-tui" }));
    ASSERT_TRUE(result.args.has_value());
    EXPECT_EQ(result.args->command, Command::Watch);
    EXPECT_EQ(result.args->intervalMs, 150);
    EXPECT_TRUE(result.args->noTui);
}

TEST(ParseCliArgs, cleanFlags)
{
    auto result = parseCliArgs(args({ "clean", "main.tex", "--all", "-n" }));
    ASSERT_TRUE(result.args.has_value());
    EXPECT_EQ(result.args->command, Command::Clean);
    EXPECT_TRUE(result.args->cleanAll);
    EXPECT_TRUE(result.args->dryRun);
}

TEST(ParseCliArgs, unknownFlagFails)
{
    auto result = parseCliArgs(args({ "build", "--frobnicate" }));
    EXPECT_FALSE(result.args.has_value());
    EXPECT_NE(result.exitCode, 0);
}

TEST(ParseCliArgs, nonPositiveMaxPassesRejected)
{
    auto result = parseCliArgs(args({ "build", "a.tex", "--max-passes", "0" }));
    EXPECT_FALSE(result.args.has_value());
    EXPECT_NE(result.exitCode, 0);
}

TEST(ParseCliArgs, formatOptionOnBuild)
{
    auto result = parseCliArgs(args({ "build", "main.tex", "--format", "json" }));
    ASSERT_TRUE(result.args.has_value());
    ASSERT_TRUE(result.args->format.has_value());
    EXPECT_EQ(*result.args->format, "json");
}

TEST(ParseCliArgs, formatOptionRejectedOnWatch)
{
    // Watch is interactive; machine formats are build-only.
    auto result = parseCliArgs(args({ "watch", "main.tex", "--format", "json" }));
    EXPECT_FALSE(result.args.has_value());
    EXPECT_NE(result.exitCode, 0);
}

TEST(NormalizeArgv, helpWordShowsHelp)
{
    EXPECT_EQ(normalizeArgv(args({ "help" })), args({ "--help" }));
}

TEST(NormalizeArgv, subcommandAfterFlagsMovesToFront)
{
    EXPECT_EQ(normalizeArgv(args({ "--plain", "watch" })), args({ "watch", "--plain" }));
}

TEST(NormalizeArgv, subcommandWordAfterDocumentIsNotACommand)
{
    // The first non-flag is the document; nothing after it is re-read.
    EXPECT_EQ(normalizeArgv(args({ "paper", "watch" }))[0], "build");
}

TEST(ParseCliArgs, negatedFlagsUndoConfig)
{
    auto result = parseCliArgs(args({ "build", "--no-shell-escape", "--no-synctex",
        "--halt-on-error" }));
    ASSERT_TRUE(result.args.has_value());
    EXPECT_EQ(result.args->shellEscape, false);
    EXPECT_EQ(result.args->synctex, false);
    EXPECT_EQ(result.args->haltOnError, true);
}

TEST(ParseCliArgs, usageErrorsExitTwoAndHelpExitsZero)
{
    EXPECT_EQ(parseCliArgs(args({ "build", "--no-such-flag" })).exitCode, maketex::cli::kExitUsage);
    EXPECT_EQ(parseCliArgs(args({ "--help" })).exitCode, maketex::cli::kExitSuccess);
}

TEST(NormalizeArgv, optionValuesAreNotSubcommands)
{
    // `-o build` names an output directory, not the build command.
    EXPECT_EQ(normalizeArgv(args({ "-o", "build" })), args({ "build", "-o", "build" }));
    EXPECT_EQ(normalizeArgv(args({ "--engine-program", "watch", "doc.tex" }))[0], "build");
    // A flag without a value still lets the subcommand through.
    EXPECT_EQ(normalizeArgv(args({ "-o", "out", "--plain", "watch" }))[0], "watch");
}
