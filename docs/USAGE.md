# Usage Guide

`maketex` runs your TeX engine and auxiliary tools until the document converges. It skips up-to-date work and prints compiler-style errors.

```sh
maketex [build] [file.tex] [options]   # build once (default)
maketex watch   [file.tex] [options]   # rebuild on file changes
maketex clean   [file.tex] [options]   # remove generated files
maketex doctor  [file.tex] [options]   # check TeX engines, tools, and packages
```

## Root document discovery

`build` and `file.tex` are optional. `maketex` picks the root file in this order:

1. The file argument (`maketex thesis` also works; `.tex` is optional).
2. `main` in [`maketex.toml`](configuration.md).
3. The `.tex` file in the current directory that starts with `\documentclass` (ignoring `subfiles` chapters). If several match, `main.tex` or `<dirname>.tex` wins.

If a file contains `% !TEX root = ../thesis.tex`, `maketex` builds that root file instead.

## Building

```sh
maketex thesis.tex
maketex thesis.tex --engine lualatex --synctex -o build
```

By default, build files (`.aux`, `.log`, `.fls`, `.maketex.db`) go into `.maketex/` beside your document. `maketex` copies `<job>.pdf` and `<job>.synctex.gz` back next to the `.tex` file and creates `.maketex/.gitignore`. Set `-o <dir>` (or `output-dir` in config) to keep all outputs, including the PDF, in that directory.

`maketex` records every input with `-recorder` and stores BLAKE3 hashes in `.maketex.db`. If nothing changed, the next build exits immediately (`✓ up to date`). Paths are stored relative to the document directory, so caches stay valid after moving the project or restoring a CI cache.

### Engine detection

Unless you pass `--engine`, `maketex` picks the engine automatically:

1. `% !TeX program = xelatex` near the top of the file.
2. Preamble packages: `luacode` / `\directlua` / TikZ `graphdrawing` → `lualatex`; `xeCJK` / `mathspec` → `xelatex`; `fontspec` / `unicode-math` / `polyglossia` → `lualatex`.
3. Otherwise, `pdflatex`.

If a custom class requires XeLaTeX or LuaLaTeX, `maketex` detects the failure message, retries with that engine, and saves the choice in `.maketex.db`.

### Auxiliary tools

Default roster: `biber`, `makeindex`, `nomencl`, `makeglossaries`, `bib2gls`, `pythontex`, `bibtex`.

A tool runs only when the last pass wrote its trigger file and its inputs changed. Independent tools run in parallel. Use `--tools a,b` to override the roster, or add custom tools with `[[tool]]` in [`maketex.toml`](configuration.md).

### Missing packages

When a TeX Live package is missing, `maketex` points at the `\usepackage` line and installs missing packages in a batched `tlmgr install` call:

- **Writable TeX tree**: installs without prompting.
- **Root-owned tree** (default on MacTeX / BasicTeX): prompts before running `sudo tlmgr install`. Pass `--install-missing` to skip the prompt, or `--no-install-missing` to disable auto-install.
- **Watch mode, CI, or JSON/GitHub output**: never prompts; prints the install command instead.

On Debian, Fedora, and MiKTeX, `maketex` prints the matching `apt`, `dnf`, or `miktex` command.

Documents that run external programs (`minted`, `svg`) require `--shell-escape`. `maketex` never enables it automatically.

### Build options

| Flag | Description |
|---|---|
| `--jobname <name>` | Override the TeX job name (PDF and aux basename) |
| `--engine <name>` | `pdflatex`, `xelatex`, or `lualatex` (auto-detected if unset) |
| `--engine-program <bin>` | Custom engine executable |
| `--engine-opt <opt>` | Pass a raw option to the engine (repeatable) |
| `--tools a,b,c` | Replace the default auxiliary tool roster |
| `-o`, `--output-dir <dir>` | Output directory (default: `.maketex/`, PDF copied beside `.tex`) |
| `--format <fmt>` | `human` (default), `json`, or `github` (`build` only) |
| `--shell-escape` / `--no-shell-escape` | Enable `\write18` (default: off) |
| `--synctex` / `--no-synctex` | Generate SyncTeX data (default: on) |
| `--preamble-cache` / `--no-preamble-cache` | Cache compiled `pdflatex` preamble as `.fmt` (default: off) |
| `--halt-on-error` / `--no-halt-on-error` | Stop at the first TeX error (default: on) |
| `--warnings-as-errors` / `--no-warnings-as-errors` | Fail build on warnings (default: off) |
| `--max-passes <n>` | Maximum engine passes (default: `8`) |
| `--timeout <seconds>` | Kill any engine or tool run after `<seconds>` |
| `--install-missing` / `--no-install-missing` | Auto-install missing TeX Live packages (default: on) |
| `-v`, `--verbose` | Show raw log context under diagnostics |
| `--plain` | Disable terminal colors and styling |
| `--config <path>` / `--no-config` | Use a specific config file or ignore config files |

### Exit codes

| Code | Meaning |
|---|---|
| `0` | Success (or `--help` / `--version`) |
| `1` | Build failed (TeX or tool error, or did not converge) |
| `2` | Usage error (bad flag, invalid config, missing input file) |
| `3` | Environment error (missing engine/tool, filesystem or DB error) |
| `130`, `143` | Stopped by `SIGINT` or `SIGTERM` (also stops child processes) |

## Watching

```sh
maketex watch thesis.tex
```

`maketex watch` builds once, then watches all recorded source files (`.tex`, `.bib`, figures, preamble inputs) with `kqueue` on macOS and `inotify` on Linux. Generated files like `.aux` are ignored so builds never loop.

TUI keys:

| Key | Action |
|---|---|
| `q` / `Ctrl-C` | Quit and stop any running build |
| `r` | Force a rebuild |
| `v` | Toggle raw log context |

Pass `--no-tui` to print one report per build instead of the full-screen UI. Pass `--interval-ms <n>` to adjust the fallback poll interval (default: `300`).

## Doctor

```sh
maketex doctor
maketex doctor thesis.tex
maketex doctor thesis.tex --install-missing
```

Lists your TeX distribution, engines, and tools. With a `.tex` file, also checks its class, packages, required tools, and whether `--shell-escape` is needed. Exits `0` if everything needed is installed, or `3` otherwise.

## Cleaning

```sh
maketex clean thesis.tex            # remove aux files, keep PDF
maketex clean thesis.tex --all      # also remove PDF, SyncTeX, and .maketex/
maketex clean thesis.tex --dry-run  # preview files without deleting
```

Never deletes `.tex` source files. Add extra extensions under `[clean] extra-extensions` in [`maketex.toml`](configuration.md).
