# Examples

Each folder is a self-contained project. Run `maketex` inside any folder to build it.

| Example | What it shows |
|---|---|
| [minimal](minimal/) | Zero-config single-file build (`maketex`) |
| [thesis](thesis/) | Multi-chapter project with `maketex.toml`, `texinputs`, BibTeX, `makeindex`, `--preamble-cache`, and `[diagnostics]` |
| [xelatex-unicode](xelatex-unicode/) | Automatic XeLaTeX detection with cross-references and TOC |
| [tikz](tikz/) | TikZ diagram with automatic `\tikzexternalize` caching inside `.maketex/` |
| [custom-tool](custom-tool/) | Declarative `[[tool]]` rule and `[env]` in `maketex.toml` |
