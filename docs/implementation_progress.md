# Implementation Progress

Last updated: 2026-06-08

---

## Implemented

### Build System
- `CMakeLists.txt` — C++23, CPM-based dependency management, `compile_commands.json`
  symlinked to project root for clangd
- Dependencies: BLAKE3 (1.8.2), SQLiteCpp (3.3.2), GoogleTest (1.16.0), Taskflow (3.7.0)
- `maketex_lib` static library shared by executable and test target
- `.clang-format` — WebKit style

### Process Execution (`include/Process.h`, `src/Process.cpp`)
- `posix_spawn` + `poll(2)` pipe drain — deadlock-safe stdout/stderr capture
- Optional working directory via `posix_spawn_file_actions_addchdir`
  (critical for multi-file documents: engine runs from the document directory
  so `\input{subdir/file}` resolves correctly)

### Type System (`include/InvokerKind.h`, `include/Invocation.h`, `include/RunResult.h`)
- `InvokerKind` enum — `PdfLatex`, `XeLaTeX`, `LuaLaTeX`, `Bibtex`, `Biber`, `Makeindex`
- `Invocation<T>` concept — requires `kind` and `outputDir`; constrains `RunResult`
  and `DiagnosticExtractor` without inheritance or variant
- `RunResult<Inv>` — replaces `EngineResult`; holds `shared_ptr<const Inv>` so the
  result is self-contained and the extractor has full invocation context

### Engine Layer (`include/IEngine.h`, `include/EngineBase.h`, `src/EngineBase.cpp`)
- `IEngine` virtual base + `Engine<T>` concept
- `EngineBase` — Taskflow async dispatch returning `std::future<RunResult<EngineInvocation>>`
- `PdfLatexEngine` — full argv construction (`-interaction=nonstopmode` so errors
  appear on stdout for `DiagnosticExtractor`; `-halt-on-error`, `-recorder`, etc.)
- `EngineInvocation` carries `InvokerKind kind` for diagnostic dispatch

### Tool Layer (`include/ToolBase.h`, `src/ToolBase.cpp`)
- `ToolBase` — mirrors `EngineBase`; takes `ToolInvocation` by value so
  `invokerKind()` can be stamped before the `shared_ptr` is created
- `BiberTool` — `--input-directory` / `--output-directory` flags; runs in `outputDir`
- `BibtexTool` — bare job name only; runs from `workingDir` (= `outputDir`) to
  satisfy TeX Live `openout_any=p` restriction on absolute output paths

### Dependency Graph (`include/graph/`, `src/graph/`)
- `FileNode` — canonical path, `FileRole` (`Source` / `Generated` / `Output`), hash cache
- `RuleNode` — named rule, input/output `FileNode*` sets, `markRan()` to persist state
- `BuildGraph` — `getOrAddFile` / `getOrAddRule`; node ownership via `unique_ptr`

### Hashing (`include/Blake3Hasher.h`, `include/FileStateTracker.h`)
- BLAKE3 via the vendored C implementation
- `FileStateTracker` — `(mtime, size)` fast path; cold-path rehashes and writes
  back to `StateDatabase`

### State Persistence (`include/StateDatabase.h`, `src/StateDatabase.cpp`)
- SQLite in WAL mode; schema applied idempotently on construction
- Tables: `file_states`, `rules`, `rule_inputs`
- Database created as `.maketex.db` in the document output directory

### Parsers (`src/parsers/`)
- `FlsParser` — `PWD` / `INPUT` / `OUTPUT` records; path normalisation
- `LogParser` — detects `rerunNeeded`, `biberRerunNeeded`, `bibtexRerunNeeded`,
  `undefinedReferences`, `undefinedCitations`
- `AuxParser` — `\bibdata`, `\citation`, `\@input` extraction

### Build Loop (`include/BuildDriver.h`, `src/BuildDriver.cpp`)
- Full fixed-point loop: engine pass → snapshot → tool runs → convergence check
- Biber triggered by `log.biberRerunNeeded`
- Bibtex triggered by `AuxParser` finding `\bibdata` entries (runs once per build)
- Convergence: `Generated` file hashes unchanged AND no log rerun signal
- Max-passes guard (default 8); `DriverResult` carries pass count, last error,
  and extracted `vector<Diagnostic>`

### Diagnostics (`include/Diagnostic.h`, `include/DiagnosticExtractor.h`, `include/DiagnosticFormatter.h`)
- `Diagnostic` — `severity`, `file`, `line`, `message`, `rawContext`
- `DiagnosticExtractor` — dispatches on `InvokerKind`:
  - **pdflatex/xelatex/lualatex errors**: `! ` lines + `l.N` look-ahead for line number;
    strips `LaTeX Error:` prefix; filters `!  ==>` continuation lines
  - **pdflatex/xelatex/lualatex warnings**: `LaTeX Warning:` lines with `on input line N`
    extraction; `Overfull \hbox` with line-range extraction; summary lines suppressed
  - **bibtex**: `I couldn't open database file` (fatal); `Warning--` lines
  - **biber**: `ERROR -` and `WARN -` prefixed lines
  - **fallback**: single diagnostic with raw stdout in `rawContext`
- `DiagnosticFormatter` — terminal output; `--verbose` reveals `rawContext`
- Warnings extracted from the final convergent pass; errors extracted on failure

### CLI (`src/main.cpp`)
- `maketex <file.tex> [--verbose|-v]`
- Locates document, creates `.maketex.db`, wires engine + tools + driver
- Prints structured diagnostics to stderr after build summary

### Test Suite
- 129 tests across 16 suites (all passing)
- `Process_test`, `EngineBase_test`, `IEngine_test`, `BuildDriver_test`,
  `BiberTool_test`, `BibtexTool_test`, `LogParser_test`, `AuxParser_test`,
  `FlsParser_test`, `BuildGraph_test`, `FileNode_test`, `RuleNode_test`,
  `FileStateTracker_test`, `StateDatabase_test`, `Blake3Hasher_test`

### Testdata Fixtures
- **15 success cases** under `testdata/` — cross-references, toc, bibtex, biber,
  makeindex, glossaries, input-include, graphics-raster/eps, pgfplots, minted,
  pythontex, xelatex-fontspec, lualatex-luacode
- **7 hard-error cases** under `testdata/failures/` — undefined command, missing
  package, missing input, runaway argument, unclosed environment, bibtex missing
  bib, bibtex bad entry
- **3 warning cases** under `testdata/failures/` — undefined refs, duplicate label,
  overfull hbox

### Debugging
- `debug/maketex.lldb` — LLDB session script with breakpoints at hash comparison,
  graph update, and engine result
- `debug/maketex_callbacks.py` — Python callbacks for structured breakpoint output

---

## Not Yet Implemented

### Additional Engines
- `XeLatexEngine` — two-stage: engine → `.xdv`, then `xdvipdfmx` → PDF
- `LuaLatexEngine`
- `TectonicEngine` — `handlesOwnLoop() = true`; driver skips fixed-point loop

### Additional Tools
- `MakeindexTool` — `.idx` → `.ind` + `.ilg`; nomenclature `.nlo` → `.nls`
- `MakeglossariesTool` — `.glo`/`.acn` → `.gls`/`.acr`
- `Bib2GlsTool` — `.aux` + `.bib` → `.glstex`
- `PythonTexTool` — `.pytxcode` re-run handling
- `PygmentizeTool` — minted integration

### Diagnostic Improvements
- Multi-file document file tracking in errors (TeX paren-stack parsing is
  currently skipped; file is always reported as the root `.tex`)
- Hint strings for common errors (e.g. "undefined control sequence → did you
  forget `\usepackage{...}`?")
- Warning extraction from intermediate passes (currently only the final pass)
- JSON output mode and IDE/LSP protocol formatter

### Engine Selection
- Runtime engine selection from CLI flag or document magic comment
- `XeLatexEngine` / `LuaLatexEngine` invocation paths

### Watch Mode
- OS-native file notification (FSEvents / inotify)
- Debounce + partial rebuild (only rules whose inputs changed)

### Configuration
- TOML config file — engine, tool paths, `max_passes`, clean/precious extension
  lists, ignore-line patterns

### Parallel Rule Scheduling (Stage 3)
- Topological level execution on the Taskflow executor
- Independent bibliographies, indexes, and figure jobs running concurrently
- TikZ externalization support

### CLI Completeness
- `--engine` flag
- `--clean` / `--distclean`
- `--dry-run`
- Exit code conventions (warnings vs errors)
