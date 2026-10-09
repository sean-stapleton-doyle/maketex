# maketex Architecture

maketex is a C++ replacement for latexmk. It compiles LaTeX documents by
driving a latex engine through a fixed-point loop, detecting convergence by
comparing BLAKE3 hashes of intermediate files rather than relying on log
parsing alone. Build failures and warnings are surfaced as structured
`Diagnostic` objects rather than raw log text.

## Layers

```
runBuild (Build.h)               — assembles everything from BuildOptions
    └── BuildDriver              — orchestrates the fixed-point loop
            ├── IEngine / EngineBase / {PdfLatex,XeLatex,LuaLatex}Engine
            │       └── Process  — posix_spawn subprocess
            ├── ITool / BibtexTool / BiberTool / HashTriggeredTool
            │       └── InputLocator — finds .bib/.bst like the tool will
            ├── DiagnosticExtractor  — RunResult<Inv> → vector<Diagnostic>
            ├── BuildGraph           — bipartite file↔rule dependency graph
            │       ├── FileNode
            │       └── RuleNode
            ├── StateDatabase        — SQLite persistence (file hashes, rule records)
            ├── FileStateTracker     — stat(2) fast-path, delegates hashing
            │       └── Blake3Hasher
            └── Parsers
                    ├── FlsParser    — -recorder output: what the engine read/wrote
                    ├── LogParser    — rerun requests, "No file …" lines
                    ├── AuxParser    — \bibdata/\citation across the \@input tree
                    └── BcfParser    — biblatex datasources
```

## Data flow for one compilation

```
1. BuildDriver::run()
2.   engineUpToDate?  (prior .fls; engine rule succeeded with the current
                       input hashes; PDF and read-back files exist)
3.     yes → run the stale tools only (e.g. after a .bib edit);
             done unless one rewrote a file latex reads (.bbl)
4.   loop (up to maxPasses):
5.     takeSnapshot(engine-written files, minus PDF/log/fls)
6.     IEngine::run() → RunResult<EngineInvocation>
7.       engine writes .fls  .log  .aux  .pdf
8.     updateGraphFromFls → wire FileNode↔RuleNode edges, hash inputs
9.     takeSnapshot again; did a read-back (Generated) file change?
10.    runStaleTools: each tool that applies and whose input digests
                      differ from its last recorded run
11.    rerun if: log asks, a read-back file changed, a "No file X" now
                 exists, a source was saved mid-pass, or a tool
                 rewrote one of its outputs
12.    otherwise converge: record the engine rule as succeeded
13.  not converged → record the engine rule as failed (next run rebuilds)
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
  extract(RunResult<EngineInvocation>) → vector<Diagnostic>
  extract(RunResult<ToolInvocation>)   → vector<Diagnostic>
  dispatches on kind

Diagnostic ────────────────────────────────────────────────────
  Severity severity    (Error | Warning | Info)
  string file          source .tex file; empty if unknown
  int line             1-based; -1 if unknown
  string message       clean, human-readable
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

The driver runs a tool when it applies and either has no successful run on
record, its input digests differ from those recorded for that run, or the
outputs that run produced were deleted or edited. The same rule decides
within a build and across invocations, so a prose edit reruns no tools and a
comment-only `.bib` edit reruns bibtex but no engine pass (its `.bbl` comes
out identical). Rules are stored as `tool:<name>:<job>` (inputs) and
`tool:<name>:<job>#outputs`.

## Diagnostic extraction

Two distinct extraction paths share the same `DiagnosticExtractor` interface:

- **On failure** (non-zero exit): `extractLatex` scans for `! ` error lines and
  looks ahead for `l.N` line numbers. The `! ` prefix is unique to TeX fatal
  errors. The `!  ==>` continuation line (two spaces) is filtered.

- **On success** (convergence): `extractLatexWarnings` scans for `LaTeX Warning:`
  and `Overfull \hbox` lines. Summary lines (`There were...`, `Label(s) may have
  changed`) are suppressed — they duplicate individual warnings or are already
  handled by `LogParser` as rerun signals.

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

On subsequent invocations the `(mtime, size)` fast-path reuses stored hashes
when nothing has changed on disk, avoiding rehashing unchanged files.

## Key design decisions

| Decision | Rationale |
|---|---|
| Hash-based convergence, not timestamp | Timestamps are unreliable across NFS, FAT, and cmake touch |
| `FileRole::Generated` = INPUT∩OUTPUT | OUTPUT-only files (pdf, log) have volatile content; only read-write intermediates are rerun triggers |
| `RunResult<Inv>` carries the invocation by value | Result is self-contained; `DiagnosticExtractor` has full invocation context without a raw pointer or extra argument |
| Tools describe, the driver schedules | One staleness rule (recorded input digests) for within-build and cross-invocation decisions; tools hold no mutable state |
| Engine rule recorded as succeeded only on convergence | A failed or non-converged build is never reported "up to date" next time |
| `BuildGraph` owns nodes via `unique_ptr` | Single ownership; `RuleNode` holds non-owning `FileNode*` |
| `StateDatabase` takes `unique_ptr<SQLite::Database>` | Allows in-memory db in tests without a separate interface |
| `posix_spawn` + `poll(2)` | Avoids `fork()` overhead and SIGPIPE hazards; captures stdout/stderr without threads |
| Synchronous execution | Engine and tool runs block the build thread; parallelism, if added, should come from the dependency graph |
| `-interaction=nonstopmode` not batchmode | Errors and warnings appear on stdout for in-memory parsing; batchmode would require disk reads of the `.log` file |
