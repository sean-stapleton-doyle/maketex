#pragma once

#include "maketex/BuildDriver.h"
#include "maketex/BuildEvent.h"
#include "maketex/DocumentProbe.h"
#include "maketex/EngineArguments.h"
#include "maketex/ToolFactory.h"
#include "maketex/engines/EngineFactory.h"
#include "maketex/tools/HashTriggeredTool.h"

#include <chrono>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace maketex {

// maketex's version, e.g. "0.1.0".
std::string_view version();

// TeX distribution roots (TEXMFROOT, system/user caches), resolved via
// kpsewhich. Files under these are excluded from dependency tracking — the
// engine reads hundreds of .sty/.cls/font files per pass, and treating them
// as inputs would mean hashing the whole distribution closure every build.
// Returns an empty list (track everything) when kpsewhich is unavailable.
std::vector<std::filesystem::path> texSystemRoots();

// Extra kpathsea search directories prepended to TEXINPUTS/BIBINPUTS/etc.
// Stored as strings so kpathsea's recursive "//" suffix is preserved verbatim.
struct SearchPaths {
    std::vector<std::string> texInputs;
    std::vector<std::string> bibInputs;
    std::vector<std::string> bstInputs;
    std::vector<std::string> indexStyle;
};

// Everything needed to run a complete build. The eventual CLI constructs
// this from flags and the user's config file; apps/main.cpp constructs it
// from flags only.
struct BuildOptions {
    std::filesystem::path texFile; // absolute path to the root .tex file

    // Explicit TeX job name (basename of .aux/.log/.pdf). Unset uses a
    // -jobname in engineArguments.extraOptions, else texFile.stem().
    std::optional<std::string> jobName;

    // The engine to drive; unset means detect it from the document (see
    // chooseEngine).
    std::optional<EngineKind> engine;
    EngineArguments engineArguments;

    // Auxiliary tools eligible to run between engine passes, in this order.
    std::vector<ToolKind> tools = {
        ToolKind::Biber,
        ToolKind::Makeindex,
        ToolKind::Nomencl,
        ToolKind::Makeglossaries,
        ToolKind::Bib2gls,
        ToolKind::Pythontex,
        ToolKind::Bibtex,
    };

    // User-defined auxiliary tools declared in maketex.toml ([[tool]]), run
    // after the built-in roster.
    std::vector<HashTriggeredTool::Recipe> customTools;

    // Extra kpathsea search directories and arbitrary environment variables
    // passed to the engine and auxiliary tools.
    SearchPaths searchPaths;
    std::vector<std::pair<std::string, std::string>> extraEnv;

    // Extra per-extension regex line filters (extension without leading dot ->
    // ECMAScript regex) appended to BuildDriver's built-in filters.
    std::vector<std::pair<std::string, std::string>> contentFilters;

    // Controls warning suppression and treating warnings as build failures.
    DiagnosticPolicy diagnosticPolicy;

    // Where the engine writes its outputs and where .maketex.db lives.
    // Empty means "<texdir>/.maketex" (created on demand by runBuild); the
    // final PDF (and .synctex.gz) is then published back beside the tex
    // file. An explicit directory keeps everything inside it.
    std::filesystem::path outputDir;

    static constexpr std::string_view kDefaultOutputDirName = ".maketex";

    // outputDir, or the hidden default next to texFile when unset.
    std::filesystem::path resolvedOutputDir() const
    {
        return outputDir.empty()
            ? texFile.parent_path() / kDefaultOutputDirName
            : outputDir;
    }

    // True when building into the hidden default — the mode that publishes
    // final artifacts back beside the tex file.
    bool usesDefaultOutputDir() const { return outputDir.empty(); }

    bool haltOnError = true;
    bool shellEscape = false;
    // On by default: editors use it for source↔PDF jumps, and it costs
    // little. Also keeps `maketex` and `maketex watch` on one fingerprint.
    bool synctex = true;
    int maxPasses = BuildDriver::kDefaultMaxPasses;

    // Receives progress events (pass/tool start and finish) on the calling
    // thread when set. See BuildEvent.h.
    BuildObserver observer;

    // Stops the build: the running engine or tool is terminated and
    // runBuild returns a failed result ("cancelled"). Watch mode uses it to
    // quit without waiting for the build to converge.
    std::stop_token stopToken;
    // Kill any single engine or tool run that takes longer than this (a
    // wedged shell-escape command, a tool waiting for input). Unset: no limit.
    std::optional<std::chrono::seconds> processTimeout;
};

// The engine a build of options would use: options.engine when set, else
// what the document asks for (detectEngine: "% !TeX program", then its
// preamble packages), else pdflatex.
EngineChoice chooseEngine(const BuildOptions& options);

// What runBuild did, on top of the driver's result.
struct BuildResult : DriverResult {
    EngineKind engine = EngineKind::PdfLaTeX; // the engine the build used
    // Why that engine, when it was detected rather than set: "loads
    // fontspec", "% !TeX program", … Empty for an explicit or default choice.
    std::string engineReason;
    // The detected engine failed and the build was redone with `engine`;
    // a caller building repeatedly (watch) should keep using it.
    bool retriedEngine = false;
    // Final artifacts at their user-facing location: the published copies
    // beside the tex file, or the PDF in an explicit output directory.
    // Empty unless the build succeeded.
    std::vector<std::filesystem::path> published;
};

// Assembles the full build — engine, state database, hasher, and the default
// auxiliary tool roster (biber, bibtex, makeindex, makeglossaries, pythontex)
// — and drives it to convergence. When the engine was detected as pdflatex
// and the build fails because the document needs a Unicode engine, it is
// retried once with that engine (an Info diagnostic says so). Environment
// failures (engine not installed, database errors) propagate as exceptions
// from the underlying components.
BuildResult runBuild(const BuildOptions& options);

// Copies jobName.pdf (and jobName.synctex.gz when present) from outDir to
// destDir via a temp file + rename, so readers never observe a half-written
// PDF. Returns the successfully published paths; no-op when the directories
// are the same. Publish failures (e.g. read-only destination) are skipped
// rather than thrown — the build has already succeeded; a missing artifact
// is reflected by its absence from the returned vector. Never throws.
std::vector<std::filesystem::path> publishArtifacts(
    const std::filesystem::path& outDir,
    const std::filesystem::path& destDir,
    const std::string& jobName);

// Classification of the last build's files, read from the engine's -recorder
// .fls output. Paths are absolute and exclude the TeX distribution roots.
struct BuildFileSets {
    // Read but never written by the engine: the document's true sources.
    std::vector<std::filesystem::path> sources;
    // Written by the engine, including read-back files like .aux.
    std::vector<std::filesystem::path> generated;
};

// The TeX job name a build of options uses: a -jobname among the engine's
// extra options, else the tex file's stem. Names the .aux/.log/.pdf files.
std::string jobNameFor(const BuildOptions& options);

// Where a build of options publishes its PDF and SyncTeX data beside the
// tex file — empty with an explicit output directory, which keeps them.
std::vector<std::filesystem::path> publishedArtifactPaths(const BuildOptions& options);

// The lock file a build of jobName holds in outDir for its duration, so
// two maketex processes building the same document take turns.
std::filesystem::path buildLockPath(const std::filesystem::path& outDir,
    const std::string& jobName);

// Scans the .fls left by the last build of options.texFile. Returns nullopt
// when no .fls exists (never built, or cleaned) or it cannot be parsed.
// Consumers: the CLI's watch mode (sources = the set to watch) and clean
// (generated = engine outputs to remove).
std::optional<BuildFileSets> scanBuildFiles(const BuildOptions& options);

} // namespace maketex
