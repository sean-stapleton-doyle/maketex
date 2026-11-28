# maketex Architecture

maketex is a C++ replacement for latexmk. It compiles LaTeX documents by
driving a latex engine through a fixed-point loop, detecting convergence by
comparing BLAKE3 hashes of intermediate files rather than relying on log
parsing alone. Build failures and warnings are surfaced as structured
`Diagnostic` objects rather than raw log text.

## Layers

```
runBuild (Build.h)                     — public facade & composition root
    ├── build::IBuildAttempts          — engine choice, remembered engine & one-shot Unicode retry
    ├── build/*                        — single-purpose build helpers & shells
    │       ├── Environment / JobName / Fingerprint / BuildHeuristics
    │       ├── TexSystemRoots / PreambleHooks / PreambleFormat
    │       ├── Publish / BuildFileSets / OutputDir / EngineMemory / BuildLock
    └── BuildDriver                    — orchestrates the fixed-point loop
            ├── driver/FileDigests     — graph-backed file & derived-input digests
            ├── driver/ToolPass        — stale-tool planning, wave execution & recording
            ├── driver/ToolWaves       — conflict detection & parallel wave grouping
            ├── driver/PassAnalysis    — read-back diffing, rerun reasons & failure messages
            ├── DiagnosticPolicy       — warning suppression & promotion rules
            ├── ProcessRunner          — subprocess launch seam (Launch.h → Process)
            ├── IEngine / EngineBase / {PdfLatex,XeLatex,LuaLatex}Engine
            ├── ITool / BibtexTool / BiberTool / HashTriggeredTool / …
            │       └── InputLocator   — finds .bib/.bst like the tool will
            ├── DiagnosticExtractor    — RunResult<Inv> → vector<Diagnostic>
            │       └── diagnostics/*  — SourceResolver, LogLines, Categorize, FileStack,
            │                            LatexErrors, LatexWarnings, ToolOutput
            ├── BuildGraph             — bipartite file↔rule dependency graph
            │       ├── FileNode
            │       └── RuleNode
            ├── StateDatabase          — SQLite persistence (file hashes, rule records)
            ├── FileStateTracker       — stat(2) fast-path, delegates hashing
            │       └── Blake3Hasher
            └── Parsers
                    ├── FlsParser      — -recorder output: what the engine read/wrote
                    ├── LogParser      — rerun requests, "No file …" lines
                    ├── AuxParser      — \bibdata/\citation across the \@input tree
                    └── BcfParser      — biblatex datasources
```

## Data flow for one compilation

```
1. runBuild(options) → build::runBuild(options, IBuildAttempts)
2.   chooseEngine (explicit, directive/preamble probe, or remembered Unicode engine)
3.   attempt → BuildDriver::run()
4.     engineUpToDate?  (prior .fls; engine rule succeeded with the current
                         input hashes; PDF and read-back files exist)
5.       yes → ToolPass::run() for stale tools only (e.g. after a .bib edit);
               done unless a tool rewrote a file latex reads (.bbl)
6.     loop (up to maxPasses):
7.       FileDigests::takeSnapshot(engine-written files, minus PDF/log/fls)
8.       IEngine::run() → RunResult<EngineInvocation>
9.         engine writes .fls  .log  .aux  .pdf
10.      updateGraphFromFls → wire FileNode↔RuleNode edges, hash inputs
11.      FileDigests::takeSnapshot again; driver::firstChangedReadBack()
12.      ToolPass::run(): plan stale tools → planToolWaves → execute waves → record runs
13.      driver::rerunReasons(): rerun if log asks, a read-back file changed,
           a "No file X" now exists, a source was saved mid-pass, a tool rewrote
           an output, or pass 1 ran in -draftmode without other rerun signals
14.      otherwise converge: optional xdvipdfmx split → applyDiagnosticPolicy →
           record engine rule as succeeded and persist file states
15.    not converged → record engine rule as failed (next run rebuilds)
16.  if guessed pdflatex failed needing a Unicode engine → discard partial .aux,
     retry once with LuaLaTeX/XeLaTeX, and remember it on success
```

## Result types

```
RunResult<Inv> ────────────────────────────────────────────────
  Inv invocation       ← EngineInvocation or ToolInvocation, by value
  InvokerKind kind     ← stamped by launchProcess
  int exitCode
  string stdoutOutput
  string stderrOutput

DiagnosticExtractor ───────────────────────────────────────────
  extract(RunResult<EngineInvocation> [, ISourceResolver]) → vector<Diagnostic>
  extract(RunResult<ToolInvocation>)                       → vector<Diagnostic>
  dispatches on kind via diagnostics/{LatexErrors,LatexWarnings,ToolOutput}

Diagnostic ────────────────────────────────────────────────────
  Severity severity    (Error | Warning | Info)
  string file          source .tex file; empty if unknown
  int line             1-based; -1 if unknown
  string message       clean, human-readable
  string category      machine-matchable tag (for DiagnosticPolicy)
  string rawContext    raw log excerpt; shown only with --verbose
```

## Convergence detection

Files that appear in **both** the INPUT and OUTPUT sections of the `.fls`
file (e.g. `.aux`, `.toc`, `.out`) are classified `FileRole::Generated`: the
engine reads back what it wrote, so a change means another pass. Files that
are OUTPUT-only (`.idx`, `.bcf`) are `FileRole::Output`; they matter only to
the tools that consume them. The snapshot taken before a pass covers both
roles (a `.toc` written in pass 1 is read in pass 2), but only Generated
files are compared, and only those that existed before the pass.

A file written for the first time is not by itself a change: every cold run
creates a `.aux`, and LaTeX checks the effect of that itself ("Label(s) may
have changed"). A file the engine looked for and missed — `No file main.toc.`
in the log — that exists after the pass does trigger a rerun. Together these
make a plain document one pass and a `\tableofcontents` document two.

## Auxiliary tools

A tool is a description: `applies` (did the latest pass write its trigger —
the `.bcf`, `.idx` — or, for bibtex, does the `.aux` tree have `\bibdata`),
`inputs` (trigger files, databases, and derived digests such as the
`\citation`/`\bibdata`/`\bibstyle` lines of the `.aux`), and `outputs` (the
`.bbl`, `.ind` the engine reads back).

`driver::ToolPass` runs a tool when it applies and either has no successful
run on record, its input digests differ from those recorded for that run, or
the outputs that run produced were deleted or edited. Independent tools with
known, non-overlapping footprints are grouped into parallel waves by
`driver::planToolWaves` and recorded deterministically in roster order. The
same staleness rule decides within a build and across invocations, so a prose
edit reruns no tools and a comment-only `.bib` edit reruns bibtex but no
engine pass (its `.bbl` comes out identical). Rules are stored as
`tool:<name>:<job>` (inputs) and `tool:<name>:<job>#outputs`.

## Diagnostic extraction

`DiagnosticExtractor` dispatches on `InvokerKind` to focused parsers under
`maketex/diagnostics/`:

- **On failure** (non-zero exit): `diagnostics::extractLatexErrors` parses
  `-file-line-error` (`file:line: message`) and fallback `! ` / engine error
  lines, folds continuation lines, attributes files via `FileStack` +
  `ISourceResolver`, and refines noise / missing-file locations.

- **On success** (convergence): `diagnostics::extractLatexWarnings` runs a
  table of line matchers (`LaTeX Warning:`, `Package`/`Class`/`Font` warnings,
  `Missing character:`, `pdfTeX warning`, `Overfull`/`Underfull` boxes) while
  suppressing summary lines (`There were...`, `Label(s) may have changed`).

- **Auxiliary tools**: `diagnostics::extractToolOutput` runs dedicated BibTeX
  parsing or constexpr `LinePattern` tables for Biber, MakeIndex,
  makeglossaries, bib2gls, PythonTeX, xdvipdfmx, and custom tools.

`pdflatex` runs with `-interaction=nonstopmode` (not `batchmode`) so errors and
warnings appear on stdout for in-memory capture. `batchmode` suppresses all
terminal output, which would require reading the `.log` file from disk instead.

## Working directory

Engines and tools run from a specific working directory set via
`posix_spawn_file_actions_addchdir`:

- **Engines** run from the `.tex` file's parent directory so that
  `\input{subdir/file}` resolves correctly regardless of maketex's own cwd.
- **BibtexTool** runs from `outputDir`. TeX Live's `openout_any=p` security
  setting rejects absolute paths for output files; the bare job name (no path)
  only works when bibtex's cwd is the output directory.
- **BiberTool**, makeindex, makeglossaries, and pythontex also run from
  `outputDir`. Every tool gets `BIBINPUTS`, `BSTINPUTS`, `TEXINPUTS`, and
  `INDEXSTYLE` with the document directory prepended to the user's own value
  (`toolSearchEnv`), so source-adjacent `.bib`/`.bst`/`.ist` files resolve.

## Persistence

A single SQLite database (`.maketex.db` in the output directory) stores:

- `file_states` — mtime, size, BLAKE3 hash for each tracked path
- `rules` — last command, timestamp, and success flag per rule name
- `rule_inputs` — hash of each input file at the time the rule last ran
- `meta` — schema version, per-job build-option fingerprints, and per-job
  metadata (remembered Unicode engine, preamble format key, publish stamp)

On subsequent invocations the `(mtime, size)` fast-path reuses stored hashes
when nothing has changed on disk, avoiding rehashing unchanged files.

## Key design decisions

| Decision | Rationale |
|---|---|
| Hash-based convergence, not timestamp | Timestamps are unreliable across NFS, FAT, and cmake touch |
| `FileRole::Generated` = INPUT∩OUTPUT | OUTPUT-only files (pdf, log) have volatile content; only read-write intermediates are rerun triggers |
| `RunResult<Inv>` carries the invocation by value | Result is self-contained; `DiagnosticExtractor` has full invocation context without a raw pointer or extra argument |
| Tools describe, `ToolPass` schedules | One staleness rule (recorded input digests) for within-build and cross-invocation decisions; tools hold no mutable state |
| Engine rule recorded as succeeded only on convergence | A failed or non-converged build is never reported "up to date" next time |
| `BuildGraph` owns nodes via `unique_ptr` | Single ownership; `RuleNode` holds non-owning `FileNode*` |
| `StateDatabase` takes `unique_ptr<SQLite::Database>` | Allows in-memory db in tests without a separate interface |
| `ProcessRunner` + `EnvLookup` + `ISourceResolver` + `IBuildAttempts` seams | Hybrid DI (pure functions + small virtual interfaces + `std::function` hooks) enables fast, deterministic unit tests without spawning subprocesses or mutating environment variables |
| `posix_spawn` + `poll(2)` | Avoids `fork()` overhead and SIGPIPE hazards; captures stdout/stderr without threads |
| Children get `/dev/null` stdin, a timeout, and a stop token | TeX never waits on a terminal; a wedged tool is killed (SIGTERM, then SIGKILL); watch mode's quit interrupts a running pass |
| A missing program is a `RunResult`, not an exception | "biber is not installed" surfaces as a `missing-tool` diagnostic with an install hint instead of aborting the build |
| One flock per document per output directory | A watch session and a manual build of the same document take turns instead of interleaving writes |
| `-interaction=nonstopmode` not batchmode | Errors and warnings appear on stdout for in-memory parsing; batchmode would require disk reads of the `.log` file |
