# maketex

[![CI](https://github.com/sean-stapleton-doyle/maketex/actions/workflows/ci.yml/badge.svg)](https://github.com/sean-stapleton-doyle/maketex/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/sean-stapleton-doyle/maketex)](https://github.com/sean-stapleton-doyle/maketex/releases/latest)
[![License: MIT](https://img.shields.io/github/license/sean-stapleton-doyle/maketex)](LICENSE)

A modern, zero-config C++ replacement for `latexmk`. Run `maketex` in a LaTeX project and it finds the root document, picks the right engine, drives auxiliary tools until convergence, hides build clutter in `.maketex/`, installs missing TeX Live packages, and prints clean compiler-style diagnostics.

## Quick start

```sh
cd my-paper
maketex                 # compile once; publishes main.pdf beside main.tex
maketex --preamble-cache # cache precompiled pdflatex preamble (.fmt) for faster body edits
maketex watch           # rebuild instantly on source or .bib edits (kqueue / inotify)
maketex doctor          # check your TeX engines, tools, and document packages
maketex clean           # remove aux files (keep PDF); add --all for a pristine tree
```

## Key features

- **Zero-touch defaults**: discovers the root `.tex` file (skipping `subfiles` chapters and following `% !TEX root`), detects `pdflatex` / `xelatex` / `lualatex` (including TikZ `graphdrawing` and automatic fallback for class-hidden Unicode requirements), and keeps intermediate files inside a `.gitignore`'d `.maketex/` folder.
- **Fast incremental & multi-pass builds**:
  - Tracks inputs via `-recorder` (`.fls`) and BLAKE3 content hashes in SQLite so unchanged builds exit immediately (`✓ up to date`) and edits run only the passes and tools actually needed.
  - Speeds up multi-pass builds with cold pass-1 `-draftmode` (`pdflatex`/`lualatex`), XeLaTeX `-no-pdf` with a single `xdvipdfmx` finalization pass, and parallel execution of independent auxiliary tools.
  - Optional precompiled preamble `.fmt` caching (`--preamble-cache` / `preamble-cache = true`) for `pdflatex` with exact `file:line` error attribution and automatic invalidation on preamble edits.
  - Native OS file watching (`kqueue` on macOS, `inotify` on Linux) in `maketex watch`.
- **Built-in auxiliary tools**: `biber`, `bibtex`, `makeindex`, `nomencl`, `makeglossaries` (including custom glossary types), `bib2gls`, and `pythontex`.
- **Automatic package installation**: on TeX Live (MacTeX / BasicTeX), batches missing preamble packages into a single `tlmgr install` (confirming before `sudo` on root-owned trees).
- **Clean diagnostics & CI support**: compiler-style terminal output with source snippets, `--format=json`, `--format=github` PR annotations, and configurable warning policies (`--warnings-as-errors`, `fail-on`, `ignore`).
- **Powerful `maketex.toml` configuration**: declarative custom tools (`[[tool]]`), `texinputs`/`bibinputs`/`[env]`, `[content-filters]`, `[diagnostics]`, `preamble-cache`, and `jobname`.

## Installation

### Prebuilt binaries

Download the archive for your platform (`macos-arm64`, `macos-x86_64`, `linux-x86_64`, or `linux-arm64`) from [GitHub Releases](https://github.com/sean-stapleton-doyle/maketex/releases), extract it, and place `bin/maketex` on your `PATH`.

### Building from source

Requires a C++26-capable compiler (Clang / GCC 14+), CMake 3.25+ (newer CMake on GCC 14+ for C++26), and Ninja or Make. Dependencies are fetched automatically via CPM.

```sh
make release                                # build optimized Release binary
make install PREFIX=/usr/local              # install stripped `maketex` binary
```

Run tests:

```sh
make test                                   # full suite (unit + integration + e2e)
make test-unit                              # fast unit tests only
make help                                   # list all Makefile targets (format, release, tag-release, ...)
```

*(You can also invoke CMake directly: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && cmake --install build --component maketex`.)*

## Documentation

- [Usage guide](docs/USAGE.md)
- [Configuration (`maketex.toml`)](docs/configuration.md)
- [CI integration](docs/ci.md)
- [Architecture](docs/architecture.md)
- [API & sandboxing design](docs/api_and_sandboxing_design.md)
- [Known gaps & roadmap](docs/next-up.md)

## License

`maketex` is released under the [MIT License](LICENSE).
