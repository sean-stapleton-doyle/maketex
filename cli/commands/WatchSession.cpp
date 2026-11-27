#include "commands/WatchSession.h"

#include "PackageHints.h"
#include "PackageInstaller.h"
#include "PathUtil.h"
#include "render/InlineReport.h"

#include <algorithm>
#include <exception>
#include <iostream>
#include <sstream>
#include <utility>

namespace fs = std::filesystem;
using namespace ftxui;

namespace maketex::cli {

namespace {

    class SystemWatchEnv final : public IWatchEnv {
    public:
        BuildResult runBuild(const BuildOptions& options) override
        {
            return maketex::runBuild(options);
        }

        BuildResult buildInstallingMissing(const BuildOptions& options,
            const InstallPolicy& policy, std::ostream& log) override
        {
            return maketex::cli::buildInstallingMissing(options, policy, log);
        }

        std::optional<BuildFileSets> scanBuildFiles(const BuildOptions& options) override
        {
            return maketex::scanBuildFiles(options);
        }

        void addInstallHints(std::vector<Diagnostic>& diagnostics) override
        {
            maketex::cli::addInstallHints(diagnostics);
        }

        std::vector<fs::path> publishedArtifactPaths(const BuildOptions& options) override
        {
            return maketex::publishedArtifactPaths(options);
        }

        bool isStyled(bool plainFlag) const override
        {
            return useStyledOutput(plainFlag);
        }

        std::chrono::steady_clock::time_point now() const override
        {
            return std::chrono::steady_clock::now();
        }

        std::ostream& out() override { return std::cout; }
        std::ostream& err() override { return std::cerr; }
    };

} // namespace

void applyBuildEvent(WatchState& state, const BuildEvent& event)
{
    switch (event.kind) {
    case BuildEvent::Kind::PassStarted:
        state.currentPass = event.pass;
        state.currentTool.clear();
        break;
    case BuildEvent::Kind::PassFinished:
        break;
    case BuildEvent::Kind::ToolStarted:
        state.currentTool = event.toolName;
        break;
    case BuildEvent::Kind::ToolFinished:
        state.currentTool.clear();
        break;
    case BuildEvent::Kind::SkippedUpToDate:
        state.skippedUpToDate = true;
        break;
    case BuildEvent::Kind::RerunScheduled:
        state.rerunReason = event.detail;
        break;
    }
}

void applyBuildResult(WatchState& state, BuildResult result,
    std::chrono::milliseconds duration, std::size_t watchedCount)
{
    state.phase = WatchState::Phase::Watching;
    state.lastResult = std::move(result);
    state.lastDuration = duration;
    state.watchedCount = watchedCount;
    ++state.buildCount;
}

void applyRebuild(WatchState& state, std::vector<fs::path> changed)
{
    state.phase = WatchState::Phase::Building;
    state.currentPass = 0;
    state.currentTool.clear();
    state.rerunReason.clear();
    state.skippedUpToDate = false;
    state.lastChanged = std::move(changed);
}

std::vector<fs::path> computeWatchSet(const BuildOptions& build,
    const std::optional<BuildFileSets>& files, const BuildResult& result,
    const std::vector<fs::path>& extraPaths)
{
    std::vector<fs::path> watchSet { build.texFile };
    if (files) {
        watchSet.insert(watchSet.end(), files->sources.begin(),
            files->sources.end());
    }
    watchSet.insert(watchSet.end(), extraPaths.begin(), extraPaths.end());

    // A missing \input file is not in the .fls; watch for it to appear so
    // creating it triggers the rebuild.
    for (const auto& d : result.diagnostics) {
        if (d.category != "missing-file" || d.missingFile.empty())
            continue;
        fs::path missing { d.missingFile };
        if (missing.extension() == ".sty" || missing.extension() == ".cls")
            continue;
        if (!missing.has_extension())
            missing += ".tex";
        watchSet.push_back(build.texFile.parent_path() / missing);
    }
    return watchSet;
}

IFileWatcher::Exclusion makeWatchExclusion(const BuildOptions& build,
    const std::vector<fs::path>& publishedArtifacts)
{
    const auto outDir = fs::weakly_canonical(build.resolvedOutputDir());
    std::vector<fs::path> published;
    published.reserve(publishedArtifacts.size());
    for (const auto& p : publishedArtifacts)
        published.push_back(fs::weakly_canonical(p));

    return [outDir, published = std::move(published)](const fs::path& p) {
        return isInside(p, outDir)
            || std::ranges::contains(published, fs::weakly_canonical(p));
    };
}

Element watchStatusLine(const WatchState& state)
{
    if (state.phase == WatchState::Phase::Building) {
        std::string detail = "building";
        if (state.currentPass > 0)
            detail += " · pass " + std::to_string(state.currentPass);
        if (state.currentPass > 1 && !state.rerunReason.empty())
            detail += " · " + state.rerunReason;
        if (!state.currentTool.empty())
            detail += " · " + state.currentTool;
        return hbox(text("● ") | color(Color::Yellow),
            text(detail) | color(Color::Yellow));
    }

    std::string watching = " · watching "
        + std::to_string(state.watchedCount) + " paths";
    if (state.skippedUpToDate)
        return hbox(text("✓ ") | color(Color::Green),
            text("up to date" + watching));
    if (state.lastResult.success)
        return hbox(text("✓ ") | color(Color::Green),
            text("built in " + std::to_string(state.lastDuration.count())
                + " ms" + watching));
    return hbox(text("✗ ") | color(Color::Red),
        text(state.lastResult.lastError) | color(Color::Red),
        text(watching) | dim);
}

Element renderWatch(const WatchState& state, const BuildOptions& build)
{
    Elements changed;
    for (const auto& p : state.lastChanged)
        changed.push_back(text(p.filename().string()) | dim);

    Elements diagnosticRows;
    for (const auto& diagnostic : state.lastResult.diagnostics)
        diagnosticRows.push_back(diagnosticRow(diagnostic, state.verbose));
    if (diagnosticRows.empty())
        diagnosticRows.push_back(text("no diagnostics") | dim);

    return vbox({
               hbox(text("maketex watch ") | bold,
                   text(build.texFile.filename().string()),
                   text("  ·  ") | dim,
                   text(state.buildCount == 0
                           ? state.initialEngine
                           : engineLabel(state.lastResult.engine,
                                 state.lastResult.engineReason))
                       | dim,
                   filler(),
                   text("build #" + std::to_string(state.buildCount)) | dim),
               separator(),
               watchStatusLine(state),
               changed.empty()
                   ? text("")
                   : hbox(text("changed: ") | dim, hbox(std::move(changed))),
               separator(),
               vbox(std::move(diagnosticRows)) | yframe | flex,
               separator(),
               text("q quit · r rebuild · v raw context") | dim,
           })
        | border;
}

void runWatchLoop(const EffectiveOptions& options, IFileWatcher& watcher,
    const std::stop_token& stopToken, const WatchLoopCallbacks& callbacks,
    IWatchEnv& env)
{
    BuildOptions build = options.build;
    build.observer = callbacks.observer;
    // Quitting interrupts the running engine pass rather than waiting for the
    // build to converge.
    build.stopToken = stopToken;

    // The document is watched from the start, so an edit made during the very
    // first build is not lost either.
    watcher.setWatchSet({ build.texFile });

    while (!stopToken.stop_requested()) {
        // Snapshot before building: a save that lands mid-build must trigger
        // the next build, not be absorbed into the baseline.
        auto preBuild = watcher.captureBaseline();
        auto start = env.now();
        BuildResult result;
        try {
            // Never prompts: the TUI owns the terminal. Installs happen only
            // into a user-writable tree; otherwise a note says why.
            std::ostringstream installLog;
            result = options.extras.installMissing
                ? env.buildInstallingMissing(build,
                      InstallPolicy { .requested = options.extras.installMissingRequested },
                      installLog)
                : env.runBuild(build);
            env.addInstallHints(result.diagnostics);
            // Don't pay for the failing pdflatex attempt on every save.
            if (result.retriedEngine)
                build.engine = result.engine;
        } catch (const std::exception& e) {
            // Engine not installed, database trouble — surface it like a failed
            // build instead of tearing down the watch session.
            result.success = false;
            result.lastError = e.what();
        }
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            env.now() - start);

        auto watchSet = computeWatchSet(build, env.scanBuildFiles(build),
            result, options.extras.watchExtraPaths);
        // Our own outputs must never retrigger a build: drop anything under
        // the output directory and the published PDF/SyncTeX.
        watcher.setWatchSet(watchSet,
            makeWatchExclusion(build, env.publishedArtifactPaths(build)));

        callbacks.onResult(std::move(result), duration, watchSet.size());

        auto changed = watcher.waitForChange(stopToken, preBuild);
        if (stopToken.stop_requested())
            return;
        callbacks.onRebuild(std::move(changed));
    }
}

void runWatchLoop(const EffectiveOptions& options, IFileWatcher& watcher,
    const std::stop_token& stopToken, const WatchLoopCallbacks& callbacks)
{
    SystemWatchEnv env;
    runWatchLoop(options, watcher, stopToken, callbacks, env);
}

int runWatchPlain(const EffectiveOptions& options, IFileWatcher& watcher,
    IWatchEnv& env)
{
    const bool styled = env.isStyled(options.extras.plain);

    WatchLoopCallbacks callbacks {
        .observer = { },
        .onResult =
            [&](const BuildResult& result,
                std::chrono::milliseconds duration, std::size_t watched) {
                printHumanReport(result, options.build, options.extras.verbose,
                    styled, env.out(), env.err());
                env.out() << "maketex: watching " << watched << " path"
                          << (watched == 1 ? "" : "s") << " ("
                          << duration.count()
                          << " ms build, Ctrl-C to stop)\n";
                // Each report must reach a pipe (`| tee log`, an editor
                // plugin) now, not when the buffer fills or we exit.
                env.out().flush();
                env.err().flush();
            },
        .onRebuild =
            [&](const std::vector<fs::path>& changed) {
                for (const auto& p : changed)
                    env.out() << "maketex: changed: "
                              << p.filename().string() << "\n";
                env.out().flush();
            },
    };

    // Runs until a signal (Ctrl-C, SIGTERM) requests the stop; the build in
    // progress is cancelled with it.
    runWatchLoop(options, watcher, options.build.stopToken, callbacks, env);
    return 0;
}

int runWatchPlain(const EffectiveOptions& options, IFileWatcher& watcher)
{
    SystemWatchEnv env;
    return runWatchPlain(options, watcher, env);
}

} // namespace maketex::cli
