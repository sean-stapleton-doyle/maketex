#include "maketex/Build.h"
#include "maketex/build/BuildAttempts.h"

#include <gtest/gtest.h>

#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using maketex::BuildOptions;
using maketex::BuildResult;
using maketex::chooseEngine;
using maketex::Diagnostic;
using maketex::EngineChoice;
using maketex::EngineKind;
using maketex::jobNameFor;
using maketex::publishedArtifactPaths;
using maketex::version;
using maketex::build::IBuildAttempts;
using maketex::build::runBuild;

namespace {

struct RecordedAttempt {
    EngineKind engine;
    bool suppressResetNotice = false;
};

class FakeBuildAttempts final : public IBuildAttempts {
public:
    EngineChoice choice { .kind = EngineKind::PdfLaTeX, .reason = "" };
    std::optional<EngineKind> remembered;
    std::deque<BuildResult> scriptedResults;

    std::vector<RecordedAttempt> attempts;
    std::vector<EngineKind> savedEngines;
    int discardedAuxCalls = 0;

    EngineChoice chooseEngine(const BuildOptions&) override { return choice; }

    BuildResult attempt(const BuildOptions&,
        EngineKind engine,
        bool suppressResetNotice) override
    {
        attempts.push_back({ .engine = engine, .suppressResetNotice = suppressResetNotice });
        if (scriptedResults.empty()) {
            BuildResult fallback;
            fallback.success = true;
            fallback.engine = engine;
            return fallback;
        }
        auto next = std::move(scriptedResults.front());
        scriptedResults.pop_front();
        next.engine = engine;
        return next;
    }

    std::optional<EngineKind> rememberedEngine(const BuildOptions&) override
    {
        return remembered;
    }

    void rememberEngine(const BuildOptions&, EngineKind engine) override
    {
        savedEngines.push_back(engine);
    }

    void discardPartialAux(const BuildOptions&) override { ++discardedAuxCalls; }
};

BuildResult failedWithDiag(std::string message)
{
    BuildResult r;
    r.success = false;
    r.lastError = "latex exited with code 1";
    r.diagnostics.push_back(Diagnostic {
        .severity = Diagnostic::Severity::Error,
        .message = std::move(message),
    });
    return r;
}

BuildResult succeeded(int passes = 1)
{
    BuildResult r;
    r.success = true;
    r.passes = passes;
    return r;
}

BuildOptions makeOptions(std::optional<EngineKind> explicitEngine = std::nullopt)
{
    BuildOptions opts;
    opts.texFile = "/project/paper.tex";
    opts.engine = explicitEngine;
    return opts;
}

} // namespace

TEST(RunBuildTest, explicitEngineNeverUsesRememberedEngineOrRetries)
{
    FakeBuildAttempts env;
    env.choice = { .kind = EngineKind::PdfLaTeX, .reason = "" };
    env.remembered = EngineKind::LuaLaTeX;
    env.scriptedResults.push_back(failedWithDiag("fontspec requires XeTeX or LuaTeX"));

    auto res = runBuild(makeOptions(EngineKind::PdfLaTeX), env);

    EXPECT_FALSE(res.success);
    EXPECT_FALSE(res.retriedEngine);
    EXPECT_EQ(res.engine, EngineKind::PdfLaTeX);
    ASSERT_EQ(env.attempts.size(), 1u);
    EXPECT_EQ(env.attempts[0].engine, EngineKind::PdfLaTeX);
    EXPECT_FALSE(env.attempts[0].suppressResetNotice);
    EXPECT_EQ(env.discardedAuxCalls, 0);
    EXPECT_TRUE(env.savedEngines.empty());
}

TEST(RunBuildTest, detectedUnicodeEngineIsUsedDirectlyWithoutRetry)
{
    FakeBuildAttempts env;
    env.choice = { .kind = EngineKind::LuaLaTeX, .reason = "loads fontspec" };
    env.scriptedResults.push_back(failedWithDiag("fontspec requires XeTeX or LuaTeX"));

    auto res = runBuild(makeOptions(), env);

    EXPECT_FALSE(res.success);
    EXPECT_FALSE(res.retriedEngine);
    EXPECT_EQ(res.engine, EngineKind::LuaLaTeX);
    EXPECT_EQ(res.engineReason, "loads fontspec");
    ASSERT_EQ(env.attempts.size(), 1u);
    EXPECT_EQ(env.attempts[0].engine, EngineKind::LuaLaTeX);
    EXPECT_EQ(env.discardedAuxCalls, 0);
}

TEST(RunBuildTest, detectedPdfLaTeXWithExplicitReasonSkipsRememberedEngine)
{
    // e.g. `% !TeX program = pdflatex` gives a non-empty reason: the remembered
    // Unicode engine from an earlier run must not override an explicit directive.
    FakeBuildAttempts env;
    env.choice = { .kind = EngineKind::PdfLaTeX, .reason = "% !TeX program" };
    env.remembered = EngineKind::LuaLaTeX;
    env.scriptedResults.push_back(succeeded());

    auto res = runBuild(makeOptions(), env);

    EXPECT_TRUE(res.success);
    EXPECT_EQ(res.engine, EngineKind::PdfLaTeX);
    EXPECT_EQ(res.engineReason, "% !TeX program");
    ASSERT_EQ(env.attempts.size(), 1u);
    EXPECT_EQ(env.attempts[0].engine, EngineKind::PdfLaTeX);
}

TEST(RunBuildTest, rememberedEngineReusedWhenDetectionIsReasonlessPdfLaTeX)
{
    FakeBuildAttempts env;
    env.choice = { .kind = EngineKind::PdfLaTeX, .reason = "" };
    env.remembered = EngineKind::XeLaTeX;
    env.scriptedResults.push_back(succeeded());

    auto res = runBuild(makeOptions(), env);

    EXPECT_TRUE(res.success);
    EXPECT_FALSE(res.retriedEngine);
    EXPECT_EQ(res.engine, EngineKind::XeLaTeX);
    EXPECT_EQ(res.engineReason, "remembered from previous build");
    ASSERT_EQ(env.attempts.size(), 1u);
    EXPECT_EQ(env.attempts[0].engine, EngineKind::XeLaTeX);
    EXPECT_FALSE(env.attempts[0].suppressResetNotice);
}

TEST(RunBuildTest, rememberedUnicodeEngineFailureDoesNotTriggerRetry)
{
    FakeBuildAttempts env;
    env.choice = { .kind = EngineKind::PdfLaTeX, .reason = "" };
    env.remembered = EngineKind::LuaLaTeX;
    env.scriptedResults.push_back(failedWithDiag("requires XeTeX"));

    auto res = runBuild(makeOptions(), env);

    EXPECT_FALSE(res.success);
    EXPECT_FALSE(res.retriedEngine);
    ASSERT_EQ(env.attempts.size(), 1u);
    EXPECT_EQ(env.attempts[0].engine, EngineKind::LuaLaTeX);
    EXPECT_EQ(env.discardedAuxCalls, 0);
}

TEST(RunBuildTest, guessedPdfLaTeXNonUnicodeFailureDoesNotRetry)
{
    FakeBuildAttempts env;
    env.choice = { .kind = EngineKind::PdfLaTeX, .reason = "" };
    env.scriptedResults.push_back(failedWithDiag("Undefined control sequence"));

    auto res = runBuild(makeOptions(), env);

    EXPECT_FALSE(res.success);
    EXPECT_FALSE(res.retriedEngine);
    ASSERT_EQ(env.attempts.size(), 1u);
    EXPECT_EQ(env.discardedAuxCalls, 0);
    EXPECT_TRUE(env.savedEngines.empty());
}

TEST(RunBuildTest, guessedPdfLaTeXUnicodeFailureRetriesWithLuaLaTeXAndRemembersOnSuccess)
{
    FakeBuildAttempts env;
    env.choice = { .kind = EngineKind::PdfLaTeX, .reason = "" };
    env.scriptedResults.push_back(failedWithDiag("Fatal Package fontspec Error: requires either XeTeX or LuaTeX"));

    BuildResult retryResult = succeeded(2);
    retryResult.diagnostics.push_back(Diagnostic {
        .severity = Diagnostic::Severity::Warning,
        .message = "Overfull \\hbox",
    });
    env.scriptedResults.push_back(std::move(retryResult));

    auto res = runBuild(makeOptions(), env);

    EXPECT_TRUE(res.success);
    EXPECT_TRUE(res.retriedEngine);
    EXPECT_EQ(res.engine, EngineKind::LuaLaTeX);
    EXPECT_EQ(res.engineReason, "pdflatex failed: the document needs a Unicode engine");
    EXPECT_EQ(res.passes, 2);

    ASSERT_EQ(env.attempts.size(), 2u);
    EXPECT_EQ(env.attempts[0].engine, EngineKind::PdfLaTeX);
    EXPECT_FALSE(env.attempts[0].suppressResetNotice);
    EXPECT_EQ(env.attempts[1].engine, EngineKind::LuaLaTeX);
    EXPECT_TRUE(env.attempts[1].suppressResetNotice);

    EXPECT_EQ(env.discardedAuxCalls, 1);
    EXPECT_EQ(env.savedEngines, (std::vector<EngineKind> { EngineKind::LuaLaTeX }));

    ASSERT_EQ(res.diagnostics.size(), 2u);
    EXPECT_EQ(res.diagnostics[0].severity, Diagnostic::Severity::Info);
    EXPECT_NE(res.diagnostics[0].message.find("the document needs lualatex"), std::string::npos);
    EXPECT_EQ(res.diagnostics[1].message, "Overfull \\hbox");
}

TEST(RunBuildTest, retryFailureDoesNotRememberEngineButPrependsInfoDiagnostic)
{
    FakeBuildAttempts env;
    env.choice = { .kind = EngineKind::PdfLaTeX, .reason = "" };
    env.scriptedResults.push_back(failedWithDiag("This package requires XeTeX"));
    env.scriptedResults.push_back(failedWithDiag("Missing font"));

    auto res = runBuild(makeOptions(), env);

    EXPECT_FALSE(res.success);
    EXPECT_TRUE(res.retriedEngine);
    EXPECT_EQ(res.engine, EngineKind::XeLaTeX);
    ASSERT_EQ(env.attempts.size(), 2u);
    EXPECT_EQ(env.attempts[1].engine, EngineKind::XeLaTeX);
    EXPECT_TRUE(env.attempts[1].suppressResetNotice);
    EXPECT_EQ(env.discardedAuxCalls, 1);
    EXPECT_TRUE(env.savedEngines.empty());

    ASSERT_EQ(res.diagnostics.size(), 2u);
    EXPECT_EQ(res.diagnostics[0].severity, Diagnostic::Severity::Info);
    EXPECT_NE(res.diagnostics[0].message.find("the document needs xelatex"), std::string::npos);
    EXPECT_EQ(res.diagnostics[1].message, "Missing font");
}

TEST(RunBuildTest, buildMetadataHelpers)
{
    EXPECT_FALSE(version().empty());

    BuildOptions opts = makeOptions(EngineKind::XeLaTeX);
    auto choice = chooseEngine(opts);
    EXPECT_EQ(choice.kind, EngineKind::XeLaTeX);
    EXPECT_TRUE(choice.reason.empty());

    EXPECT_EQ(jobNameFor(opts), "paper");
    opts.engineArguments.extraOptions = { "-jobname=custom" };
    EXPECT_EQ(jobNameFor(opts), "custom");
    opts.jobName = "explicit";
    EXPECT_EQ(jobNameFor(opts), "explicit");

    auto published = publishedArtifactPaths(opts);
    EXPECT_EQ(published, (std::vector<std::filesystem::path> {
                             "/project/explicit.pdf",
                             "/project/explicit.synctex.gz",
                         }));
    opts.outputDir = "/project/out";
    EXPECT_TRUE(publishedArtifactPaths(opts).empty());
}
