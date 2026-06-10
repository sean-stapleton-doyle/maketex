# Diagnostics Feature Plan

> **Status: implemented.** All items in this plan are complete as of 2026-06-08.
> Remaining future work (multi-file error file tracking, hint strings, JSON
> output mode) is tracked in [implementation_progress.md](implementation_progress.md).

## Motivation

The primary reason for replacing latexmk is that its output is difficult to
understand, especially for novice users. It typically dumps raw TeX log text
without filtering or contextualisation. This feature ensures that maketex
surfaces build failures and warnings as structured, human-readable diagnostics,
with raw engine output hidden unless the user explicitly requests it.

---

## Design Principles

- **Extract then format.** Parsing and presentation are separate concerns.
  `DiagnosticExtractor` produces structured data; `DiagnosticFormatter` produces
  terminal output. This separation enables future output modes (JSON, LSP, IDE).
- **Never dump raw stdout by default.** The fallback for an unrecognised error
  is still a structured `Diagnostic`, not a wall of log text. Raw output is
  available only via `--verbose`.
- **Caller always gets a `vector<Diagnostic>`.** No special-casing of the raw
  output at the call site. The extractor owns the fallback.

---

## New Types

### `InvokerKind` (`include/InvokerKind.h`)

```cpp
enum class InvokerKind { PdfLatex, XeLaTeX, LuaLaTeX, Bibtex, Biber, Makeindex };
```

Added as a field to both `EngineInvocation` and `ToolInvocation`. This is the
only field the `Invocation` concept requires beyond `outputDir`.

### `Invocation` concept (`include/Invocation.h`)

```cpp
template<typename T>
concept Invocation = requires(const T& inv) {
    { inv.kind }      -> std::convertible_to<InvokerKind>;
    { inv.outputDir } -> std::convertible_to<std::filesystem::path>;
};
```

Constrains `RunResult` and `DiagnosticExtractor` without requiring inheritance
or a variant. Both `EngineInvocation` and `ToolInvocation` satisfy it once
`kind` is added.

### `RunResult<Inv>` (`include/RunResult.h`)

Replaces `EngineResult`. Templated on the invocation type.

```cpp
template<Invocation Inv>
struct RunResult {
    std::shared_ptr<const Inv> invocation;
    int exitCode;
    std::string stdoutOutput;
    std::string stderrOutput;

    bool success() const { return exitCode == 0; }
};
```

The invocation is heap-allocated and shared so that `RunResult` is self-contained
— no raw pointers, no lifetime dependency on the owner. `EngineBase::run()` and
`ToolBase::run()` return `std::future<RunResult<EngineInvocation>>` and
`std::future<RunResult<ToolInvocation>>` respectively.

### `Diagnostic` (`include/Diagnostic.h`)

```cpp
struct Diagnostic {
    enum class Severity { Error, Warning, Info };

    Severity severity;
    std::string file;       // source .tex file; empty if unknown
    int line = -1;          // 1-based; -1 if unknown
    std::string message;    // clean, human-readable message
    std::string rawContext; // surrounding log lines; shown only with --verbose
};
```

`rawContext` carries the raw log excerpt that the structured fields were
extracted from. It is never shown at normal verbosity.

---

## Extraction

### `DiagnosticExtractor` (`include/DiagnosticExtractor.h`, `src/DiagnosticExtractor.cpp`)

```cpp
class DiagnosticExtractor {
public:
    template<Invocation Inv>
    std::vector<Diagnostic> extract(const RunResult<Inv>& result) const;
};
```

Dispatches internally on `result.invocation->kind`:

- `PdfLatex`, `XeLaTeX`, `LuaLaTeX` — share the same `! Error` / `LaTeX Warning:`
  format; handled by a common TeX log parser.
- `Bibtex` — completely different format; separate parser.
- `Biber` — different again; separate parser.
- `Makeindex` — separate parser.

**Fallback** (unrecognised kind, or no structured errors found):

```cpp
Diagnostic {
    .severity   = Diagnostic::Severity::Error,
    .file       = {},
    .line       = -1,
    .message    = "build failed — run with --verbose to see full engine output",
    .rawContext = result.stdoutOutput,
}
```

The user always sees a clean message. The raw log is in `rawContext` and surfaced
only by the formatter under `--verbose`.

---

## Formatting

### `DiagnosticFormatter` (`include/DiagnosticFormatter.h`, `src/DiagnosticFormatter.cpp`)

Responsible for all terminal output. Controls verbosity.

```cpp
class DiagnosticFormatter {
public:
    explicit DiagnosticFormatter(bool verbose = false);
    void print(const std::vector<Diagnostic>& diagnostics, std::ostream& out) const;
};
```

At normal verbosity: prints `severity`, `file:line`, and `message` only.
Under `--verbose`: also prints `rawContext` for each diagnostic.

Novice-friendliness lives here, not in the extractor. Future work can add
`hint` strings for common errors (e.g. `! Undefined control sequence` →
"Did you forget to `\\usepackage{...}`?"), without touching the parsing layer.

---

## Changes to Existing Types

### `EngineInvocation`

Add `InvokerKind kind` field. Remove `EngineResult` from this header (it moves
to `RunResult.h`).

### `ToolInvocation`

Add `InvokerKind kind` field.

### `EngineBase` / `ToolBase`

Return `std::future<RunResult<EngineInvocation>>` and
`std::future<RunResult<ToolInvocation>>` respectively. The invocation is
captured as a `shared_ptr<const Inv>` inside the async lambda so the result is
self-contained.

### `BuildDriver`

Collect `RunResult` from each engine and tool call. On failure (or unconditionally
for warnings), pass the result to `DiagnosticExtractor`. Attach the resulting
`vector<Diagnostic>` to `DriverResult`.

### `DriverResult`

Add `std::vector<Diagnostic> diagnostics`.

### `main.cpp`

Construct `DiagnosticFormatter` with the `--verbose` flag. After `driver.run()`,
call `formatter.print(result.diagnostics, std::cerr)`.

---

## Known Design Quirk: `jobName` optionality

`EngineInvocation::jobName` is `optional<string>` because it maps directly to
the `-jobname` CLI flag — absent means "pdflatex defaults to the tex file stem."
`ToolInvocation::jobName` is plain `string` because `BuildDriver::makeToolInv()`
always resolves the optional before constructing the tool invocation. The types
differ intentionally: one captures user intent, the other carries a resolved
value. This asymmetry should be revisited if `ToolInvocation` ever needs to
express the same "override or default" semantics.

---

## Files to Create / Modify

| File | Action |
|---|---|
| `include/InvokerKind.h` | Create |
| `include/Invocation.h` | Create — concept |
| `include/RunResult.h` | Create — replaces `EngineResult` |
| `include/Diagnostic.h` | Create |
| `include/DiagnosticExtractor.h` | Create |
| `src/DiagnosticExtractor.cpp` | Create |
| `include/DiagnosticFormatter.h` | Create |
| `src/DiagnosticFormatter.cpp` | Create |
| `include/EngineInvocation.h` | Modify — add `kind`; remove `EngineResult` |
| `include/ToolInvocation.h` | Modify — add `kind` |
| `include/EngineBase.h` | Modify — return type |
| `src/EngineBase.cpp` | Modify — construct `RunResult` with shared invocation |
| `include/ToolBase.h` | Modify — return type |
| `src/ToolBase.cpp` | Modify — construct `RunResult` with shared invocation |
| `include/BuildDriver.h` | Modify — `DriverResult` gets `diagnostics` field |
| `src/BuildDriver.cpp` | Modify — extract diagnostics on failure |
| `src/main.cpp` | Modify — wire `DiagnosticFormatter`, pass `--verbose` |
| `CMakeLists.txt` | Modify — add new sources |
