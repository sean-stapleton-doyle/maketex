#include "commands/WatchCommand.h"

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
        std::function<void(DriverResult, std::chrono::milliseconds, std::size_t)>
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

        while (!stopToken.stop_requested()) {
            auto start = Clock::now();
            DriverResult result;
            try {
                result = runBuild(build);
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

            // Our own outputs must never retrigger a build: drop anything
            // under the output directory and the published PDF/SyncTeX
            // (watch-extra-paths may name a directory that contains both).
            const auto outDir = fs::weakly_canonical(build.resolvedOutputDir());
            const auto texDir = build.texFile.parent_path();
            const std::string jobName = build.texFile.stem().string();
            std::erase_if(watchSet, [&](const fs::path& p) {
                auto canonical = fs::weakly_canonical(p);
                auto rel = canonical.lexically_relative(outDir);
                if (!rel.empty() && *rel.begin() != "..")
                    return true;
                return build.usesDefaultOutputDir()
                    && (canonical == texDir / (jobName + ".pdf")
                        || canonical == texDir / (jobName + ".synctex.gz"));
            });
            watcher.setWatchSet(watchSet);

            callbacks.onResult(std::move(result), duration, watchSet.size());

            auto changed = watcher.waitForChange(stopToken);
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
                [&](const DriverResult& result,
                    std::chrono::milliseconds duration, std::size_t watched) {
                    if (styled) {
                        printInline(buildReport(result, options.build,
                            options.extras.verbose));
                    } else {
                        diagnostics::format(result.diagnostics, std::cerr,
                            options.extras.verbose ? diagnostics::Verbosity::Verbose
                                                   : diagnostics::Verbosity::Normal);
                        if (result.success)
                            std::cout << "maketex: done in " << result.passes
                                      << " pass"
                                      << (result.passes == 1 ? "" : "es") << "\n";
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
        DriverResult lastResult;
        std::chrono::milliseconds lastDuration { };
        std::size_t watchedCount = 0;
        int buildCount = 0;
        std::vector<fs::path> lastChanged;
        bool verbose = false;
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
                       text(std::string(engineKindName(build.engine))) | dim,
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
                [&](DriverResult result, std::chrono::milliseconds duration,
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
