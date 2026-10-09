+++
title = 'Using maketex'
date = '2026-06-11T00:00:00Z'
+++

# Using maketex

maketex is a modern replacement for latexmk: it runs your TeX engine and
auxiliary tools (biber, makeindex, …) until the document converges, skips
work that is already up to date, and reports diagnostics cleanly.

```
maketex [build] [file.tex] [options]    # compile once (default command)
maketex watch  [file.tex] [options]     # rebuild whenever a source changes
maketex clean  [file.tex] [options]     # remove generated files
maketex --help | --version
```

The `build` keyword is optional: `maketex thesis.tex` works. With a
[`maketex.toml`](configuration/) declaring `main`, plain `maketex` with no
arguments builds the configured document.

## Building

```sh
maketex thesis.tex
maketex thesis.tex --engine lualatex --synctex -o build
```

By default everything the build produces — aux files, logs, the `.fls`
recorder output, and the `.maketex.db` state database — goes into a hidden
`.maketex/` directory next to the document, and the finished PDF (plus
`.synctex.gz` when enabled) is copied back beside the `.tex` file. Your
source tree stays clean; add `.maketex/` to `.gitignore`. An explicit
`--output-dir` (or the `output-dir` config key) overrides this and keeps
everything, including the PDF, inside the directory you chose.

maketex runs the engine with `-recorder`, tracks every input it reads, and
stores BLAKE3 file hashes in the state database. A second invocation with
nothing changed exits immediately:

```
$ maketex thesis.tex
✓ built  ·  4 passes
$ maketex thesis.tex
✓ up to date
```

The stored state is portable: paths are recorded relative to the document's
directory, so moving or renaming the project (or restoring a CI cache at a
different checkout path) keeps incremental builds working. A database
written by an incompatible maketex version or under different build options
(engine, tools, flags) is detected and reset to a clean full build —
upgrading after this change therefore triggers one full rebuild, and old
aux files strewn next to your sources are swept up by `maketex clean`.

Auxiliary tools run automatically between engine passes when their inputs
change — the default roster is biber, makeindex, makeglossaries, pythontex,
and bibtex, in that order. Trim or reorder it with `--tools biber,makeindex`
or the `tools` config key.

### Build options

| Flag | Effect |
|---|---|
| `--engine <name>` | `pdflatex` (default), `xelatex`, or `lualatex` |
| `--engine-program <bin>` | spawn this executable instead of the engine's default binary |
| `--engine-opt <opt>` | pass an option through to the engine verbatim (repeatable) |
| `--tools a,b,c` | auxiliary tool roster, replacing the default |
| `-o`, `--output-dir <dir>` | where outputs and `.maketex.db` go (created if missing); default `.maketex/` next to the document, with the PDF published back |
| `--format <fmt>` | report format: `human` (default), `json`, or `github` (build only) |
| `--shell-escape` | enable `\write18` |
| `--synctex` | generate SyncTeX data |
| `--no-halt-on-error` | keep compiling past TeX errors |
| `--max-passes <n>` | give up if not converged after n passes (default 8) |
| `-v`, `--verbose` | show raw log context under each diagnostic |
| `--plain` | plain text output even on a terminal |
| `--config <path>` / `--no-config` | use exactly this config file / ignore config files |

### Output

On a terminal, build reports are styled: severity-badged diagnostic rows
with `file:line` locations, the offending source line quoted underneath
(compiler-style), and a green/red status footer. Errors are attributed to
the file they actually occur in — an error inside `\input{chapters/ch1}`
points at `chapters/ch1.tex:12`, not the root document. When stdout is a
pipe, `--plain` is given, or `NO_COLOR` is set, maketex prints plain text
instead — safe for CI logs and scripting.

Two machine formats are available on `build` (see [ci.md](ci/)):

- `--format=json` prints exactly one JSON document on stdout — build
  status, pass count, and a structured diagnostics array with severity,
  file, line, message, and a stable `category` tag (`undefined-citation`,
  `missing-file`, `package`, …).
- `--format=github` prints GitHub Actions workflow commands
  (`::error file=…,line=…::…`), which surface as inline annotations on
  pull requests.

Exit codes: `0` success, `1` build failure or runtime error (engine missing,
database trouble), `2` configuration error (bad engine name, missing input
file). Malformed command lines exit non-zero with CLI11's own codes.

## Watching

```sh
maketex watch thesis.tex
```

Builds once, then polls the build's recorded source files (the `.tex` roots,
included chapters, `.bib` files, figures — everything the engine actually
read, excluding the TeX distribution) and rebuilds on change. Generated
files like `.aux` are never watched, so builds don't retrigger themselves.

On a terminal this opens a full-screen interface showing live status
(current pass, running tool), the latest diagnostics, and what changed:

| Key | Action |
|---|---|
| `q` / `Ctrl-C` | quit (an in-flight engine pass finishes first) |
| `r` | force a rebuild now |
| `v` | toggle raw log context on diagnostics |

With `--no-tui` (or when stdout is not a terminal) maketex prints one report
per build instead — useful for CI or running inside another tool.

Watch-only options: `--interval-ms <n>` sets the polling interval
(default 300); the config's `[watch] extra-paths` adds files or directories
to the watch set.

## Cleaning

```sh
maketex clean thesis.tex            # remove aux files, keep the PDF
maketex clean thesis.tex --all      # also remove PDF, SyncTeX, .maketex.db
maketex clean thesis.tex --dry-run  # show the plan without deleting
```

Clean removes the engine outputs recorded in the last build's `.fls` plus
the standard aux-file extensions (`.aux`, `.log`, `.bbl`, `.toc`, …; extend
via `[clean] extra-extensions`). Without `--all` the PDF (including the
copy published beside the document), SyncTeX file, and state database
survive; with `--all` they are removed and the emptied `.maketex/`
directory disappears too. Builds made by older maketex versions, which left
aux files next to the sources, are swept by the same command. Safety rules:
outside the output directory only the published PDF/SyncTeX and a legacy
`.maketex.db` are ever candidates, and the `.tex` source never is.

## Configuration

Per-project defaults live in `maketex.toml`, discovered by walking up from
the document's directory. Flags always win over config values. See
[configuration.md](configuration/) for the full schema.

```toml
main = "thesis.tex"
engine = "lualatex"
tools = ["biber"]
output-dir = "build"
```

## A typical session

```sh
$ cd ~/papers/thesis
$ maketex                  # builds main from maketex.toml
✓ built  ·  3 passes
$ maketex watch            # full-screen TUI; edit, save, watch it rebuild
$ maketex clean --all      # pristine tree before committing
```
