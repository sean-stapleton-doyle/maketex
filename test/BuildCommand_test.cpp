#include "commands/BuildCommand.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using maketex::BuildOptions;
using maketex::BuildResult;
using maketex::Diagnostic;
using maketex::DriverResult;
using maketex::EngineKind;
using maketex::cli::EffectiveOptions;
using maketex::cli::IBuildEnv;
using maketex::cli::InstallPolicy;
using maketex::cli::kExitBuildFailed;
using maketex::cli::kExitEnvironment;
using maketex::cli::kExitSuccess;
using maketex::cli::OutputFormat;
using maketex::cli::runBuildCommand;

namespace {

BuildResult makeResult(DriverResult driver, EngineKind engine = EngineKind::PdfLaTeX)
{
    BuildResult result;
    static_cast<DriverResult&>(result) = std::move(driver);
    result.engine = engine;
    return result;
}

class FakeBuildEnv : public IBuildEnv {
public:
    BuildResult nextResult = makeResult(DriverResult { .success = true, .passes = 1 });
    std::optional<std::string> throwError;
    bool stdoutTty = false;
    bool interactiveTty = false;
    bool noColor = false;
    bool confirmAnswer = true;
    std::string hintToInject;

    int runBuildCalls = 0;
    int installBuildCalls = 0;
    int addHintsCalls = 0;
    std::vector<std::string> confirmedQuestions;
    std::optional<InstallPolicy> capturedPolicy;
    std::ostringstream outStream;
    std::ostringstream errStream;

    BuildResult runBuild(const BuildOptions&) override
    {
        ++runBuildCalls;
        if (throwError)
            throw std::runtime_error(*throwError);
        return nextResult;
    }

    BuildResult buildInstallingMissing(const BuildOptions&,
        const InstallPolicy& policy, std::ostream& log) override
    {
        ++installBuildCalls;
        capturedPolicy = policy;
        if (policy.confirm)
            (void)policy.confirm("Install package foo?");
        log << "install log line\n";
        if (throwError)
            throw std::runtime_error(*throwError);
        return nextResult;
    }

    void addInstallHints(std::vector<Diagnostic>& diagnostics) override
    {
        ++addHintsCalls;
        if (!hintToInject.empty())
            for (auto& d : diagnostics)
                if (d.hint.empty())
                    d.hint = hintToInject;
    }

    bool confirm(const std::string& question) override
    {
        confirmedQuestions.push_back(question);
        return confirmAnswer;
    }

    bool isStdoutTty() const override { return stdoutTty; }
    bool isInteractiveTty() const override { return interactiveTty; }
    bool hasNoColor() const override { return noColor; }
    std::ostream& out() override { return outStream; }
    std::ostream& err() override { return errStream; }
};

EffectiveOptions baseOptions()
{
    EffectiveOptions opts;
    opts.build.texFile = "/project/paper.tex";
    opts.extras.installMissing = false;
    opts.extras.format = OutputFormat::Human;
    return opts;
}

} // namespace

TEST(BuildCommandTest, returnsZeroAndPrintsHumanSummaryOnSuccess)
{
    FakeBuildEnv env;
    auto opts = baseOptions();

    int code = runBuildCommand(opts, env);

    EXPECT_EQ(code, kExitSuccess);
    EXPECT_EQ(env.runBuildCalls, 1);
    EXPECT_EQ(env.installBuildCalls, 0);
    EXPECT_EQ(env.addHintsCalls, 1);
    EXPECT_EQ(env.outStream.str(), "maketex: done in 1 pass\n");
    EXPECT_TRUE(env.errStream.str().empty());
}

TEST(BuildCommandTest, returnsBuildFailedOnCompileError)
{
    FakeBuildEnv env;
    env.nextResult = makeResult(DriverResult {
        .success = false,
        .passes = 1,
        .lastError = "pdflatex exited with code 1",
        .diagnostics = { Diagnostic {
            .severity = Diagnostic::Severity::Error,
            .file = "paper.tex",
            .line = 10,
            .message = "Undefined control sequence",
        } },
    });
    env.hintToInject = "run tlmgr install foo";
    auto opts = baseOptions();

    int code = runBuildCommand(opts, env);

    EXPECT_EQ(code, kExitBuildFailed);
    EXPECT_NE(env.errStream.str().find("Undefined control sequence"), std::string::npos);
    EXPECT_NE(env.errStream.str().find("hint: run tlmgr install foo"), std::string::npos);
    EXPECT_NE(env.errStream.str().find("maketex: failed after 1 pass"), std::string::npos);
}

TEST(BuildCommandTest, returnsEnvironmentErrorWhenDiagnosticReportsMissingTool)
{
    FakeBuildEnv env;
    env.nextResult = makeResult(DriverResult {
        .success = false,
        .passes = 0,
        .lastError = "biber not found",
        .diagnostics = { Diagnostic {
            .severity = Diagnostic::Severity::Error,
            .message = "biber not found",
            .category = "missing-tool",
            .missingFile = "biber",
        } },
    });
    auto opts = baseOptions();

    EXPECT_EQ(runBuildCommand(opts, env), kExitEnvironment);
}

TEST(BuildCommandTest, catchesExceptionAndEmitsSingleValidJsonReport)
{
    FakeBuildEnv env;
    env.throwError = "sqlite database is locked";
    auto opts = baseOptions();
    opts.extras.format = OutputFormat::Json;

    int code = runBuildCommand(opts, env);

    EXPECT_EQ(code, kExitEnvironment);
    auto doc = nlohmann::json::parse(env.outStream.str());
    EXPECT_FALSE(doc.at("success").get<bool>());
    ASSERT_EQ(doc.at("diagnostics").size(), 1u);
    EXPECT_EQ(doc.at("diagnostics")[0].at("category"), "environment");
    EXPECT_EQ(doc.at("diagnostics")[0].at("message"), "sqlite database is locked");
}

TEST(BuildCommandTest, dispatchesGithubFormatToOutStream)
{
    FakeBuildEnv env;
    env.nextResult = makeResult(DriverResult {
        .success = false,
        .passes = 1,
        .lastError = "compile failed",
        .diagnostics = { Diagnostic {
            .severity = Diagnostic::Severity::Error,
            .file = "paper.tex",
            .line = 5,
            .message = "Missing $ inserted",
        } },
    });
    auto opts = baseOptions();
    opts.extras.format = OutputFormat::Github;

    int code = runBuildCommand(opts, env);

    EXPECT_EQ(code, kExitBuildFailed);
    EXPECT_NE(env.outStream.str().find("::error file=paper.tex,line=5,title=LaTeX::Missing $ inserted"),
        std::string::npos);
}

TEST(BuildCommandTest, usesStyledInlineOutputOnTtyUnlessPlainOrNoColor)
{
    FakeBuildEnv env;
    env.stdoutTty = true;
    auto opts = baseOptions();

    EXPECT_EQ(runBuildCommand(opts, env), kExitSuccess);
    EXPECT_NE(env.outStream.str().find("paper.tex"), std::string::npos);
    EXPECT_EQ(env.outStream.str().find("maketex: done in"), std::string::npos);

    // NO_COLOR falls back to plain output even on a TTY.
    FakeBuildEnv noColorEnv;
    noColorEnv.stdoutTty = true;
    noColorEnv.noColor = true;
    EXPECT_EQ(runBuildCommand(opts, noColorEnv), kExitSuccess);
    EXPECT_EQ(noColorEnv.outStream.str(), "maketex: done in 1 pass\n");
}

TEST(BuildCommandTest, installMissingWiresPolicyInteractiveOnlyForHumanTty)
{
    FakeBuildEnv humanEnv;
    humanEnv.interactiveTty = true;
    auto humanOpts = baseOptions();
    humanOpts.extras.installMissing = true;
    humanOpts.extras.installMissingRequested = true;
    humanOpts.extras.format = OutputFormat::Human;

    EXPECT_EQ(runBuildCommand(humanOpts, humanEnv), kExitSuccess);
    EXPECT_EQ(humanEnv.runBuildCalls, 0);
    EXPECT_EQ(humanEnv.installBuildCalls, 1);
    ASSERT_TRUE(humanEnv.capturedPolicy.has_value());
    EXPECT_TRUE(humanEnv.capturedPolicy->interactive);
    EXPECT_TRUE(humanEnv.capturedPolicy->requested);
    ASSERT_EQ(humanEnv.confirmedQuestions.size(), 1u);
    EXPECT_EQ(humanEnv.confirmedQuestions[0], "Install package foo?");
    EXPECT_NE(humanEnv.errStream.str().find("install log line"), std::string::npos);

    // JSON output must suppress interactive prompts even when stdin/stderr are TTYs.
    FakeBuildEnv jsonEnv;
    jsonEnv.interactiveTty = true;
    auto jsonOpts = humanOpts;
    jsonOpts.extras.format = OutputFormat::Json;

    EXPECT_EQ(runBuildCommand(jsonOpts, jsonEnv), kExitSuccess);
    ASSERT_TRUE(jsonEnv.capturedPolicy.has_value());
    EXPECT_FALSE(jsonEnv.capturedPolicy->interactive);
}
