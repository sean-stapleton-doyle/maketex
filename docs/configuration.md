# Configuration: `maketex.toml`

maketex reads per-project defaults from a `maketex.toml` file. Everything in
it can also be set on the command line; the config file exists so a project
can pin its engine, tool roster, and output layout without every invocation
repeating them.

## Discovery

1. `--config <path>` uses exactly that file (and errors if it is missing).
2. Otherwise maketex starts in the directory of the `.tex` file given on the
   command line — or the current directory when no file was given — and walks
   up toward the repository root. The first `maketex.toml` found wins; files
   in higher directories are not merged. The search stops at the first
   directory containing `.git`, `.hg`, or `.svn`, and never reads a
   `maketex.toml` in your home directory, so a stray config can't redirect
   builds of unrelated documents.
3. `--no-config` skips the search entirely.

Relative paths inside the config (`main`, `output-dir`, `watch.extra-paths`)
resolve against the directory containing the config file, not the current
working directory.

## Precedence

Per field: **command-line flag > config value > built-in default.**

Unknown keys produce a warning naming the file, line, and key, but do not
fail the build — older maketex versions stay usable with newer configs.
Type errors and invalid values (an unknown engine name, a `max-passes` or
`timeout` that isn't a positive integer) are hard errors.

## Schema

All keys are optional. Values shown are examples, not defaults, except where
noted.

```toml
# Document built when no .tex file is given on the command line.
# `maketex` with no arguments builds this.
main = "thesis.tex"

# TeX engine: "pdflatex", "xelatex", or "lualatex". Default: detected from
# the document (see USAGE.md, "Choosing the engine").
engine = "lualatex"

# Where outputs and the .maketex.db state database go. Created if missing.
# Default: a hidden ".maketex" directory next to the .tex file, with the
# finished PDF (and .synctex.gz) published back beside the document.
# Setting an explicit directory keeps everything, the PDF included, inside it.
output-dir = "build"

# Auxiliary tools eligible to run between engine passes, in this order.
# Replaces (not extends) the default roster:
#   ["biber", "makeindex", "makeglossaries", "pythontex", "bibtex"]
tools = ["biber", "makeindex"]

# Enable \write18 shell escapes. Default: false.
shell-escape = false

# Generate SyncTeX data. Default: true.
synctex = true

# Stop at the first TeX error instead of compiling past it. Default: true.
halt-on-error = true

# Give up if the build has not converged after this many engine passes.
# Default: 8.
max-passes = 8

# Stop any single engine or tool run after this many seconds. Default: no
# limit.
timeout = 300

# Install missing packages and tools with tlmgr, then rebuild. See USAGE.md,
# "Missing packages and programs". Default: false.
install-missing = false

[engine-args]
# Executable to spawn instead of the engine's default binary name.
program = "lualatex-dev"
# Options passed through to the engine verbatim, after maketex's own flags
# and before the .tex file.
extra-options = ["-cnf-line=max_print_line=1000"]

[watch]
# Polling interval for `maketex watch`. Default: 300.
interval-ms = 300
# Files or directories watched in addition to the build's recorded inputs.
# Directories contribute their regular files (non-recursive).
extra-paths = ["figures"]

[clean]
# Extensions removed by `maketex clean` on top of the built-in list
# (.aux, .log, .bbl, .toc, ...). A missing leading dot is added.
extra-extensions = [".tikz-cache"]
```

## Corresponding command-line flags

| Config key | Flag |
|---|---|
| `main` | positional `file` argument |
| `engine` | `--engine` |
| `engine-args.program` | `--engine-program` |
| `engine-args.extra-options` | `--engine-opt` (repeatable) |
| `tools` | `--tools a,b,c` |
| `output-dir` | `-o` / `--output-dir` |
| `shell-escape` | `--shell-escape` / `--no-shell-escape` |
| `synctex` | `--synctex` / `--no-synctex` |
| `halt-on-error` | `--halt-on-error` / `--no-halt-on-error` |
| `max-passes` | `--max-passes` |
| `timeout` | `--timeout` |
| `install-missing` | `--install-missing` / `--no-install-missing` |
| `watch.interval-ms` | `--interval-ms` (watch only) |
| `watch.extra-paths` | — (config only) |
| `clean.extra-extensions` | — (config only) |

Flags without a config counterpart: `--verbose`, `--plain`, `--config`,
`--no-config`, `--no-tui` (watch), `--all` and `--dry-run` (clean).

## Example: minimal project

```toml
main = "paper.tex"
engine = "pdflatex"
tools = ["biber"]
output-dir = "build"
```

With this in the project root, `maketex` builds the paper, `maketex watch`
rebuilds it on save, and `maketex clean` clears `build/` of everything but
the PDF.
