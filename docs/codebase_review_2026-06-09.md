# Codebase Review: maketex

## Context

`maketex` is a C++23 (CMake declares C++26) replacement for `latexmk`. It drives LaTeX engines (pdflatex, …) and helper tools (bibtex, biber, makeindex, …) to convergence, tracking file hashes via Blake3 and persisting state in SQLite-style `StateDatabase`. ~4.7k LOC across `include/` and `src/`, with a parallel `test/` tree using GoogleTest. The review focuses on architecture, separation of concerns, design patterns, and abstraction quality — not on bugs or features.

Overall the project is in good shape for its size: modular layout (`engines/`, `tools/`, `parsers/`, `graph/`), real interfaces (`IEngine`, `IFileHasher`), concept-based generics, modern CMake with CPM. The issues below are the gap between "tidy" and "well-architected."

---

## Critical findings

### C1. Tool/Engine abstraction is asymmetric — there is no `ITool` ✅ DONE (2026-06-09)

**Original finding.** `IEngine.h:12-17` defined a proper concept; `EngineBase` was a thin virtual base over it. On the tool side there was **only** `ToolBase`: a concrete base mixing framework concerns (taskflow executor, templated `run()`) with abstract hooks. Tools are heterogeneous (bibtex vs. biber vs. makeindex), so this is exactly the case where an interface earns its keep.

**Resolution.** Introduced `include/ITool.h` (concept + `ITool` abstract class) mirroring `IEngine`. Execution moved to a new `ToolRunner` (`include/ToolRunner.h`, `src/ToolRunner.cpp`) that composes an `ITool&` with a `tf::Executor`. `BibtexTool` now inherits `ITool` directly and no longer carries an executor. `BuildDriver` holds `std::vector<ITool*>` plus a non-owning `ToolRunner*`; constructor asserts the runner is present when tools are. The post-construction `inv.kind = invokerKind()` mutation that lived in `ToolBase::run` is gone — `ToolRunner::run` stamps `tool.invokerKind()` once before launch. `ToolBase.h`/`.cpp` deleted. All 125 tests pass.

### C2. Three competing invocation models ✅ DONE (2026-06-09)

**Original finding.** Four overlapping ideas — `EngineInvocation`, `ToolInvocation` (silently-wrong `Bibtex` default), `ToolSpec`, and the `Invocation` concept — plus two parallel dispatch paths in `BuildDriver::run` (`runSpec` static vs. `tool->run` loop). `ToolBase::run` post-mutated `inv.kind`.

**Resolution.**

- **`kind` moved off invocations onto `RunResult`.** `RunResult<Inv>` now carries `InvokerKind kind` (`include/RunResult.h`). `EngineInvocation` and `ToolInvocation` no longer have a `kind` field. The `Invocation` concept and `include/Invocation.h` are deleted.
- **`IEngine` gained `invokerKind()`** to mirror `ITool`. `EngineBase::run` and `ToolRunner::run` stamp `result.kind = invokerKind()` once before delivery. `DiagnosticExtractor` reads `result.kind` (no more `invocation->kind`).
- **`ToolSpec` deleted.** Spec-style tools are now real `ITool`s: `HashTriggeredTool` (`include/tools/HashTriggeredTool.h`, `src/tools/HashTriggeredTool.cpp`) implements the trigger-on-hash-change + `runOnce` + `$job` substitution logic.
- **`ToolFactory` introduced** (`include/ToolFactory.h`, `src/ToolFactory.cpp`) — holds the `Blake3Hasher&` dependency; exposes `hashTriggered(...)` and `bibtex()` returning `unique_ptr<ITool>`. `main.cpp` is now a clean list of factory calls.
- **`BuildDriver::run` has one tool loop.** The `runSpec` static, `m_specs` field, and the `specs` constructor parameter are gone.
- **`ITool::decide` is now non-const** — tools may stash scheduling state (a freshly-hashed trigger value) for `onSuccess` to commit. `mutable` smell avoided.

All 125 tests pass. `BuildDriver_test`'s old spec-based biber test is replaced by `hashTriggeredToolFiresOnBcfChange`, which exercises the same flow through `ToolFactory` + `HashTriggeredTool`.

### C3. `BuildGraph` is built but not used to drive execution ✅ DONE (2026-06-09)

**Original finding.** Two parallel staleness systems answering different questions: (1) the graph (`RuleNode::isStale` + per-rule input hashes in SQLite) for incremental skip, and (2) `BuildDriver::takeSnapshot` snapshot-diff for fixed-point convergence within a build. The graph machinery was built, tested in unit tests, and never consulted by the driver — `isStale` had zero callers outside `RuleNode_test`.

**Resolution.** Wired `isStale` into `BuildDriver::run` as an incremental skip pre-check:

- Before the convergence loop, if `.fls` exists, build the graph, refresh each rule input from disk, and call `latexRule.isStale(m_db)`. If false, return success with `passes = 0` — second invocation with no edits does zero work.
- Reordered `markRan` to fire *after* `updateGraphFromFls`, and added a refresh of all rule inputs before `markRan`. Previously `Source` inputs were never refreshed (only `Generated` were, via `takeSnapshot`), so their state.exists was false and `markRan` persisted zero input hashes — making `isStale` always return true (new input never recorded). Fix is one extra refresh loop.
- Snapshot-diff is kept as-is for convergence within a build; the two systems now do distinct jobs.

Two new tests in `BuildDriver_test`: `secondInvocationWithNoChangesSkips` (passes=0, engine.callCount=0) and `secondInvocationRunsWhenSourceChanges` (passes=1 after editing .tex). All 127 tests pass.

### C4. `BuildDriver` has three responsibilities ✅ DONE (2026-06-09)

**Original finding.** `BuildDriver::run` was ~90 lines covering convergence loop, graph maintenance, and tool dispatch.

**Resolution.** Extracted three private methods, leaving `run()` as a ~50-line orchestrator that reads top-to-bottom:

- `tryIncrementalSkip(tracker) → bool` — pre-loop isStale check.
- `persistEnginePass(tracker, latexRule)` — updateGraphFromFls + refresh inputs + markRan(true).
- `runAuxiliaryTools(ctx) → ToolPassOutcome` — iterate tools, collect filesChanged / failure.

`ToolPassOutcome` is a private nested struct on `BuildDriver`. Did not push to separate classes (`ConvergenceLoop`, etc.) — at current size the cost (more types, more wiring) exceeds the benefit (testability of convergence policy in isolation). Revisit if the loop policy grows.

---

## Important findings

### I1. `ToolContext` couples tools to the parsing layer ✅ DONE (2026-06-09)

**Original finding.** `ToolContext.h` was a POD struct including `AuxParser.h`/`LogParser.h` and exposing parsed records as references. Every tool TU transitively pulled in the parser layer even when it didn't read log/aux.

**Resolution.** `ToolContext` is now a class with lazy `log()`/`aux()` accessors backed by `std::function<LogRecord()>` / `std::function<AuxRecord()>` sources supplied at construction time. The header forward-declares `LogRecord`/`AuxRecord` and stores them via `std::unique_ptr<incomplete>`; the destructor is defined in `src/ToolContext.cpp` where the records are complete types. Parsing happens once on first access and is cached. Tools that don't read log/aux pay zero parsing cost; tool TUs no longer transitively include the parser headers.

`BuildDriver::run` constructs the context with two lambdas (one for the log path, one for the aux file with the existence guard preserved). The convergence check reads `ctx.log().rerunNeeded` — same cache. All 127 tests pass.

### I2. Parsers use private inheritance from a non-polymorphic utility ✅ DONE (2026-06-09)

**Original finding.** `AuxParser`, `LogParser`, `FlsParser` all `private`-inherited `TextFileParser`, a non-polymorphic class whose only member was a single `static` function taking `const std::function<void(std::string_view)>&` — every line of a multi-thousand-line `.log` paid a `std::function` indirection.

**Resolution.** `TextFileParser` is now a header-only function template `maketex::detail::forEachLine` in `include/parsers/TextFileParser.h`. The `: private TextFileParser` clauses are gone from all three parsers. Call sites became `detail::forEachLine(path, [&](auto line) {...})` — the lambda monomorphizes into the parser's TU and inlines fully. `src/parsers/TextFileParser.cpp` deleted; CMakeLists updated.

All 127 tests pass.

### I3. Inconsistent error model ✅ DONE (2026-06-09)

**Original finding.** Boundary code threw exceptions (`Blake3Hasher`, parsers via `TextFileParser`, `FileStateTracker`), execution code returned codes (`Process::wait`, `RunResult::success()`), and `DiagnosticExtractor` silently swallowed `std::stoi` failures.

**Resolution.** Adopted `std::expected<T, std::error_code>` for the "file-might-be-unreadable" boundary; left exceptions in place for system-level failures (Process spawn/pipe) and programming errors (corrupt schema in `StateDatabase`); replaced the silent stoi catches.

Converted to `std::expected<T, std::error_code>`:

- `IFileHasher::hashFile` / `Blake3Hasher::hashFile` → `expected<Hash, error_code>`
- `detail::forEachLine` → `expected<void, error_code>`
- `LogParser::parse` / `AuxParser::parse` / `FlsParser::parse` → `expected<Record, error_code>`
- `FileStateTracker::snapshot` → `expected<FileState, error_code>`. ENOENT now uniformly returns `FileState::absent` as the success value; other system errors flow through the error channel.

Callers:
- `FileNode::refresh` keeps cached state on error.
- `BuildDriver::updateGraphFromFls` early-returns on parse failure.
- `BuildDriver::run` ToolContext lambdas use `.value_or(LogRecord{})` / `.value_or(AuxRecord{})` to preserve "no signal on unreadable" semantics.
- `HashTriggeredTool::decide` returns `RunDecision::Skip` when the trigger file can't be hashed.

`DiagnosticExtractor`: extracted `tryParseInt(string_view) → std::optional<int>` using `std::from_chars`; replaced two `try { stoi } catch (...) {}` sites.

Per-conversation rule: extract via `.has_value()` / `.value()` / `.error()`; avoid `operator*` so the type stays distinct from pointers at a glance.

Out of scope (intentional):
- `Process` ctor (pipe / `posix_spawn` failures) — system-level, exceptional; stays as exceptions.
- `StateDatabase` corrupt-blob throws — programming/data-corruption error; stays as exception.
- `RunResult::success()` — exit code is the value, not an error.

Tests converted from `EXPECT_THROW(...)` to `EXPECT_FALSE(result.has_value()) + EXPECT_EQ(result.error(), std::errc::no_such_file_or_directory)`. Two `MockHasher` test fixtures updated. All 127 tests pass.

### I4. `DiagnosticExtractor` is a class for no reason
It is a collection of free functions in an anonymous namespace dispatched by `InvokerKind` (`DiagnosticExtractor.cpp`). No state, no virtual dispatch, no member data. Make it a namespace of free functions or a single `extract(kind, …)` function. `DiagnosticFormatter` (`DiagnosticFormatter.cpp:1-35`) is a similarly trivial wrapper; inline it.

### I5. Test fixture duplication
`makeMemoryDb()` is redefined in 5 test files (`BuildDriver_test.cpp:126`, `FileStateTracker_test.cpp:37`, `FileNode_test.cpp:37`, `RuleNode_test.cpp:22`, `StateDatabase_test.cpp:9`). `MockHasher` is duplicated between `FileStateTracker_test.cpp:25` and `FileNode_test.cpp:25`. Temp-dir SetUp/TearDown boilerplate is everywhere. Extract `test/test_utils.{h,cpp}` with `makeMemoryDb`, `MockHasher`, `TempDirFixture`, `writeFile`.

### I6. `Blake3Hasher` text mode has a newline-handling bug ✅ DONE (2026-06-09)

**Original finding.** The text-mode (ignore-pattern) hashing loop unconditionally appended `"\n"` after every `getline`, so a file without a trailing newline was fed an extra byte that wasn't in the source. Text-mode and binary-mode hashes therefore diverged on the same content.

**Resolution.** Check `file.eof()` after each `getline`: when set, the line ran out of bytes before finding a delimiter — i.e. no trailing newline — so don't emit one. With this fix, hashing in text mode with a pattern that matches nothing produces the same digest as binary mode for both files with and without trailing newlines.

Two regression tests added in `FileHasher_test`:
- `textModeMatchesBinaryWhenNoLinesIgnored` (with/without trailing newline)
- `textModeNoTrailingNewline_differsFromTrailingNewline` (the file content really does differ)

Both fail on the old code, pass after the fix. All 129 tests pass.

---

## Minor findings

- **M1.** `EngineInvocation` mixes engine *config* (haltOnError, shellEscape, synctex, recorder) with *context* (texFile, outputDir, jobName, auxDir). Split into `EngineConfig` + `EngineRunRequest`.
- **M2.** No factory/registry — `main.cpp:48-50` hard-codes which tools exist. Once `ITool` lands (C1), a `std::vector<std::unique_ptr<ITool>>` registry is trivial.
- **M3.** `BuildDriver.h:6,8` includes `StateDatabase.h` and `graph/BuildGraph.h` but only stores references; forward-declare and move includes to the `.cpp`.
- **M4.** Common compile flags duplicated across targets in `CMakeLists.txt:80-99`; extract `set(COMMON_COMPILE_FLAGS …)`.
- **M5.** `Process::wait` has no timeout; long-hanging `pdflatex` runs are unbounded. Add optional `std::chrono::milliseconds`.
- **M6.** `Diagnostic::rawContext` is populated on every path but only read in `--verbose` mode; defer or make lazy.
- **M7.** Parsers and `DiagnosticExtractor` re-implement small string utilities (numeric suffix parsing, prefix scanning). Extract a `parse_util` header.
- **M8.** Parser tests cover only happy paths — no malformed `.log`/`.aux`/`.fls` cases.

---

## Suggested refactor order

The findings are listed individually but several stack. A pragmatic sequence:

1. **C1 + C2 together** — introduce `ITool`, unify invocations on a request type, remove the silent default `InvokerKind`. This is the largest design move and unblocks M2.
2. **C3** — pick one of "graph drives staleness" or "graph is gone." Both are fine; status quo is not.
3. **C4** — extract `ConvergenceLoop` from `BuildDriver`; the other two extractions fall out naturally.
4. **I1–I4 + I6** — independent localized cleanups; can be parallelized.
5. **I5 + M3 + M4** — test/build hygiene.
6. **Minor M-items** as time allows.

---

## Verification

This is a review-only artifact; no code changes proposed yet. To validate findings:

- Re-read the file:line citations under each finding and confirm the smell exists.
- Run `cmake --build build && ctest --test-dir build` after any refactor to confirm green.
- For C3 (graph), grep for callers of `RuleNode::isStale` — current count should be zero outside `RuleNode_test.cpp`.
- For C2, grep for `ToolInvocation{` and `EngineInvocation{` constructor sites and confirm whether `kind` is set or relies on the default.
