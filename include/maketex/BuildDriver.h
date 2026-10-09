#pragma once

#include "maketex/BuildEvent.h"
#include "maketex/Diagnostic.h"
#include "maketex/EngineInvocation.h"
#include "maketex/IEngine.h"
#include "maketex/ITool.h"
#include "maketex/RunResult.h"
#include "maketex/StateDatabase.h"
#include "maketex/ToolContext.h"
#include "maketex/ToolInvocation.h"
#include "maketex/graph/BuildGraph.h"
#include "maketex/parsers/FlsParser.h"
#include "maketex/parsers/LogParser.h"

#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

namespace maketex {

class FileStateTracker;
class IFileHasher;

// Result of a complete BuildDriver run.
struct DriverResult {
    bool success = false;
    int passes = 0; // how many latex passes were run
    // Nothing needed doing: no engine pass and no tool run.
    bool upToDate = false;
    std::string lastError; // set on failure
    std::vector<Diagnostic> diagnostics; // extracted from engine/tool output
};

// Drives the full multi-pass latex compilation loop.
//
// Algorithm:
//   0. If the engine's recorded inputs are unchanged and its products
//      exist, only auxiliary tools whose inputs changed run (e.g. after a
//      .bib edit); if none ran or their outputs came out identical, the
//      build is done without an engine pass.
//   1. Run latex with -recorder to produce a .fls file.
//   2. Parse .fls → update BuildGraph (inputs/outputs).
//   3. Run each auxiliary tool that applies and whose inputs differ from
//      those its last successful run consumed (recorded in the database).
//   4. Re-run latex when a file it reads back changed during the pass, a
//      file it found missing now exists, the log asks for a rerun, a source
//      was edited mid-pass, or a tool rewrote one of its outputs.
//   5. Repeat until none of those hold (convergence) or maxPasses is
//      reached. Only a converged build is recorded as succeeded.
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
        // Applied to every engine and tool run: a per-process timeout and
        // the token that cancels the build.
        WaitLimits processLimits;
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
    // Outcome of running the stale tools once.
    struct ToolPassOutcome {
        bool ranAny = false;
        // A tool rewrote a file the engine reads back (or has unknown
        // outputs), so the engine must run again.
        bool outputsChanged = false;
        std::optional<RunResult<ToolInvocation>> failure;
        std::string failedToolName;
        std::optional<std::error_code> snapshotError;
        // Warnings from the tools that ran successfully (e.g. bibtex's
        // "I didn't find a database entry").
        std::vector<Diagnostic> diagnostics;
    };

    // Content digests keyed by weakly-canonical path string.
    using Digests = std::map<std::string, Hash>;

    // Rule identity must include the job name: the state database lives
    // per-directory, and two documents built in the same directory must not
    // share rule records (they would clobber each other's input snapshots).
    std::string engineRuleName() const;
    std::string toolRuleName(const ITool& tool) const;

    // Context handed to ITool's hooks, with lazy log/aux/fls parsing bound
    // to this invocation's output files.
    ToolContext makeToolContext() const;

    FlsParser makeFlsParser() const;

    // Writes every refreshed FileNode's state to the database so the next
    // invocation's (mtime, size) fast path avoids rehashing unchanged files.
    void persistFileStates();

    // Parse .fls and wire up FileNode/RuleNode edges in m_graph.
    void updateGraphFromFls(const std::filesystem::path& flsPath);

    // Hashes every engine-written file except final products (PDF, log, …)
    // — the files a following pass may read back. Propagates the first
    // snapshot error (E.7) instead of silently guessing.
    std::expected<Digests, std::error_code> takeSnapshot(FileStateTracker& tracker);

    // True when a prior .fls exists, the engine rule last succeeded with
    // exactly the current inputs, and its products are still on disk. Any
    // snapshot error answers false (conservative: a real pass re-surfaces
    // the error if it persists).
    bool engineUpToDate(FileStateTracker& tracker);

    // Digest of each tool input: file contents via the tracker, derived
    // inputs via the hasher. Missing files are omitted.
    std::expected<Digests, std::error_code> digestInputs(
        const std::vector<ToolInput>& inputs, FileStateTracker& tracker);

    // Current hash of path, or nullopt when it does not exist.
    std::expected<std::optional<Hash>, std::error_code> currentHash(
        const std::filesystem::path& path, FileStateTracker& tracker);

    // Whether tool must run given its current input digests: no successful
    // run on record, different inputs than that run consumed, or one of the
    // outputs it produced since deleted or modified.
    std::expected<bool, std::error_code> toolIsStale(const ITool& tool,
        const Digests& inputs, FileStateTracker& tracker);

    // Runs every tool that applies and is stale, recording each run (inputs
    // consumed, outputs produced) as it completes. Stops at the first
    // failure.
    ToolPassOutcome runStaleTools(const ToolContext& ctx, FileStateTracker& tracker);

    // Whether a file the pass reported missing ("No file main.toc.") now
    // exists, so the next pass would read it.
    bool missingInputAppeared(const LogRecord& log) const;

    // The main .tex file plus every source the engine read in the previous
    // pass — the inputs known to exist before the next pass starts.
    std::vector<std::filesystem::path> knownSources() const;

    // Whether one of sources was modified after passStart: its recorded
    // hash may not match what the pass compiled.
    bool sourceEditedDuringPass(const std::vector<std::filesystem::path>& sources,
        std::filesystem::file_time_type passStart) const;

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
    WaitLimits m_processLimits;

    BuildGraph m_graph;
};

} // namespace maketex
