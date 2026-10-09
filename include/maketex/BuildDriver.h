#pragma once

#include "maketex/BuildEvent.h"
#include "maketex/Diagnostic.h"
#include "maketex/EngineInvocation.h"
#include "maketex/IEngine.h"
#include "maketex/RunResult.h"
#include "maketex/StateDatabase.h"
#include "maketex/ToolInvocation.h"
#include "maketex/graph/BuildGraph.h"

#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace maketex {

class FileStateTracker;
class IFileHasher;
class ITool;
class RuleNode;
class ToolContext;

// Result of a complete BuildDriver run.
struct DriverResult {
    bool success = false;
    int passes = 0; // how many latex passes were run
    std::string lastError; // set on failure
    std::vector<Diagnostic> diagnostics; // extracted from engine/tool output
};

// Drives the full multi-pass latex compilation loop.
//
// Algorithm:
//   1. Run latex with -recorder to produce a .fls file.
//   2. Parse .fls → update BuildGraph (inputs/outputs).
//   3. Snapshot hashes of all Generated files.
//   4. Run any auxiliary tools whose inputs have changed.
//   5. Re-run latex.
//   6. Repeat until no Generated file changed between passes (convergence)
//      or maxPasses is reached.
class BuildDriver {
public:
    static constexpr int kDefaultMaxPasses = 8;

    // Optional configuration. Kept in a struct so the BuildDriver constructor
    // stays well under I.23's four-parameter guideline and the two
    // vector<> fields (tools, ignorePrefixes) can't be silently swapped.
    struct Config {
        int maxPasses = kDefaultMaxPasses;
        std::vector<ITool*> tools;
        std::vector<std::filesystem::path> ignorePrefixes;
        BuildObserver observer;
    };

    // engine, db, and hasher are non-owning — caller must ensure they
    // outlive the driver. Files under any ignorePrefix (e.g. the TeX
    // distribution roots) are excluded from dependency tracking entirely.
    // observer, when set, receives progress events (see BuildEvent.h) on the
    // calling thread.
    BuildDriver(IEngine& engine,
        StateDatabase& db,
        IFileHasher& hasher,
        EngineInvocation inv,
        Config config);

    // Convenience overload: collaborators only, default configuration.
    BuildDriver(IEngine& engine,
        StateDatabase& db,
        IFileHasher& hasher,
        EngineInvocation inv);

    DriverResult run();

private:
    // Outcome of one pass through m_tools.
    struct ToolPassOutcome {
        bool filesChanged = false;
        std::optional<RunResult<ToolInvocation>> failure;
        std::string failedToolName;
        std::vector<ITool*> ranTools; // tools that executed successfully
    };

    // Rule identity must include the job name: the state database lives
    // per-directory, and two documents built in the same directory must not
    // share rule records (they would clobber each other's input snapshots).
    std::string engineRuleName() const;
    std::string toolRuleName(const ITool& tool) const;

    // Context handed to ITool::decide()/trackedInputs(), with lazy log/aux
    // parsing bound to this invocation's output files.
    ToolContext makeToolContext() const;

    // Persists input hashes for every tool that ran this build, using
    // post-convergence file contents, so the next invocation's incremental
    // skip can verify tool inputs (e.g. .bib files) are unchanged.
    std::expected<void, std::error_code> persistToolRules(FileStateTracker& tracker,
        const ToolContext& ctx,
        const std::vector<ITool*>& ranTools);

    // Writes every refreshed FileNode's state to the database so the next
    // invocation's (mtime, size) fast path avoids rehashing unchanged files.
    void persistFileStates();

    // Parse .fls and wire up FileNode/RuleNode edges in m_graph.
    void updateGraphFromFls(const std::filesystem::path& flsPath);

    // Returns the current hash of every Generated FileNode that exists on disk.
    // Propagates the first snapshot error (E.7) instead of silently guessing.
    std::expected<std::unordered_map<std::string, Hash>, std::error_code>
    takeSnapshot(FileStateTracker& tracker);

    // Pre-loop incremental check. Returns true if a prior .fls exists, every
    // recorded rule input still matches its stored hash, and the engine pass
    // can be skipped entirely. Falls through (returns false) on first build
    // or on any snapshot error (conservative: forces a real build pass, which
    // re-surfaces the error if it persists).
    bool tryIncrementalSkip(FileStateTracker& tracker);

    // Post-pass bookkeeping for a successful engine run: parse the new .fls,
    // refresh rule inputs from disk, and persist input hashes via markRan(true).
    std::expected<void, std::error_code> persistEnginePass(
        FileStateTracker& tracker, RuleNode& latexRule);

    // Iterates m_tools, running any that decide() Run.
    // Stops at the first failure; reports it via outcome.failure.
    ToolPassOutcome runAuxiliaryTools(ToolContext& ctx);

    // Forwards event to m_observer when one is set.
    void notify(const BuildEvent& event) const;

    IEngine& m_engine;
    StateDatabase& m_db;
    IFileHasher& m_hasher;
    EngineInvocation m_inv;
    const int m_maxPasses;
    std::vector<ITool*> m_tools;
    std::vector<std::filesystem::path> m_ignorePrefixes;
    BuildObserver m_observer;

    BuildGraph m_graph;
};

} // namespace maketex
