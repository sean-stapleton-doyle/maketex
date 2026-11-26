# Documentation

## User guides

- [Examples](../examples/) — minimal, multi-chapter thesis, XeLaTeX, and custom tool projects
- [Usage](USAGE.md) — CLI commands, flags, watch mode, doctor, clean
- [Editor setup](editors.md) — VS Code, Neovim, Zed, Emacs, TeXstudio
- [Configuration](configuration.md) — `maketex.toml` discovery, schema, precedence
- [CI integration](ci.md) — `.maketex/` caching, `--format=github`, `--format=json`
- [Packaging](packaging.md) — Homebrew tap, AUR (`maketex-bin`), shell installer

## Engineering docs

- [Architecture](architecture.md) — layers, data flow, key design decisions
- [Implementation progress](implementation_progress.md) — current status
- [Roadmap](next-up.md) — known gaps and upcoming work
- [Testdata cases](testdata_cases.md) — fixture inventory
- [BibTeX walkthrough](bibtex_execution_walkthrough.md) — 3-pass build trace

## Design documents

| Document | Status |
|---|---|
| [Compile API & Sandboxing Design](api_and_sandboxing_design.md) | Proposed (2026-11-26) |
| [Test Gap Plan](test_plan.md) | Proposed (2026-10-09) |
| [Diagnostics Plan](diagnostics_plan.md) | Implemented (2026-06-08) |
| [Process Execution Plan](process_execution_plan.md) | Superseded by `architecture.md` |
| [latexmk Replacement](latexmk_replacement.md) | Original specification |
