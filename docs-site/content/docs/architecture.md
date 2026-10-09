+++
title = 'Architecture'
date = '2026-06-11T00:00:00Z'
+++

# maketex Architecture

maketex is a C++23 replacement for latexmk. It compiles LaTeX documents by
driving a latex engine through a fixed-point loop, detecting convergence by
comparing BLAKE3 hashes of intermediate files rather than relying on log
parsing alone. Build failures and warnings are surfaced as structured
`Diagnostic` objects rather than raw log text.

## Layers

```
main.cpp
    └── BuildDriver              — orchestrates the fixed-point loop
            ├── IEngine / EngineBase / PdfLatexEngine
            │       └── Process  — posix_spawn subprocess
            ├── ToolBase / BiberTool / BibtexTool
            │       └── Process
            ├── DiagnosticExtractor  — RunResult<Inv> → vector<Diagnostic>
            ├── BuildGraph           — bipartite file↔rule dependency graph
            │       ├── FileNode
            │       └── RuleNode
            ├── StateDatabase        — SQLite persistence (file hashes, rule records)
            ├── FileStateTracker     — stat(2) fast-path, delegates hashing
            │       └── Blake3Hasher
            └── Parsers
                    ├── FlsParser    — pdflatex -recorder output
                    ├── LogParser    — rerun signals from .log
                    └── AuxParser    — citations, bibdata from .aux
```

## Data flow for one compilation

```
1. BuildDriver::run()
2.   [if .fls exists from prior run] updateGraphFromFls → pre-populate graph
3.   loop (up to maxPasses):
4.     takeSnapshot(Generated files before)
5.     IEngine::run() → future<RunResult<EngineInvocation>>
6.       pdflatex writes .fls  .log  .aux  .pdf
7.     RuleNode::markRan() → StateDatabase persists input hashes
8.     updateGraphFromFls → wire FileNode↔RuleNode edges
9.     takeSnapshot(Generated files after)
10.    LogParser::parse(.log) → rerunNeeded? biberRerunNeeded?
11.   if biberRerunNeeded: BiberTool::run() → future<RunResult<ToolInvocation>>
12.   if .aux has \bibdata (first time): BibtexTool::run()
13.   if before==after && !rerunNeeded → converge:
14.     DiagnosticExtractor::extract(engineResult) → warnings
15.     return DriverResult { success=true, diagnostics }
```

## Result type system

```
InvokerKind  ─────────────────────────────────────────────────
  PdfLatex | XeLaTeX | LuaLaTeX | Bibtex | Biber | Makeindex

Invocation<T> concept ────────────────────────────────────────
  requires { inv.kind: InvokerKind, inv.outputDir: path }
  satisfied by: EngineInvocation, ToolInvocation

RunResult<Inv> ────────────────────────────────────────────────
  shared_ptr<const Inv> invocation    ← self-contained context
  int exitCode
  string stdoutOutput
  string stderrOutput

DiagnosticExtractor ───────────────────────────────────────────
  extract(RunResult<EngineInvocation>) → vector<Diagnostic>
  extract(RunResult<ToolInvocation>)   → vector<Diagnostic>
  dispatches on invocation->kind

Diagnostic ────────────────────────────────────────────────────
  Severity severity    (Error | Warning | Info)
  string file          source .tex file; empty if unknown
  int line             1-based; -1 if unknown
  string message       clean, human-readable
  string rawContext    raw log excerpt; shown only with --verbose
```

## Convergence detection

Only files that appear in **both** the INPUT and OUTPUT sections of the `.fls`
file (e.g. `.aux`, `.toc`, `.lot`) are classified `FileRole::Generated` and
included in the convergence snapshot. Files that are OUTPUT-only (`.pdf`,
`.log`) are classified `FileRole::Output` and excluded — their content changes
every run due to timestamps.

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
- **BiberTool** uses `--input-directory` and `--output-directory` flags instead
  of relying on cwd.

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
| `RunResult<Inv>` carries `shared_ptr<const Inv>` | Result is self-contained; `DiagnosticExtractor` has full invocation context without a raw pointer or extra argument |
| `Invocation<T>` concept not inheritance | Avoids virtual dispatch on value types; both `EngineInvocation` and `ToolInvocation` satisfy it structurally |
| `ToolBase::run()` takes invocation by value | Lets `invokerKind()` be stamped on the invocation before the `shared_ptr` is created; callers (e.g. `BuildDriver`) don't need to know the kind |
| `BuildGraph` owns nodes via `unique_ptr` | Single ownership; `RuleNode` holds non-owning `FileNode*` |
| `StateDatabase` takes `unique_ptr<SQLite::Database>` | Allows in-memory db in tests without a separate interface |
| `posix_spawn` + `poll(2)` | Avoids `fork()` overhead and SIGPIPE hazards; captures stdout/stderr without threads |
| Taskflow executor for async engine/tool dispatch | Engine and tool runs on a worker thread; caller `.get()`s the future |
| `-interaction=nonstopmode` not batchmode | Errors and warnings appear on stdout for in-memory parsing; batchmode would require disk reads of the `.log` file |
