# Next up

Known gaps, deliberately out of scope so far. Each is a feature, not a bug:
the documents they affect fail with a clear diagnostic rather than a wrong
PDF.

## Auxiliary tools not yet supported

- **nomencl** (`.nlo` → `.nls` via `makeindex -s nomencl.ist`).
- **Multiple indexes**: imakeidx, splitindex, multind, and the index
  package write `.idx` files other than `<job>.idx`; only that one is run.
- **Other glossary back ends**: bib2gls (glossaries-extra) and
  makeglossaries-lite.
- **Per-unit bibliographies**: multibib, bibunits, and chapterbib write one
  `.aux` per unit, each needing its own bibtex run.
- **xindy / texindy / upmendex** in place of makeindex.
- **biber with an output directory**: biber runs from the output directory
  instead of receiving `--input-directory`/`--output-directory`.
- **pythontex and minted caches** under a separate output directory.

## Engines and formats

- The latex → dvips → ps2pdf route and xelatex's `-no-pdf` + xdvipdfmx
  split.

## Design notes carried forward

- `EngineInvocation` still mixes configuration with per-build context
  (review 06-09, M1).
- Watch mode polls; a native file-event backend (FSEvents, inotify) would
  cut latency and idle CPU.
