# Using maketex

maketex is a modern replacement for latexmk: it runs your TeX engine and
auxiliary tools (biber, makeindex, …) until the document converges, skips
work that is already up to date, and reports diagnostics cleanly.

```
maketex [build] [file.tex] [options]    # compile once (default command)
maketex watch  [file.tex] [options]     # rebuild whenever a source changes
maketex clean  [file.tex] [options]     # remove generated files
maketex doctor [file.tex] [options]     # check the TeX installation
maketex --help | --version
```

The `build` keyword is optional, and so is the file: in a directory with
one document, plain `maketex` builds it. maketex picks the document in this
order:

1. The file you pass (`maketex thesis` works too; `.tex` is optional).
2. `main` in [`maketex.toml`](configuration.md).
3. The `.tex` file in the current directory that starts with
   `\documentclass`. If there are several, `main.tex` or the one named after
   the directory wins; otherwise maketex lists them and asks you to choose.

If the chosen file has a `% !TEX root = ../thesis.tex` comment (as editors
write in chapter files), maketex builds that root document instead.

## Building

```sh
maketex thesis.tex
maketex thesis.tex --engine lualatex --synctex -o build
```

By default everything the build produces — aux files, logs, the `.fls`
recorder output, and the `.maketex.db` state database — goes into a hidden
`.maketex/` directory next to the document, and the finished PDF (plus
`.synctex.gz` when enabled) is copied back beside the `.tex` file. Your
source tree stays clean, and maketex writes a `.gitignore` inside
`.maketex/` so you don't have to. An explicit
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

### Choosing the engine

Unless you set one, maketex picks the engine from the document:

1. A `% !TeX program = xelatex` comment in the first lines.
2. The packages the preamble loads: Lua-only packages (`luacode`,
   `luatexbase`, …) or `\directlua` select `lualatex`; XeTeX-only packages
   (`xeCJK`, `mathspec`, …) select `xelatex`; `fontspec`, `unicode-math`, and
   `polyglossia` select `lualatex`.
3. Otherwise, `pdflatex`.

If a package hidden in a custom class needs a Unicode engine, pdflatex fails
with a message saying so, and maketex rebuilds once with the engine that the
message names. The report says which engine ran and why:
`lualatex (loads fontspec)`.

### Auxiliary tools

Auxiliary tools run automatically between engine passes when their inputs
change — the default roster is biber, makeindex, makeglossaries, pythontex,
and bibtex, in that order. A tool runs only when the latest pass used it
(for example, wrote a `.bcf`) and its inputs differ from its last run: a
prose edit reruns no tools, and editing only a `.bib` comment reruns bibtex
but no engine pass. Trim or reorder the roster with
`--tools biber,makeindex` or the `tools` config key.

### Missing packages and programs

When a package or tool isn't installed, maketex reports one error at the
`\usepackage` line that loads it and suggests the install command for your
TeX distribution:

```
error: main.tex:19: File `minted.sty' not found
    19 | \usepackage{minted}
  hint: install it: sudo tlmgr install minted
```

On TeX Live (including MacTeX and BasicTeX), maketex asks
`tlmgr search` which package provides an unfamiliar file and caches the
answer in `~/.cache/maketex/`. Debian, Fedora, and MiKTeX installs get
`apt`, `dnf`, and `miktex` commands instead.

By default, maketex also installs them for you. After a build fails for a
missing package or tool, it runs `tlmgr install` and builds again, round
after round, because TeX stops at the first missing package. This works on
TeX Live only; on other distributions you get the hint. How `tlmgr` runs
depends on who owns the TeX tree:

- If your user can write to the tree, maketex installs without asking.
- If root owns the tree (the MacTeX and BasicTeX default), a plain
  `maketex` build on a terminal asks first —
  ``Install with `sudo tlmgr install minted`? [Y/n]`` — then runs `sudo`,
  which asks for your password. Pass `--install-missing` (or set
  `install-missing = true`) to skip the question.
- In `watch`, CI, or with `--format=json`/`github`, maketex never prompts:
  with a root-owned tree it leaves the install to you and prints the
  command.

To make installs work everywhere without a password, take ownership of the
tree once: `sudo chown -R $(whoami) /usr/local/texlive/<year>`. To turn
installs off, pass `--no-install-missing` or set `install-missing = false`.

Documents that run external programs (minted, svg) need `--shell-escape`.
maketex never turns it on by itself, because it lets the document run
arbitrary commands; the error tells you to add the flag.

### Build options

| Flag | Effect |
|---|---|
| `--engine <name>` | `pdflatex`, `xelatex`, or `lualatex`; detected from the document when unset |
| `--engine-program <bin>` | spawn this executable instead of the engine's default binary |
| `--engine-opt <opt>` | pass an option through to the engine verbatim (repeatable) |
| `--tools a,b,c` | auxiliary tool roster, replacing the default |
| `-o`, `--output-dir <dir>` | where outputs and `.maketex.db` go (created if missing); default `.maketex/` next to the document, with the PDF published back |
| `--format <fmt>` | report format: `human` (default), `json`, or `github` (build only) |
| `--shell-escape` / `--no-shell-escape` | enable `\write18` (default off) |
| `--synctex` / `--no-synctex` | generate SyncTeX data (default on) |
| `--halt-on-error` / `--no-halt-on-error` | stop at the first TeX error (default on) |
| `--max-passes <n>` | give up if not converged after n passes (default 8) |
| `--timeout <seconds>` | stop any engine or tool run that takes longer (default: no limit) |
| `--install-missing` / `--no-install-missing` | install missing packages and tools with `tlmgr`, then rebuild (default on; explicit `--install-missing` skips the sudo confirmation) |
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

Two machine formats are available on `build` (see [ci.md](ci.md)):

- `--format=json` prints exactly one JSON document on stdout — build
  status, pass count, and a structured diagnostics array with severity,
  file, line, message, and a stable `category` tag (`undefined-citation`,
  `missing-file`, `package`, …).
- `--format=github` prints GitHub Actions workflow commands
  (`::error file=…,line=…::…`), which surface as inline annotations on
  pull requests.

Exit codes:

| Code | Meaning |
|---|---|
| `0` | success (also `--help` and `--version`) |
| `1` | the document did not build (TeX or tool errors, no convergence) |
| `2` | usage error: bad flag, invalid config, input file not found |
| `3` | environment error: engine or tool not installed, database or file system trouble |

With `--format=json`, stdout always carries exactly one JSON document, even
for exit code 3.

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
| `q` / `Ctrl-C` | quit (a running engine pass or tool is stopped) |
| `r` | force a rebuild now |
| `v` | toggle raw log context on diagnostics |

With `--no-tui` (or when stdout is not a terminal) maketex prints one report
per build instead — useful for CI or running inside another tool.

Watch-only options: `--interval-ms <n>` sets the polling interval
(default 300); the config's `[watch] extra-paths` adds files or directories
to the watch set.

## Checking the installation

```sh
maketex doctor               # engines, tools, and whether installs can run
maketex doctor thesis.tex    # also the packages and programs thesis.tex needs
maketex doctor thesis.tex --install-missing
```

`doctor` lists your TeX distribution, each engine and auxiliary tool with
its version, and whether `--install-missing` can run `tlmgr`. Given a
document, it also checks the engine the document will use, its class and
every package it loads, and the programs it needs: biber for biblatex,
makeindex for an index, pygmentize for minted. It also warns when the
document needs `--shell-escape`. Every missing item comes with an install
hint. Tools the document doesn't use are listed for information only.

`doctor` exits `0` when everything needed is installed and `3` otherwise.
It reads the document's source rather than building it, so it can't see
packages that a class or another package loads behind the scenes.

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
the document's directory to the repository root. Every boolean flag has a
`--no-` form, so flags always win over config values. See
[configuration.md](configuration.md) for the full schema.

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
