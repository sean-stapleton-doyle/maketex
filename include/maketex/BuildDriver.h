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

#include <cstdint>
#include <expected>
#include <filesystem>
#include <map>
#include <optional>
#include <regex>
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
    // Why each extra engine pass ran, in order: "pass 2: main.aux changed",
    // "pass 3: biber rewrote main.bbl", …
    std::vector<std::string> rerunReasons;
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
        // Lines to leave out when hashing files with a given extension, for
        // content that changes without meaning anything to the next pass:
        // - the %%CreationDate stamp regenerated EPS figures carry;
        // - \@writefile lines in .aux files, which LaTeX skips when it reads
        //   the .aux (they reach the next pass only through the .toc/.lof
        //   files, which are tracked themselves). Without this, adding a
        //   section costs a pass even in a document with no contents list.
        std::vector<std::pair<std::string, std::regex>> contentFilters {
            { "eps", std::regex { R"(^%%CreationDate: )" } },
            { "aux", std::regex { R"(^\\@writefile\{)" } },
        };
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
        // What changed, for rerun reasons: "biber rewrote main.bbl".
        std::vector<std::string> changes;
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

    // A file the pass reported missing ("No file main.toc.") that now
    // exists, so the next pass would read it; nullopt when there is none.
    std::optional<std::string> appearedInput(const LogRecord& log) const;

    // (mtime, size) of a file, or nullopt when it can't be stat'ed.
    using Stamp = std::pair<std::filesystem::file_time_type, std::uintmax_t>;
    using Stamps = std::map<std::filesystem::path, std::optional<Stamp>>;

    // Stamps of the main .tex file and every source the engine read in the
    // previous pass — the inputs known to exist before the next pass starts.
    Stamps stampKnownSources() const;

    // A source whose stamp changed between `before` and now: it was
    // saved while the pass ran, so its recorded hash may not match what the
    // pass compiled. Comparing a file with itself, not with a clock, keeps
    // this right on filesystems whose timestamps lag the system clock
    // (Linux stamps files with a coarse kernel clock).
    // The first such source, or nullopt.
    static std::optional<std::filesystem::path> editedDuringPass(const Stamps& before);

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
    std::vector<std::pair<std::string, std::regex>> m_contentFilters;

    BuildGraph m_graph;
};

} // namespace maketex
