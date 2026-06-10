# Process Execution & Threading — Design Plan

> **Status: superseded.** This document was the pre-implementation design.
> The code was implemented as described except:
> - **stdexec was replaced by Taskflow** (`tf::Executor`). The `IEngine::run()`
>   contract (`std::future<RunResult<EngineInvocation>>`) is the same but the
>   underlying scheduler is Taskflow, not stdexec/P2300.
> - **`EngineResult` was replaced by `RunResult<Inv>`** — a template carrying
>   a `shared_ptr<const Inv>` for `DiagnosticExtractor` context.
> - **`-interaction=nonstopmode`** is used instead of `batchmode` so that
>   errors/warnings appear on stdout for in-memory parsing.
>
> See [architecture.md](architecture.md) and
> [implementation_progress.md](implementation_progress.md) for the current state.

## Context

This covers Stage 1 process execution: launching `pdflatex` (and later bibtex,
biber, makeindex, etc.), capturing stdout/stderr, and returning results
asynchronously. It also establishes the threading infrastructure that Stage 3
parallel rule scheduling will build on.

## Compiler/Library Situation

Apple Clang 21 reports `__cplusplus = 202400L` (C++26) but its bundled libc++
has not shipped two C++26 features we care about:

### `std::execution` (P2300) — use stdexec
**Resolution:** NVIDIA/stdexec `nvhpc-26.05` is the canonical P2300 reference
implementation. Added to CMakeLists via CPM with `STDEXEC_BUILD_TESTS OFF`.
Linked as `STDEXEC::stdexec`. When Apple's libc++ ships P2300 the include
path changes from `<stdexec/execution.hpp>` to `<execution>`; everything else
stays the same.

### `std::move_only_function` — use `std::shared_ptr` where needed
**Resolution:** `std::move_only_function` is missing from Apple libc++.
- Where a callable is used immediately (submit lambda to pool): use a plain
  template parameter — zero overhead, no type erasure needed.
- Where a callable must be stored in a container or struct field: wrap in
  `std::shared_ptr<std::function<...>>`. The ref-count overhead is negligible
  for build-system work (we're launching OS processes, not hot loops).
- Do not work around the missing feature with `std::function` directly for
  move-only lambdas (those that capture `unique_ptr`, `promise`, etc.) —
  use `shared_ptr<function>` in those cases.

This is a toolchain gap, not a design choice; revisit when libc++ ships
`move_only_function`.

---

## Layers

```
┌─────────────────────────────────────────────────────┐
│  EngineBase  (IEngine impl, builds argv, submits)   │
├─────────────────────────────────────────────────────┤
│  Process  (posix_spawn, poll pipe drain, waitpid)   │
├─────────────────────────────────────────────────────┤
│  stdexec static_thread_pool  (C++26 std::execution) │
└─────────────────────────────────────────────────────┘
```

---

## Layer 1 — `Process`

**File:** `include/Process.h`, `src/Process.cpp`

Thin POSIX wrapper. Owns the child process and its stdout/stderr pipes.

```cpp
namespace maketex {

struct ProcessResult {
    int exitCode;
    std::string stdoutOutput;
    std::string stderrOutput;
};

class Process {
public:
    // Launches immediately on construction. Throws on spawn failure.
    explicit Process(std::span<const std::string> argv);

    // Non-copyable, movable.
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    Process(Process&&) noexcept;
    Process& operator=(Process&&) noexcept;

    // Blocks: poll()-drains both pipes, then waitpid(). Safe to call once.
    ProcessResult wait();

    // Sends SIGTERM; destructor calls this if wait() was never called.
    void kill() noexcept;

    ~Process();

private:
    pid_t m_pid = -1;
    int m_stdoutFd = -1;
    int m_stderrFd = -1;
};

} // namespace maketex
```

### Spawn strategy

Use `posix_spawn` with `posix_spawn_file_actions_t` to redirect the child's
stdout/stderr into pipes created by `pipe2(O_CLOEXEC)`. `posix_spawn` is
preferred over `fork`+`exec` on macOS (no full address-space copy) and is
available on all POSIX targets.

### Pipe drain strategy

`wait()` runs a `poll(2)` loop over the two read-end file descriptors until
both are EOF, then `waitpid`. This is the standard deadlock-safe approach:
reading from one pipe while the other's kernel buffer fills would deadlock
with two sequential `read()` calls; `poll` avoids this by always reading
whichever pipe has data.

```
while (at least one pipe still open):
    poll([stdout_fd, stderr_fd], timeout=-1)
    for each fd with POLLIN: read into buffer
    for each fd with POLLHUP/EOF: mark closed
waitpid(m_pid, WNOHANG loop or blocking)
```

### Destructor contract

If `wait()` was never called, the destructor sends SIGTERM. This covers the
"build cancelled by watch mode" path without requiring the caller to always
clean up explicitly.

---

## Layer 2 — Thread pool (stdexec)

**CMakeLists addition:**

```cmake
CPMAddPackage("gh:NVIDIA/stdexec#main")
target_link_libraries(maketex_lib PUBLIC STDEXEC::stdexec)
```

No wrapper class needed — use `stdexec::static_thread_pool` directly. The
pool is owned by a top-level `BuildContext` (or by `main`) and injected into
engines. Thread count defaults to `std::thread::hardware_concurrency()`.

```cpp
#include <stdexec/execution.hpp>

namespace ex = stdexec;

// Construction (owned by BuildContext):
ex::static_thread_pool pool(std::thread::hardware_concurrency());
ex::scheduler auto sched = pool.get_scheduler();

// Submitting work (inside EngineBase::run):
auto sender = ex::schedule(sched)
    | ex::then([args]() mutable -> EngineResult {
          return toEngineResult(Process(args).wait());
      });

// Blocking wait (inside the fixed-point loop driver):
auto result = ex::sync_wait(std::move(sender)).value();
```

For Stage 3 parallel rules, `ex::when_all(sender1, sender2, ...)` runs
independent senders concurrently on the pool and joins — this is exactly the
structured-concurrency primitive we need for "run biber and makeindex in
parallel, then resume the latex pass."

### Bridge to `std::future<EngineResult>`

`IEngine::run()` currently returns `std::future<EngineResult>`. Keep this
contract — it is what the fixed-point loop driver calls. Bridge it like this:

```cpp
std::future<EngineResult> EngineBase::run(const EngineInvocation& inv) {
    std::promise<EngineResult> p;
    auto fut = p.get_future();

    auto sender = ex::schedule(m_sched)
        | ex::then([args = buildArgs(inv)]() mutable {
              return toEngineResult(Process(args).wait());
          });

    // Fire-and-forget: fulfil the promise when the sender completes.
    ex::start_detached(
        std::move(sender)
        | ex::then([p = std::move(p)](EngineResult r) mutable {
              p.set_value(std::move(r));
          })
    );

    return fut;
}
```

Once the fixed-point loop is refactored to be sender-aware (Stage 3), the
`std::future` bridge disappears and the loop uses `ex::sync_wait` directly.

---

## Layer 3 — `EngineBase`

**Files:** `include/EngineBase.h`, `src/EngineBase.cpp`

Concrete base for `PdfLatexEngine`, `XeLatexEngine`, `LuaLatexEngine`.
Implements `IEngine`. Subclasses override `buildArgs`.

```cpp
namespace maketex {

class EngineBase : public IEngine {
public:
    explicit EngineBase(stdexec::scheduler auto sched);

    std::future<EngineResult> run(const EngineInvocation& inv) override;
    bool handlesOwnLoop() const override { return false; }

protected:
    // Subclass builds the full argv: engine binary + flags + file paths.
    virtual std::vector<std::string> buildArgs(const EngineInvocation& inv) const = 0;

private:
    // scheduler type-erased via any_scheduler if needed
    stdexec::any_scheduler m_sched;
};

} // namespace maketex
```

**`PdfLatexEngine`** is the first concrete engine:

```cpp
class PdfLatexEngine final : public EngineBase {
public:
    using EngineBase::EngineBase;
    std::string_view name() const override { return "pdflatex"; }

protected:
    std::vector<std::string> buildArgs(const EngineInvocation& inv) const override;
};
```

`buildArgs` produces:
```
pdflatex
  -recorder               (always — dependency discovery requires .fls)
  -interaction=batchmode  (suppress prompts on error)
  -halt-on-error          (if inv.haltOnError)
  -shell-escape           (if inv.shellEscape)
  -synctex=1              (if inv.synctex)
  -output-directory=<dir> (if inv.outputDir set)
  -aux-directory=<dir>    (if inv.auxDir set, MiKTeX-style; skip on TeX Live)
  <jobname flag>          (if inv.jobName set)
  <texFile>
```

---

## File layout after this work

```
include/
  Process.h
  EngineBase.h
  engines/
    PdfLatexEngine.h

src/
  Process.cpp
  EngineBase.cpp
  engines/
    PdfLatexEngine.cpp

test/
  Process_test.cpp     — run /bin/echo, /bin/cat; verify capture, exit code
  EngineBase_test.cpp  — mock Process, verify argv construction
```

---

## Implementation order

1. Add stdexec to CMakeLists; verify it compiles.
2. `Process` + `Process_test.cpp` — test against `/bin/echo` and
   `/usr/bin/true`/`/usr/bin/false`; verify stdout/stderr captured
   separately; verify kill() terminates a sleeping process.
3. `EngineBase` + `PdfLatexEngine` (argv construction only; no real latex
   invocation in tests).
4. `EngineBase_test.cpp` — inject a fake scheduler that runs tasks inline;
   verify `buildArgs` output for various `EngineInvocation` configurations.
5. Manual smoke test: run `pdflatex -recorder -interaction=batchmode` on a
   minimal `.tex` file; confirm `EngineResult` captures log and `.fls` appears
   in the output directory.

---

## Open questions (decide before coding)

1. **`any_scheduler` vs template parameter** — stdexec's `any_scheduler`
   type-erases the scheduler for runtime polymorphism (needed to inject a mock
   in tests). Alternatively, make `EngineBase` a template. `any_scheduler` is
   simpler; template is zero-overhead. Prefer `any_scheduler` until profiling
   says otherwise.

2. **`-aux-directory` flag** — TeX Live `pdflatex` does not support
   `-aux-directory`; MiKTeX does. Detect at runtime via engine binary name/
   version, or just omit it and always use `-output-directory` for both?
   Simplest correct answer: omit `auxDir` from argv for now; add when a user
   reports needing it.

3. **stdexec version pin** — pinned to `nvhpc-26.05`. ✓ Done.
