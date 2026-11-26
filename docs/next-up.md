# Next up

Known gaps, deliberately out of scope so far. Each is a feature, not a bug:
the documents they affect fail with a clear diagnostic rather than a wrong
PDF.

## Auxiliary tools not yet supported

- **Multiple indexes**: imakeidx, splitindex, multind, and the index
  package write `.idx` files other than `<job>.idx`; only that one is run
  (can be wired via `[[tool]]` in `maketex.toml`).
- **Per-unit bibliographies**: multibib, bibunits, and chapterbib write one
  `.aux` per unit, each needing its own bibtex run.
- **Alternative index/glossary binaries**: xindy / texindy / upmendex and
  makeglossaries-lite (can be wired via `[[tool]]` in `maketex.toml`).
- **TikZ externalize `mode=list and make`** parallel makefile execution.

## Platforms and distribution

- Windows support (`Process`, file locking, `;` path separators).
- Homebrew tap / package-manager formulas (GitHub Release tarballs are built via `.github/workflows/release.yml`).

## Engines and formats

- The latex → dvips → ps2pdf route (XeLaTeX `-no-pdf` + `xdvipdfmx` split is now built in).

## Design notes carried forward

- `EngineInvocation` still mixes configuration with per-build context
  (review 06-09, M1).
- Compile API & multi-tenant sandboxing: see [api_and_sandboxing_design.md](api_and_sandboxing_design.md).
