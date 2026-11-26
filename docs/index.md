# maketex Documentation

## Overview

- [Usage](USAGE.md) — the `maketex` command line: build, watch, clean
- [CI](ci.md) — caching `.maketex/`, `--format=github`/`json`, GitHub Actions recipe
- [Packaging](packaging.md) — Homebrew tap, AUR (`maketex-bin`), and `curl | sh` installer setup
- [Architecture](architecture.md) — layer diagram, data flow, type system, key design decisions
- [Configuration](configuration.md) — the `maketex.toml` config file: discovery, schema, precedence
- [Implementation Progress](implementation_progress.md) — what is built, what remains
- [Next up](next-up.md) — known gaps and the backlog

Building: the core library always builds; `MAKETEX_BUILD_CLI` and
`MAKETEX_BUILD_TESTS` control the rest and default to ON only when maketex
is the top-level CMake project, so embedding consumers get just the library
(and only its two dependencies).

Testing: `ctest --test-dir build -LE "integration|e2e"` runs the unit
tests; `ctest --test-dir build -L integration` builds every `testdata/`
fixture with the real TeX engines through the library; and
`ctest --test-dir build -L e2e` runs the `maketex` binary itself —
arguments, exit codes, output formats, `watch`, and package installs (with a
fake `tlmgr`). A fixture whose engine, tool, or package is not
installed is reported as *Skipped* with the missing names (a BasicTeX install
skips several); CI installs a full TeX Live so all of them run.

## Design Documents

| Document | Status |
|---|---|
| [Compile API & Sandboxing Design](api_and_sandboxing_design.md) | Proposed (2026-11-26) |
| [Test Gap Plan](test_plan.md) | Proposed (2026-10-09) |
| [Diagnostics Plan](diagnostics_plan.md) | Implemented (2026-06-08) |
| [Process Execution Plan](process_execution_plan.md) | Superseded — see architecture.md for divergences |
| [latexmk Replacement](latexmk_replacement.md) | Original specification |

## Other

- [Testdata cases](testdata_cases.md) — fixture inventory (success + failure cases)
- [Bibtex execution walkthrough](bibtex_execution_walkthrough.md) — step-by-step
  trace of a 3-pass bibtex build
