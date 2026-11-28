#pragma once

#include "maketex/BuildEvent.h"
#include "maketex/Diagnostic.h"
#include "maketex/DiagnosticPolicy.h"
#include "maketex/EngineInvocation.h"
#include "maketex/IEngine.h"
#include "maketex/ITool.h"
#include "maketex/ProcessRunner.h"
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

namespace driver {
    class FileDigests;
    class ToolPass;
    struct ToolPassOutcome;
} // namespace driver

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
        DiagnosticPolicy diagnosticPolicy;
        // Computed lazily only when an engine pass is actually needed (skipped
        // entirely on up-to-date and tool-only runs).
        std::function<std::string()> preambleProvider;
        // Run pass 1 with -draftmode (pdflatex/lualatex) when the build is
        // cold and expected to take multiple passes. If pass 1 unexpectedly
        // converges, an automatic non-draft pass runs to produce the PDF.
        bool draftFirstPass = false;
        // For XeLaTeX: run every engine pass with -no-pdf (emitting .xdv) and
        // invoke xdvipdfmx once after convergence.
        bool xelatexXdvSplit = false;
        // Subprocess execution seam for auxiliary tools and xdvipdfmx. When
        // unset, spawns directly via maketex::Process.
        ProcessRunner processRunner;
        // Wall-clock epoch seconds for recorded rule timestamps. Defaults to
        // std::time(nullptr) when unset.
        std::function<std::int64_t()> clock;
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
    enum class PassStep { Rerun,
        Done };

    struct ConvergenceState {
        DriverResult result;
        std::vector<Diagnostic> toolWarnings;
    };

    // Rule identity must include the job name: the state database lives
    // per-directory, and two documents built in the same directory must not
    // share rule records (they would clobber each other's input snapshots).
    std::string engineRuleName() const;

    // Context handed to ITool's hooks, with lazy log/aux/fls parsing bound
    // to this invocation's output files.
    ToolContext makeToolContext() const;

    FlsParser makeFlsParser() const;
    FileStateTracker makeTracker() const;
    driver::ToolPass makeToolPass(driver::FileDigests& digests) const;

    // Writes every refreshed FileNode's state to the database so the next
    // invocation's (mtime, size) fast path avoids rehashing unchanged files.
    void persistFileStates();

    // Parse .fls and wire up FileNode/RuleNode edges in m_graph.
    void updateGraphFromFls(const std::filesystem::path& flsPath);

    // True when a prior .fls exists, the engine rule last succeeded with
    // exactly the current inputs, and its products are still on disk. Any
    // snapshot error answers false (conservative: a real pass re-surfaces
    // the error if it persists).
    bool engineUpToDate(FileStateTracker& tracker);

    // Handles the fast path where engine inputs are up to date: runs any stale
    // auxiliary tools, returning a final DriverResult unless a tool rewrote an
    // engine input (in which case nullopt signals that engine passes must run).
    std::optional<DriverResult> finishWithoutEnginePass(driver::FileDigests& digests);

    // Populates m_inv.preamble lazily from m_preambleProvider when unset.
    void ensurePreamble();

    // Runs one engine pass plus any stale tools, updating state and returning
    // whether another pass is required.
    PassStep runPass(int pass,
        FileStateTracker& tracker,
        driver::FileDigests& digests,
        RuleNode& latexRule,
        const std::string& command,
        ConvergenceState& state);

    // Updates the graph from the pass's .fls and refreshes every engine input.
    std::expected<void, std::string> refreshEngineInputs(
        RuleNode& latexRule, FileStateTracker& tracker);

    // Under XeLaTeX -no-pdf split, invokes xdvipdfmx once on the converged .xdv.
    // Returns false (having recorded the failure on state) if xdvipdfmx failed.
    bool finalizeXdv(RuleNode& latexRule,
        const std::string& command,
        ConvergenceState& state);

    // Applies diagnostic policy, records convergence, and persists file states.
    void finalizeConverged(const RunResult<EngineInvocation>& engineResult,
        RuleNode& latexRule,
        const std::string& command,
        ConvergenceState& state);

    static void recordToolFailure(DriverResult& result, driver::ToolPassOutcome& outcome);

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
    std::int64_t nowEpoch() const;

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
    DiagnosticPolicy m_diagnosticPolicy;
    std::function<std::string()> m_preambleProvider;
    bool m_draftFirstPass = false;
    bool m_xelatexXdvSplit = false;
    ProcessRunner m_processRunner;
    std::function<std::int64_t()> m_clock;

    BuildGraph m_graph;
};

} // namespace maketex
