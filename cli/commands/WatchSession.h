#pragma once

#include "EffectiveOptions.h"
#include "FileWatcher.h"
#include "PackageInstaller.h"

#include <ftxui/dom/elements.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace maketex::cli {

// Mutable UI/session state for `maketex watch` TUI mode.
struct WatchState {
    enum class Phase {
        Building,
        Watching,
    };

    Phase phase = Phase::Building;
    int currentPass = 0;
    std::string currentTool;
    std::string rerunReason; // why the current pass runs (passes 2+)
    bool skippedUpToDate = false;
    BuildResult lastResult;
    std::chrono::milliseconds lastDuration { };
    std::size_t watchedCount = 0;
    int buildCount = 0;
    std::vector<std::filesystem::path> lastChanged;
    bool verbose = false;
    // Shown until the first build reports the engine it used; computed
    // once, as detection reads the document.
    std::string initialEngine;
};

// Pure state-machine reducers for WatchState.
void applyBuildEvent(WatchState& state, const BuildEvent& event);
void applyBuildResult(WatchState& state, BuildResult result,
    std::chrono::milliseconds duration, std::size_t watchedCount);
void applyRebuild(WatchState& state, std::vector<std::filesystem::path> changed);

// Pure watch-set and output-exclusion helpers.
std::vector<std::filesystem::path> computeWatchSet(const BuildOptions& build,
    const std::optional<BuildFileSets>& files, const BuildResult& result,
    const std::vector<std::filesystem::path>& extraPaths);

IFileWatcher::Exclusion makeWatchExclusion(const BuildOptions& build,
    const std::vector<std::filesystem::path>& publishedArtifacts);

// Pure FTXUI view builders for the watch TUI.
ftxui::Element watchStatusLine(const WatchState& state);
ftxui::Element renderWatch(const WatchState& state, const BuildOptions& build);

struct WatchLoopCallbacks {
    BuildObserver observer; // forwarded into BuildOptions; may be empty
    // result, wall-clock duration, number of watched paths
    std::function<void(BuildResult, std::chrono::milliseconds, std::size_t)> onResult;
    // changed paths that triggered the next build (empty = forced rebuild)
    std::function<void(std::vector<std::filesystem::path>)> onRebuild;
};

// External seams for watch orchestration so build execution, exception
// recovery, sticky engine retry, watch-set updates, and plain-mode reporting
// are unit-testable without invoking TeX or sleeping.
class IWatchEnv {
public:
    virtual ~IWatchEnv() = default;

    virtual BuildResult runBuild(const BuildOptions& options) = 0;
    virtual BuildResult buildInstallingMissing(const BuildOptions& options,
        const InstallPolicy& policy, std::ostream& log) = 0;
    virtual std::optional<BuildFileSets> scanBuildFiles(
        const BuildOptions& options) = 0;
    virtual void addInstallHints(std::vector<Diagnostic>& diagnostics) = 0;
    virtual std::vector<std::filesystem::path> publishedArtifactPaths(
        const BuildOptions& options) = 0;
    virtual bool isStyled(bool plainFlag) const = 0;
    virtual std::chrono::steady_clock::time_point now() const = 0;
    virtual std::ostream& out() = 0;
    virtual std::ostream& err() = 0;
};

// Build → refresh watch set from the .fls → wait → repeat until stopToken fires.
void runWatchLoop(const EffectiveOptions& options, IFileWatcher& watcher,
    const std::stop_token& stopToken, const WatchLoopCallbacks& callbacks,
    IWatchEnv& env);
void runWatchLoop(const EffectiveOptions& options, IFileWatcher& watcher,
    const std::stop_token& stopToken, const WatchLoopCallbacks& callbacks);

// Plain (non-TUI) watch mode, used with --no-tui or when stdout is not a tty.
int runWatchPlain(const EffectiveOptions& options, IFileWatcher& watcher,
    IWatchEnv& env);
int runWatchPlain(const EffectiveOptions& options, IFileWatcher& watcher);

} // namespace maketex::cli
