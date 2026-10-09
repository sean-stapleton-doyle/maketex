---
layout: landing
---

<br />

# maketex {anchor=false}

A modern C++23 replacement for `latexmk`. It runs your TeX engine and auxiliary
tools (biber, makeindex, …) until your document converges, skips work that is
already up to date, and reports diagnostics cleanly.

{{<button href="/docs/usage">}}Get Started{{</button>}}

<br />
<br />

{{% columns %}}
- ### Fast incremental builds {anchor=false}
  Tracks every input your engine reads and stores file hashes in a local database. A second invocation with nothing changed exits immediately — no wasted work.

- ### Automatic tool orchestration {anchor=false}
  Runs biber, makeindex, makeglossaries, pythontex, and bibtex automatically between engine passes whenever their inputs change.

- ### Watch mode {anchor=false}
  Polls your source files and rebuilds on change with a full-screen TUI showing live status, diagnostics, and what changed.
{{% /columns %}}

{{% columns %}}
- ### Clean diagnostics {anchor=false}
  Severity-badged diagnostic rows with `file:line` locations and a styled status footer. Plain text mode available for CI logs.

- ### Zero-config defaults {anchor=false}
  Sensible defaults out of the box. Optionally configure projects with a `maketex.toml` file discovered automatically.

- ### Embeddable library {anchor=false}
  The core library builds with only two dependencies. Consume it via `add_subdirectory`, CPM, or FetchContent and bring your own CLI.
{{% /columns %}}

## Documentation

- [Usage](/docs/usage/) — the `maketex` command line: build, watch, clean
- [Architecture](/docs/architecture/) — layer diagram, data flow, type system, key design decisions
- [Configuration](/docs/configuration/) — the `maketex.toml` config file: discovery, schema, precedence
- [Testdata Cases](/docs/testdata-cases/) — fixture inventory (success + failure cases)
- [BibTeX Build Walkthrough](/docs/bibtex-execution-walkthrough/) — step-by-step trace of a 3-pass bibtex build
