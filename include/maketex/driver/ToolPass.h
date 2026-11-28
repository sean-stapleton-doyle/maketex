#pragma once

#include "maketex/BuildEvent.h"
#include "maketex/Diagnostic.h"
#include "maketex/EngineInvocation.h"
#include "maketex/ITool.h"
#include "maketex/ProcessRunner.h"
#include "maketex/RunResult.h"
#include "maketex/StateDatabase.h"
#include "maketex/ToolContext.h"
#include "maketex/ToolInvocation.h"
#include "maketex/driver/FileDigests.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace maketex::driver {

// Outcome of running the stale auxiliary tools once.
struct ToolPassOutcome {
    bool ranAny = false;
    // A tool rewrote a file the engine reads back (or has unknown outputs), so
    // the engine must run again.
    bool outputsChanged = false;
    // What changed, for rerun reasons: "biber rewrote main.bbl".
    std::vector<std::string> changes;
    std::optional<RunResult<ToolInvocation>> failure;
    std::string failedToolName;
    std::optional<std::error_code> snapshotError;
    // Warnings from the tools that ran successfully (e.g. bibtex's
    // "I didn't find a database entry").
    std::vector<Diagnostic> diagnostics;
};

// Rule name for an auxiliary tool scoped by job name so two documents built in
// the same directory never clobber each other's snapshots.
std::string toolRuleName(std::string_view toolName, std::string_view jobName);

// Builds the ToolInvocation for one tool given the active EngineInvocation.
ToolInvocation makeToolInvocation(const EngineInvocation& inv, const ITool& tool);

// Plans, waves, executes, and records all applicable stale auxiliary tools for
// one build step.
class ToolPass {
public:
    struct Deps {
        StateDatabase& db;
        FileDigests& digests;
        const ProcessRunner& runner;
        const WaitLimits& limits;
        const BuildObserver& observer;
        std::function<std::int64_t()> clock;
    };

    ToolPass(Deps deps, const EngineInvocation& inv);

    // Runs every tool that applies and is stale, recording each run (inputs
    // consumed, outputs produced) as each wave completes. Stops after the first
    // wave that fails.
    ToolPassOutcome run(std::span<ITool* const> tools, const ToolContext& ctx);

private:
    struct PendingTool {
        ITool* tool = nullptr;
        std::vector<ToolInput> rawInputs;
        Digests inputs;
        std::unordered_set<std::string> inputKeys;
        std::vector<std::filesystem::path> outputs;
        std::vector<std::optional<Hash>> before;
        std::unordered_set<std::string> outputKeys;
        std::vector<std::string> args;
        ToolInvocation inv;
    };

    std::expected<bool, std::error_code> isStale(const ITool& tool, const Digests& inputs);
    std::expected<std::vector<PendingTool>, std::error_code> planStale(
        std::span<ITool* const> tools, const ToolContext& ctx);
    std::expected<void, std::error_code> refreshWaveInputs(
        std::span<PendingTool> pending, std::span<const std::size_t> wave);
    std::vector<RunResult<ToolInvocation>> executeWave(
        std::span<const PendingTool> pending,
        std::span<const std::size_t> wave,
        const std::vector<std::pair<std::string, std::string>>& toolEnv);
    std::expected<void, std::error_code> recordWaveOutcomes(
        std::span<PendingTool> pending,
        std::span<const std::size_t> wave,
        std::vector<RunResult<ToolInvocation>>& results,
        ToolPassOutcome& outcome);
    std::expected<void, std::error_code> recordSuccess(
        PendingTool& item,
        const RunResult<ToolInvocation>& toolResult,
        ToolPassOutcome& outcome);
    void recordFailure(
        const PendingTool& item,
        RunResult<ToolInvocation> toolResult,
        ToolPassOutcome& outcome);
    void notify(const BuildEvent& event) const;
    std::int64_t nowEpoch() const;

    Deps m_deps;
    const EngineInvocation& m_inv;
};

} // namespace maketex::driver
