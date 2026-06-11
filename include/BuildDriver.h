#pragma once

#include "Diagnostic.h"
#include "EngineInvocation.h"
#include "IEngine.h"
#include "RunResult.h"
#include "StateDatabase.h"
#include "ToolInvocation.h"
#include "graph/BuildGraph.h"

#include <filesystem>
#include <optional>
#include <string>
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

    // engine, db, hasher, and tools are non-owning — caller must ensure they
    // outlive the driver. Files under any ignorePrefix (e.g. the TeX
    // distribution roots) are excluded from dependency tracking entirely.
    BuildDriver(IEngine& engine,
        StateDatabase& db,
        IFileHasher& hasher,
        EngineInvocation inv,
        int maxPasses = kDefaultMaxPasses,
        std::vector<ITool*> tools = { },
        std::vector<std::filesystem::path> ignorePrefixes = { });

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
    void persistToolRules(FileStateTracker& tracker,
        const ToolContext& ctx,
        const std::vector<ITool*>& ranTools);

    // Writes every refreshed FileNode's state to the database so the next
    // invocation's (mtime, size) fast path avoids rehashing unchanged files.
    void persistFileStates();

    // Parse .fls and wire up FileNode/RuleNode edges in m_graph.
    void updateGraphFromFls(const std::filesystem::path& flsPath);

    // Returns the current hash of every Generated FileNode that exists on disk.
    std::unordered_map<std::string, Hash> takeSnapshot(FileStateTracker& tracker);

    // Pre-loop incremental check. Returns true if a prior .fls exists, every
    // recorded rule input still matches its stored hash, and the engine pass
    // can be skipped entirely. Falls through (returns false) on first build.
    bool tryIncrementalSkip(FileStateTracker& tracker);

    // Post-pass bookkeeping for a successful engine run: parse the new .fls,
    // refresh rule inputs from disk, and persist input hashes via markRan(true).
    void persistEnginePass(FileStateTracker& tracker, RuleNode& latexRule);

    // Iterates m_tools, running any that decide() Run.
    // Stops at the first failure; reports it via outcome.failure.
    ToolPassOutcome runAuxiliaryTools(ToolContext& ctx);

    IEngine& m_engine;
    StateDatabase& m_db;
    IFileHasher& m_hasher;
    EngineInvocation m_inv;
    int m_maxPasses;
    std::vector<ITool*> m_tools;
    std::vector<std::filesystem::path> m_ignorePrefixes;

    BuildGraph m_graph;
};

} // namespace maketex
