#include "commands/WatchSession.h"

#include "test_utils.h"

#include <ftxui/dom/node.hpp>
#include <ftxui/screen/screen.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace std::chrono_literals;
using maketex::BuildEvent;
using maketex::BuildFileSets;
using maketex::BuildOptions;
using maketex::BuildResult;
using maketex::Diagnostic;
using maketex::DriverResult;
using maketex::EngineKind;
using maketex::cli::applyBuildEvent;
using maketex::cli::applyBuildResult;
using maketex::cli::applyRebuild;
using maketex::cli::computeWatchSet;
using maketex::cli::EffectiveOptions;
using maketex::cli::IFileWatcher;
using maketex::cli::InstallPolicy;
using maketex::cli::IWatchEnv;
using maketex::cli::makeWatchExclusion;
using maketex::cli::renderWatch;
using maketex::cli::runWatchLoop;
using maketex::cli::runWatchPlain;
using maketex::cli::WatchLoopCallbacks;
using maketex::cli::WatchState;
using maketex::cli::watchStatusLine;

namespace {

BuildResult makeResult(DriverResult driver, EngineKind engine = EngineKind::PdfLaTeX)
{
    BuildResult result;
    static_cast<DriverResult&>(result) = std::move(driver);
    result.engine = engine;
    return result;
}

std::string renderToString(ftxui::Element element)
{
    auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(100),
        ftxui::Dimension::Fit(element));
    ftxui::Render(screen, element);
    return screen.ToString();
}

// Scripted fake watcher: each waitForChange call pops the next change batch,
// or requests stop on `stopSource` when the script is exhausted.
class FakeFileWatcher : public IFileWatcher {
public:
    explicit FakeFileWatcher(std::stop_source& stopSource)
        : m_stopSource(stopSource)
    {
    }

    std::deque<std::vector<fs::path>> scriptedChanges;
    std::vector<std::vector<fs::path>> recordedWatchSets;
    Exclusion lastExclusion;
    int baselineCalls = 0;
    int nudgeCalls = 0;

    void setWatchSet(std::vector<fs::path> paths, Exclusion exclude = { }) override
    {
        recordedWatchSets.push_back(std::move(paths));
        lastExclusion = std::move(exclude);
    }

    std::vector<Entry> captureBaseline() const override
    {
        return { };
    }

    std::vector<fs::path> waitForChange(std::stop_token,
        const std::optional<std::vector<Entry>>&) override
    {
        ++baselineCalls;
        if (scriptedChanges.empty()) {
            m_stopSource.request_stop();
            return { };
        }
        auto next = std::move(scriptedChanges.front());
        scriptedChanges.pop_front();
        return next;
    }

    void nudge() override { ++nudgeCalls; }

private:
    std::stop_source& m_stopSource;
};

class FakeWatchEnv : public IWatchEnv {
public:
    std::deque<BuildResult> scriptedResults;
    std::deque<std::optional<std::string>> scriptedThrows;
    std::optional<BuildFileSets> scannedFiles;
    std::vector<fs::path> published;
    bool styled = false;
    mutable std::chrono::steady_clock::time_point clockNow { };
    std::chrono::milliseconds stepPerNowCall { 25ms };

    std::vector<BuildOptions> seenBuildOptions;
    std::vector<InstallPolicy> seenInstallPolicies;
    int addHintsCalls = 0;
    std::ostringstream outStream;
    std::ostringstream errStream;

    BuildResult runBuild(const BuildOptions& options) override
    {
        seenBuildOptions.push_back(options);
        return nextBuildOutcome();
    }

    BuildResult buildInstallingMissing(const BuildOptions& options,
        const InstallPolicy& policy, std::ostream&) override
    {
        seenBuildOptions.push_back(options);
        seenInstallPolicies.push_back(policy);
        return nextBuildOutcome();
    }

    std::optional<BuildFileSets> scanBuildFiles(const BuildOptions&) override
    {
        return scannedFiles;
    }

    void addInstallHints(std::vector<Diagnostic>&) override { ++addHintsCalls; }

    std::vector<fs::path> publishedArtifactPaths(const BuildOptions&) override
    {
        return published;
    }

    bool isStyled(bool plainFlag) const override { return !plainFlag && styled; }

    std::chrono::steady_clock::time_point now() const override
    {
        auto current = clockNow;
        clockNow += stepPerNowCall;
        return current;
    }

    std::ostream& out() override { return outStream; }
    std::ostream& err() override { return errStream; }

private:
    BuildResult nextBuildOutcome()
    {
        if (!scriptedThrows.empty()) {
            auto err = scriptedThrows.front();
            scriptedThrows.pop_front();
            if (err)
                throw std::runtime_error(*err);
        }
        if (scriptedResults.empty())
            return makeResult(DriverResult { .success = true, .passes = 1 });
        auto next = std::move(scriptedResults.front());
        scriptedResults.pop_front();
        return next;
    }
};

} // namespace

// ── WatchState reducers ───────────────────────────────────────────────────────

TEST(WatchSessionTest, applyBuildEventTransitionsAllEventKinds)
{
    WatchState state;

    applyBuildEvent(state, BuildEvent { .kind = BuildEvent::Kind::PassStarted, .pass = 1 });
    EXPECT_EQ(state.currentPass, 1);
    EXPECT_TRUE(state.currentTool.empty());

    applyBuildEvent(state, BuildEvent { .kind = BuildEvent::Kind::ToolStarted, .toolName = "biber" });
    EXPECT_EQ(state.currentTool, "biber");

    applyBuildEvent(state, BuildEvent { .kind = BuildEvent::Kind::ToolFinished, .toolName = "biber" });
    EXPECT_TRUE(state.currentTool.empty());

    applyBuildEvent(state, BuildEvent { .kind = BuildEvent::Kind::RerunScheduled, .detail = "main.aux changed" });
    EXPECT_EQ(state.rerunReason, "main.aux changed");

    applyBuildEvent(state, BuildEvent { .kind = BuildEvent::Kind::PassFinished, .pass = 1 });
    applyBuildEvent(state, BuildEvent { .kind = BuildEvent::Kind::PassStarted, .pass = 2 });
    EXPECT_EQ(state.currentPass, 2);

    applyBuildEvent(state, BuildEvent { .kind = BuildEvent::Kind::SkippedUpToDate });
    EXPECT_TRUE(state.skippedUpToDate);
}

TEST(WatchSessionTest, applyBuildResultAndApplyRebuildCycleStateCleanly)
{
    WatchState state;
    state.currentPass = 2;
    state.currentTool = "bibtex";
    state.rerunReason = "aux changed";
    state.skippedUpToDate = true;

    applyBuildResult(state, makeResult(DriverResult { .success = true, .passes = 2 }), 140ms, 5);
    EXPECT_EQ(state.phase, WatchState::Phase::Watching);
    EXPECT_EQ(state.buildCount, 1);
    EXPECT_EQ(state.lastDuration, 140ms);
    EXPECT_EQ(state.watchedCount, 5u);

    applyRebuild(state, { "/project/chapter.tex" });
    EXPECT_EQ(state.phase, WatchState::Phase::Building);
    EXPECT_EQ(state.currentPass, 0);
    EXPECT_TRUE(state.currentTool.empty());
    EXPECT_TRUE(state.rerunReason.empty());
    EXPECT_FALSE(state.skippedUpToDate);
    ASSERT_EQ(state.lastChanged.size(), 1u);
    EXPECT_EQ(state.lastChanged[0], fs::path("/project/chapter.tex"));
}

// ── computeWatchSet & makeWatchExclusion ──────────────────────────────────────

TEST(WatchSessionTest, computeWatchSetCombinesSourcesExtrasAndMissingTexInputsOnly)
{
    BuildOptions build { .texFile = "/project/main.tex" };
    BuildFileSets files { .sources = { "/project/chap1.tex", "/project/refs.bib" } };
    BuildResult result = makeResult(DriverResult {
        .success = false,
        .diagnostics = {
            Diagnostic { .category = "missing-file", .missingFile = "later" },
            Diagnostic { .category = "missing-file", .missingFile = "appendix.tex" },
            Diagnostic { .category = "missing-file", .missingFile = "fancyhdr.sty" },
            Diagnostic { .category = "missing-file", .missingFile = "custom.cls" },
            Diagnostic { .category = "missing-file", .missingFile = "" },
            Diagnostic { .category = "syntax", .missingFile = "ignored" },
        },
    });

    auto set = computeWatchSet(build, files, result, { "/project/figures" });

    auto has = [&](const fs::path& p) { return std::ranges::contains(set, p); };
    EXPECT_TRUE(has("/project/main.tex"));
    EXPECT_TRUE(has("/project/chap1.tex"));
    EXPECT_TRUE(has("/project/refs.bib"));
    EXPECT_TRUE(has("/project/figures"));
    EXPECT_TRUE(has("/project/later.tex"));
    EXPECT_TRUE(has("/project/appendix.tex"));
    EXPECT_FALSE(has("/project/fancyhdr.sty"));
    EXPECT_FALSE(has("/project/custom.cls"));
    EXPECT_FALSE(has("/project/ignored.tex"));
}

TEST(WatchSessionTest, makeWatchExclusionDropsOutputDirAndPublishedArtifacts)
{
    auto dir = maketex::test_utils::uniqueTempDir("maketex_watch_excl");
    fs::create_directories(dir / ".maketex");

    BuildOptions build { .texFile = dir / "main.tex" };
    auto publishedPdf = dir / "main.pdf";
    auto exclude = makeWatchExclusion(build, { publishedPdf });

    EXPECT_TRUE(exclude(dir / ".maketex" / "main.aux"));
    EXPECT_TRUE(exclude(publishedPdf));
    EXPECT_FALSE(exclude(dir / "main.tex"));
    EXPECT_FALSE(exclude(dir / "chap1.tex"));

    fs::remove_all(dir);
}

// ── FTXUI view builders ───────────────────────────────────────────────────────

TEST(WatchSessionTest, watchStatusLineRendersAllBuildingAndWatchingPhases)
{
    WatchState building;
    building.phase = WatchState::Phase::Building;
    EXPECT_NE(renderToString(watchStatusLine(building)).find("building"), std::string::npos);

    building.currentPass = 2;
    building.rerunReason = "main.aux changed";
    building.currentTool = "biber";
    auto active = renderToString(watchStatusLine(building));
    EXPECT_NE(active.find("building · pass 2 · main.aux changed · biber"), std::string::npos);

    WatchState upToDate;
    upToDate.phase = WatchState::Phase::Watching;
    upToDate.skippedUpToDate = true;
    upToDate.watchedCount = 4;
    EXPECT_NE(renderToString(watchStatusLine(upToDate)).find("up to date · watching 4 paths"),
        std::string::npos);

    WatchState succeeded;
    succeeded.phase = WatchState::Phase::Watching;
    succeeded.lastResult = makeResult(DriverResult { .success = true, .passes = 1 });
    succeeded.lastDuration = 82ms;
    succeeded.watchedCount = 3;
    EXPECT_NE(renderToString(watchStatusLine(succeeded)).find("built in 82 ms · watching 3 paths"),
        std::string::npos);

    WatchState failed;
    failed.phase = WatchState::Phase::Watching;
    failed.lastResult = makeResult(DriverResult {
        .success = false,
        .lastError = "pdflatex exited with code 1",
    });
    failed.watchedCount = 2;
    auto failedStr = renderToString(watchStatusLine(failed));
    EXPECT_NE(failedStr.find("pdflatex exited with code 1"), std::string::npos);
    EXPECT_NE(failedStr.find("watching 2 paths"), std::string::npos);
}

TEST(WatchSessionTest, renderWatchShowsInitialVsPostBuildEngineChangedFilesAndDiagnostics)
{
    BuildOptions build { .texFile = "/project/thesis.tex" };

    // Before first build completes: shows initialEngine and "no diagnostics".
    WatchState initial;
    initial.initialEngine = "pdflatex";
    auto initialRendered = renderToString(renderWatch(initial, build));
    EXPECT_NE(initialRendered.find("thesis.tex"), std::string::npos);
    EXPECT_NE(initialRendered.find("pdflatex"), std::string::npos);
    EXPECT_NE(initialRendered.find("build #0"), std::string::npos);
    EXPECT_NE(initialRendered.find("no diagnostics"), std::string::npos);

    // After a rebuild with diagnostics and changed files.
    WatchState after;
    after.phase = WatchState::Phase::Watching;
    after.buildCount = 2;
    after.verbose = true;
    after.lastChanged = { "/project/chapter1.tex" };
    after.lastResult = makeResult(DriverResult {
                                      .success = false,
                                      .lastError = "compile failed",
                                      .diagnostics = { Diagnostic {
                                          .severity = Diagnostic::Severity::Error,
                                          .file = "chapter1.tex",
                                          .line = 7,
                                          .message = "Undefined control sequence",
                                          .rawContext = "l.7 \\nope",
                                      } },
                                  },
        EngineKind::LuaLaTeX);
    after.lastResult.engineReason = "loads fontspec";

    auto afterRendered = renderToString(renderWatch(after, build));
    EXPECT_NE(afterRendered.find("lualatex (loads fontspec)"), std::string::npos);
    EXPECT_NE(afterRendered.find("build #2"), std::string::npos);
    EXPECT_NE(afterRendered.find("changed:"), std::string::npos);
    EXPECT_NE(afterRendered.find("chapter1.tex"), std::string::npos);
    EXPECT_NE(afterRendered.find("Undefined control sequence"), std::string::npos);
    EXPECT_NE(afterRendered.find("l.7 \\nope"), std::string::npos);
}

// ── runWatchLoop & runWatchPlain ──────────────────────────────────────────────

TEST(WatchSessionTest, runWatchLoopHandlesStickyEngineRetryExceptionsAndInstallMissing)
{
    std::stop_source stopSource;
    FakeFileWatcher watcher(stopSource);
    watcher.scriptedChanges.push_back({ "/project/main.tex" });
    watcher.scriptedChanges.push_back({ "/project/chap.tex" });

    FakeWatchEnv env;
    // Build 1: succeeds with retriedEngine = LuaLaTeX (must become sticky for build 2+).
    auto first = makeResult(DriverResult { .success = true, .passes = 1 }, EngineKind::LuaLaTeX);
    first.retriedEngine = true;
    env.scriptedResults.push_back(first);
    env.scriptedThrows.push_back(std::nullopt);
    // Build 2: throws std::runtime_error (must be caught and reported, not abort loop).
    env.scriptedThrows.push_back("database busy");
    // Build 3: succeeds using the sticky LuaLaTeX engine.
    env.scriptedResults.push_back(makeResult(DriverResult { .success = true, .passes = 1 },
        EngineKind::LuaLaTeX));
    env.scriptedThrows.push_back(std::nullopt);

    EffectiveOptions opts;
    opts.build.texFile = "/project/main.tex";
    opts.extras.installMissing = true;
    opts.extras.installMissingRequested = true;

    std::vector<BuildResult> seenResults;
    std::vector<std::vector<fs::path>> seenChanges;
    WatchLoopCallbacks callbacks {
        .observer = { },
        .onResult =
            [&](BuildResult r, std::chrono::milliseconds, std::size_t) {
                seenResults.push_back(std::move(r));
            },
        .onRebuild =
            [&](std::vector<fs::path> changed) {
                seenChanges.push_back(std::move(changed));
            },
    };

    runWatchLoop(opts, watcher, stopSource.get_token(), callbacks, env);

    ASSERT_EQ(seenResults.size(), 3u);
    EXPECT_TRUE(seenResults[0].success);
    EXPECT_FALSE(seenResults[1].success);
    EXPECT_EQ(seenResults[1].lastError, "database busy");
    EXPECT_TRUE(seenResults[2].success);

    // Build 2 and Build 3 must inherit the sticky retried engine.
    ASSERT_EQ(env.seenBuildOptions.size(), 3u);
    EXPECT_FALSE(env.seenBuildOptions[0].engine.has_value());
    EXPECT_EQ(env.seenBuildOptions[1].engine, EngineKind::LuaLaTeX);
    EXPECT_EQ(env.seenBuildOptions[2].engine, EngineKind::LuaLaTeX);

    // Watch mode never sets interactive = true on InstallPolicy.
    ASSERT_EQ(env.seenInstallPolicies.size(), 3u);
    EXPECT_FALSE(env.seenInstallPolicies[0].interactive);
    EXPECT_TRUE(env.seenInstallPolicies[0].requested);

    ASSERT_EQ(seenChanges.size(), 2u);
    EXPECT_EQ(seenChanges[0], std::vector<fs::path> { "/project/main.tex" });
    EXPECT_EQ(seenChanges[1], std::vector<fs::path> { "/project/chap.tex" });
}

TEST(WatchSessionTest, runWatchPlainFormatsPlainAndStyledReports)
{
    // Plain unstyled mode: 1 path (singular) on failure, then change + success.
    {
        std::stop_source stopSource;
        FakeFileWatcher watcher(stopSource);
        watcher.scriptedChanges.push_back({ "/project/main.tex" });

        FakeWatchEnv env;
        env.styled = false;
        env.scriptedResults.push_back(makeResult(DriverResult {
            .success = false,
            .passes = 1,
            .lastError = "compile failed",
        }));
        env.scriptedResults.push_back(makeResult(DriverResult {
            .success = true,
            .passes = 2,
        }));

        EffectiveOptions opts;
        opts.build.texFile = "/project/main.tex";
        opts.build.stopToken = stopSource.get_token();
        opts.extras.installMissing = false;

        EXPECT_EQ(runWatchPlain(opts, watcher, env), 0);
        EXPECT_NE(env.errStream.str().find("maketex: failed after 1 pass: compile failed"),
            std::string::npos);
        EXPECT_NE(env.outStream.str().find("maketex: watching 1 path (25 ms build"),
            std::string::npos);
        EXPECT_NE(env.outStream.str().find("maketex: changed: main.tex"),
            std::string::npos);
        EXPECT_NE(env.outStream.str().find("maketex: done in 2 passes"),
            std::string::npos);
    }

    // Styled mode: renders FTXUI inline report + plural watching paths.
    {
        std::stop_source stopSource;
        FakeFileWatcher watcher(stopSource);

        FakeWatchEnv env;
        env.styled = true;
        env.scannedFiles = BuildFileSets { .sources = { "/project/a.tex", "/project/b.tex" } };
        env.scriptedResults.push_back(makeResult(DriverResult {
            .success = true,
            .passes = 1,
        }));

        EffectiveOptions opts;
        opts.build.texFile = "/project/main.tex";
        opts.build.stopToken = stopSource.get_token();
        opts.extras.installMissing = false;

        EXPECT_EQ(runWatchPlain(opts, watcher, env), 0);
        EXPECT_NE(env.outStream.str().find("main.tex"), std::string::npos);
        EXPECT_NE(env.outStream.str().find("maketex: watching 3 paths"),
            std::string::npos);
    }
}
