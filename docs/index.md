# maketex Documentation

## Overview

- [Usage](USAGE.md) — the `maketex` command line: build, watch, clean
- [CI](ci.md) — caching `.maketex/`, `--format=github`/`json`, GitHub Actions recipe
- [Architecture](architecture.md) — layer diagram, data flow, type system, key design decisions
- [Configuration](configuration.md) — the `maketex.toml` config file: discovery, schema, precedence
- [Implementation Progress](implementation_progress.md) — what is built, what remains

Building: the core library always builds; `MAKETEX_BUILD_CLI`,
`MAKETEX_BUILD_HARNESS`, and `MAKETEX_BUILD_TESTS` control the rest and
default to ON only when maketex is the top-level CMake project, so embedding
consumers get just the library (and only its two dependencies).

Testing: `ctest --test-dir build -LE integration` runs the unit tests;
`ctest --test-dir build -L integration` builds every `testdata/` fixture with
the real TeX engines. A fixture whose engine, tool, or package is not
installed is reported as *Skipped* with the missing names (a BasicTeX install
skips several); CI installs a full TeX Live so all of them run.

## Design Documents

| Document | Status |
|---|---|
| [Diagnostics Plan](diagnostics_plan.md) | Implemented (2026-06-08) |
| [Process Execution Plan](process_execution_plan.md) | Superseded — see architecture.md for divergences |
| [latexmk Replacement](latexmk_replacement.md) | Original specification |

## Class Reference

| Document | Classes covered |
|---|---|
| [BuildDriver](classes/BuildDriver.md) | `BuildDriver`, `DriverResult` |
| [BuildGraph](classes/BuildGraph.md) | `BuildGraph` |
| [FileNode](classes/FileNode.md) | `FileNode`, `FileRole` |
| [RuleNode](classes/RuleNode.md) | `RuleNode` |
| [StateDatabase](classes/StateDatabase.md) | `StateDatabase`, `FileState`, `RuleRecord`, `Hash` |
| [FileStateTracker](classes/FileStateTracker.md) | `FileStateTracker` |
| [Engines](classes/IEngine_EngineBase_PdfLatexEngine.md) | `IEngine`, `EngineBase`, `PdfLatexEngine`, `EngineInvocation` |
| [Tools](classes/ToolBase.md) | `ToolBase`, `BiberTool`, `BibtexTool`, `ToolInvocation` |
| [RunResult](classes/RunResult.md) | `RunResult<Inv>`, `InvokerKind`, `Invocation` concept |
| [Diagnostics](classes/Diagnostics.md) | `Diagnostic`, `DiagnosticExtractor`, `DiagnosticFormatter` |
| [Process](classes/Process.md) | `Process`, `ProcessResult` |
| [Hashing](classes/IFileHasher_Blake3Hasher.md) | `Blake3Hasher` |
| [Parsers](classes/parsers.md) | `TextFileParser`, `FlsParser`, `LogParser`, `AuxParser` |

## Other

- [Testdata cases](testdata_cases.md) — fixture inventory (success + failure cases)
- [Bibtex execution walkthrough](bibtex_execution_walkthrough.md) — step-by-step
  trace of a 3-pass bibtex build
