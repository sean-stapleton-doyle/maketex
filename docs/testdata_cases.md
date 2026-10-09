# Test Fixture Cases

Each row is a self-contained LaTeX project under `testdata/`. The "Status"
column tracks whether the fixture has been created on disk.

## Success Cases

| Fixture | Engine | Passes | Aux tools | Key feature under test | Status |
|---|---|---|---|---|---|
| `pdflatex-simple` | pdflatex | 1 | — | Baseline: single-file, one pass, no extras | Added |
| `cross-references` | pdflatex | 2 | — | `\ref`/`\label` trigger a second pass | Added |
| `toc` | pdflatex | 2 | — | Table of contents as a rerun trigger | Added |
| `bibtex` | pdflatex | 3 | bibtex | Classic latex → bibtex → latex → latex | Added |
| `biber` | pdflatex | 3 | biber | biblatex + biber; same pass structure, different tool | Added |
| `makeindex` | pdflatex | 3 | makeindex | Index generation; the index adds a page, so a third pass settles `\@abspage@last` | Added |
| `glossaries` | pdflatex | 2 | makeglossaries | Glossary/acronym expansion | Added |
| `input-include` | pdflatex | 1 | — | `\input` + `\include` across multiple files; dependency tracking | Added |
| `graphics-raster` | pdflatex | 1 | — | `\includegraphics` with PNG/JPEG; image files tracked as deps | Added |
| `graphics-eps` | pdflatex | 1 | — | EPS figures with `%%CreationDate`; per-extension ignore pattern | Added |
| `pgfplots` | pdflatex | 1 | — | Inline TikZ/PGFPlots; no false reruns | Added |
| `minted` | pdflatex | 1 | pygmentize | `-shell-escape` required; syntax highlighting via minted | Added |
| `pythontex` | pdflatex | 2 | pythontex | Inline Python; generates aux files that trigger a rerun | Added |
| `xelatex-fontspec` | xelatex | 1 | — | XeLaTeX engine + fontspec; engine selection | Added |
| `lualatex-luacode` | lualatex | 1 | — | LuaLaTeX engine + inline Lua; third engine path | Added |

## Failure Cases

Fixtures under `testdata/failures/` exercise the error-surfacing system.
Each is expected to produce the documented exit code and error output.

### Hard Errors (exit ≠ 0, no PDF)

| Fixture | Engine / tool | Exit | Error kind | Key signal to extract |
|---|---|---|---|---|
| `failures/err-undefined-command` | pdflatex | 1 | Undefined control sequence | `! Undefined control sequence.` at l.8; command name `\undefinedmacro` |
| `failures/err-missing-package` | pdflatex | 1 | Missing package | `! LaTeX Error: File 'no-such-package-maketex-test.sty' not found.` |
| `failures/err-missing-input` | pdflatex | 1 | Missing input file | `! LaTeX Error: File 'this-file-does-not-exist.tex' not found.` |
| `failures/err-runaway-argument` | pdflatex | 1 | Unclosed brace / runaway argument | `! File ended while scanning use of \textbf` |
| `failures/err-unclosed-environment` | pdflatex | 1 | `\begin` without `\end` | `! LaTeX Error: \begin{center} on input line 26 ended by \end{document}.` |
| `failures/err-bibtex-missing-bib` | pdflatex + bibtex | 2 | Missing `.bib` file | `I couldn't open database file no-such-bib-file.bib` |
| `failures/err-bibtex-bad-entry` | pdflatex + bibtex | 2 | Malformed `.bib` entry | bibtex parse error with `(There was 1 error message)` |
| `failures/err-nested-input` | pdflatex | 1 | Error inside `\input`'d file | `./chapters/ch1.tex:9: Undefined control sequence.` — attribution must point at the chapter, not the root |
| `failures/err-missing-font` | xelatex | 1 | fontspec font not found | `Package fontspec Error: The font "No Such Font 2026" cannot be found.` → category `missing-font` |

### Warnings (exit 0, PDF produced)

| Fixture | Engine | Passes | Warning kind | Key signal to extract |
|---|---|---|---|---|
| `failures/warn-undefined-refs` | pdflatex | 1 | Undefined references | `LaTeX Warning: Reference 'X' on page N undefined` |
| `failures/warn-duplicate-label` | pdflatex | 2 | Multiply-defined label | `LaTeX Warning: Label 'sec:duplicate' multiply defined.` |
| `failures/warn-overfull-hbox` | pdflatex | 1 | Overfull `\hbox` | `Overfull \hbox (Xpt too wide) in paragraph at lines N--M` |
| `failures/warn-undefined-citation` | pdflatex | 1 | Undefined citation | `LaTeX Warning: Citation 'nosuchkey2026' ... undefined` → category `undefined-citation`, key kept in message |
| `failures/warn-package-warning` | pdflatex | 2 | Multi-line package warning | `Package hyperref Warning: ...` with `(hyperref)` continuations folded into one diagnostic |
