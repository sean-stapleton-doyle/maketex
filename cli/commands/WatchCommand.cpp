#include "commands/WatchCommand.h"

#include "FileWatcher.h"
#include "commands/WatchSession.h"
#include "render/InlineReport.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <chrono>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>

namespace fs = std::filesystem;
using namespace ftxui;

namespace maketex::cli {

namespace {

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

        WatchLoopCallbacks callbacks {
            .observer =
                [&](const BuildEvent& event) {
                    {
                        std::lock_guard lock(stateMutex);
                        applyBuildEvent(state, event);
                    }
                    post();
                },
            .onResult =
                [&](BuildResult result, std::chrono::milliseconds duration,
                    std::size_t watched) {
                    {
                        std::lock_guard lock(stateMutex);
                        applyBuildResult(state, std::move(result), duration, watched);
                    }
                    post();
                },
            .onRebuild =
                [&](std::vector<fs::path> changed) {
                    {
                        std::lock_guard lock(stateMutex);
                        applyRebuild(state, std::move(changed));
                    }
                    post();
                },
        };

        std::jthread worker([&](const std::stop_token& stopToken) {
            runWatchLoop(options, watcher, stopToken, callbacks);
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

        // SIGTERM/SIGHUP from outside (Ctrl-C is a key event in the TUI)
        // closes the screen the same way `q` does, restoring the terminal.
        std::stop_callback onSignal(options.build.stopToken, [&screen] { screen.Exit(); });

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
    if (options.extras.noTui || !useStyledOutput(options.extras.plain)) {
        FileWatcher watcher(
            std::chrono::milliseconds(options.extras.watchIntervalMs));
        return runWatchPlain(options, watcher);
    }
    return runWatchTui(options);
}

} // namespace maketex::cli
