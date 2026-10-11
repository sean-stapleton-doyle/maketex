#include "commands/WatchCommand.h"

#include "PackageHints.h"
#include "PackageInstaller.h"
#include "PathUtil.h"

#include "FileWatcher.h"
#include "render/InlineReport.h"

#include "maketex/DiagnosticFormatter.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stop_token>
#include <thread>
#include <utility>

namespace fs = std::filesystem;
using namespace ftxui;

namespace maketex::cli {

namespace {

    using Clock = std::chrono::steady_clock;

    struct LoopCallbacks {
        BuildObserver observer; // forwarded into BuildOptions; may be empty
        // result, wall-clock duration, number of watched paths
        std::function<void(BuildResult, std::chrono::milliseconds, std::size_t)>
            onResult;
        // changed paths that triggered the next build (empty = forced rebuild)
        std::function<void(std::vector<fs::path>)> onRebuild;
    };

    // Build → refresh watch set from the .fls → wait → repeat. Returns when
    // stopToken fires; an in-flight engine pass finishes first.
    void watchLoop(const EffectiveOptions& options, FileWatcher& watcher,
        const std::stop_token& stopToken, const LoopCallbacks& callbacks)
    {
        BuildOptions build = options.build;
        build.observer = callbacks.observer;
        // Quitting interrupts the running engine pass rather than waiting
        // for the build to converge.
        build.stopToken = stopToken;

        // The document is watched from the start, so an edit made during
        // the very first build is not lost either.
        watcher.setWatchSet({ build.texFile });

        while (!stopToken.stop_requested()) {
            // Snapshot before building: a save that lands mid-build must
            // trigger the next build, not be absorbed into the baseline.
            auto preBuild = watcher.captureBaseline();
            auto start = Clock::now();
            BuildResult result;
            try {
                // Never prompts: the TUI owns the terminal. Installs happen
                // only into a user-writable tree; otherwise a note says why.
                std::ostringstream installLog;
                result = options.extras.installMissing
                    ? buildInstallingMissing(build,
                          InstallPolicy { .requested = options.extras.installMissingRequested },
                          installLog)
                    : runBuild(build);
                addInstallHints(result.diagnostics);
                // Don't pay for the failing pdflatex attempt on every save.
                if (result.retriedEngine)
                    build.engine = result.engine;
            } catch (const std::exception& e) {
                // Engine not installed, database trouble — surface it like a
                // failed build instead of tearing down the watch session.
                result.success = false;
                result.lastError = e.what();
            }
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                Clock::now() - start);

            std::vector<fs::path> watchSet { build.texFile };
            if (auto sets = scanBuildFiles(build)) {
                watchSet.insert(watchSet.end(), sets->sources.begin(),
                    sets->sources.end());
            }
            watchSet.insert(watchSet.end(),
                options.extras.watchExtraPaths.begin(),
                options.extras.watchExtraPaths.end());
            // A missing \input file is not in the .fls; watch for it to
            // appear so creating it triggers the rebuild.
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

            // Our own outputs must never retrigger a build: drop anything
            // under the output directory and the published PDF/SyncTeX.
            // Applied by the watcher after it expands directories, since
            // watch-extra-paths may name one that contains them.
            const auto outDir = fs::weakly_canonical(build.resolvedOutputDir());
            std::vector<fs::path> published;
            for (const auto& p : publishedArtifactPaths(build))
                published.push_back(fs::weakly_canonical(p));
            watcher.setWatchSet(watchSet, [=](const fs::path& p) {
                return isInside(p, outDir)
                    || std::ranges::contains(published, fs::weakly_canonical(p));
            });

            callbacks.onResult(std::move(result), duration, watchSet.size());

            auto changed = watcher.waitForChange(stopToken, preBuild);
            if (stopToken.stop_requested())
                return;
            callbacks.onRebuild(std::move(changed));
        }
    }

    // ── plain mode (--no-tui, or stdout is not a terminal) ───────────────────

    int runWatchPlain(const EffectiveOptions& options)
    {
        FileWatcher watcher(
            std::chrono::milliseconds(options.extras.watchIntervalMs));
        bool styled = useStyledOutput(options.extras.plain);

        LoopCallbacks callbacks {
            .observer = { },
            .onResult =
                [&](const BuildResult& result,
                    std::chrono::milliseconds duration, std::size_t watched) {
                    if (styled) {
                        printInline(buildReport(result, options.build,
                            options.extras.verbose));
                    } else {
                        diagnostics::format(result.diagnostics, std::cerr,
                            options.extras.verbose ? diagnostics::Verbosity::Verbose
                                                   : diagnostics::Verbosity::Normal);
                        if (result.success)
                            std::cout << "maketex: done in " << passCount(result.passes) << "\n";
                        else
                            std::cerr << "maketex: failed: " << result.lastError
                                      << "\n";
                    }
                    std::cout << "maketex: watching " << watched << " path"
                              << (watched == 1 ? "" : "s") << " ("
                              << duration.count()
                              << " ms build, Ctrl-C to stop)\n";
                },
            .onRebuild =
                [](const std::vector<fs::path>& changed) {
                    for (const auto& p : changed)
                        std::cout << "maketex: changed: "
                                  << p.filename().string() << "\n";
                },
        };

        std::stop_source stop;
        watchLoop(options, watcher, stop.get_token(), callbacks);
        return 0;
    }

    // ── full-screen TUI ───────────────────────────────────────────────────────

    struct WatchState {
        enum class Phase { Building,
            Watching };

        Phase phase = Phase::Building;
        int currentPass = 0;
        std::string currentTool;
        bool skippedUpToDate = false;
        BuildResult lastResult;
        std::chrono::milliseconds lastDuration { };
        std::size_t watchedCount = 0;
        int buildCount = 0;
        std::vector<fs::path> lastChanged;
        bool verbose = false;
        // Shown until the first build reports the engine it used; computed
        // once, as detection reads the document.
        std::string initialEngine;
    };

    Element statusLine(const WatchState& state)
    {
        if (state.phase == WatchState::Phase::Building) {
            std::string detail = "building";
            if (state.currentPass > 0)
                detail += " · pass " + std::to_string(state.currentPass);
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
                               : engineLabel(state.lastResult.engine, state.lastResult.engineReason))
                           | dim,
                       filler(),
                       text("build #" + std::to_string(state.buildCount)) | dim),
                   separator(),
                   statusLine(state),
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

    int runWatchTui(const EffectiveOptions& options)
    {
        auto screen = ScreenInteractive::Fullscreen();
        FileWatcher watcher(
            std::chrono::milliseconds(options.extras.watchIntervalMs));

        WatchState state;
        state.verbose = options.extras.verbose;
        auto choice = chooseEngine(options.build);
        state.initialEngine = engineLabel(choice.kind, choice.reason);
        std::mutex stateMutex;

        auto post = [&screen] { screen.PostEvent(Event::Custom); };

        LoopCallbacks callbacks {
            .observer =
                [&](const BuildEvent& event) {
                    {
                        std::lock_guard lock(stateMutex);
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
                        }
                    }
                    post();
                },
            .onResult =
                [&](BuildResult result, std::chrono::milliseconds duration,
                    std::size_t watched) {
                    {
                        std::lock_guard lock(stateMutex);
                        state.phase = WatchState::Phase::Watching;
                        state.lastResult = std::move(result);
                        state.lastDuration = duration;
                        state.watchedCount = watched;
                        ++state.buildCount;
                    }
                    post();
                },
            .onRebuild =
                [&](std::vector<fs::path> changed) {
                    {
                        std::lock_guard lock(stateMutex);
                        state.phase = WatchState::Phase::Building;
                        state.currentPass = 0;
                        state.currentTool.clear();
                        state.skippedUpToDate = false;
                        state.lastChanged = std::move(changed);
                    }
                    post();
                },
        };

        std::jthread worker([&](const std::stop_token& stopToken) {
            watchLoop(options, watcher, stopToken, callbacks);
        });

        auto renderer = Renderer([&] {
            std::lock_guard lock(stateMutex);
            return renderWatch(state, options.build);
        });

        auto component = CatchEvent(renderer, [&](const Event& event) {
            if (event == Event::Character('q') || event == Event::CtrlC) {
                screen.Exit();
                return true;
            }
            if (event == Event::Character('r')) {
                watcher.nudge();
                return true;
            }
            if (event == Event::Character('v')) {
                std::lock_guard lock(stateMutex);
                state.verbose = !state.verbose;
                return true;
            }
            return false;
        });

        screen.Loop(component);

        // jthread's destructor would do this too; being explicit makes the
        // shutdown order (stop, wake the poll wait, join) readable.
        worker.request_stop();
        worker.join();
        return 0;
    }

} // namespace

int runWatchCommand(const EffectiveOptions& options)
{
    if (options.extras.noTui || !useStyledOutput(options.extras.plain))
        return runWatchPlain(options);
    return runWatchTui(options);
}

} // namespace maketex::cli
