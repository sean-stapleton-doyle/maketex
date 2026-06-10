# maketex Documentation

## Overview

- [Architecture](architecture.md) — layer diagram, data flow, type system, key design decisions
- [Implementation Progress](implementation_progress.md) — what is built, what remains

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
