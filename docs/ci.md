# CI Integration

## GitHub Actions example

```yaml
jobs:
  paper:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/cache@v4
        with:
          path: paper/.maketex
          key: maketex-${{ runner.os }}-${{ hashFiles('paper/**/*.tex', 'paper/**/*.bib') }}
          restore-keys: maketex-${{ runner.os }}-
      # Install TeX Live or run inside a TeX Live container
      - run: maketex build paper/main.tex --format=github
      - uses: actions/upload-artifact@v4
        with:
          name: paper
          path: paper/main.pdf
```

## Caching `.maketex/`

Cache the `.maketex/` folder beside your root `.tex` file. It holds the SQLite state database and aux files.

- **Path-portable**: paths are stored relative to the document directory, so a restored cache hits even at a different checkout path.
- **mtime-independent**: `maketex` verifies BLAKE3 content hashes after a fresh checkout and exits `up to date` if files match.
- **Self-invalidating**: changing `maketex` version, engine, tools, or flags automatically resets the database to a clean rebuild. Include the TeX Live version in your cache key if TeX Live upgrades should trigger a rebuild.

## Output formats

### GitHub PR annotations (`--format=github`)

Emits `::error` and `::warning` workflow commands (`::error file=chapters/ch1.tex,line=12,title=LaTeX::...`) that show inline on pull request diffs.

### JSON (`--format=json`)

Prints one JSON object on stdout (notices go to stderr):

```json
{
  "formatVersion": 1,
  "success": false,
  "upToDate": false,
  "passes": 1,
  "error": "latex exited with code 1",
  "document": "/work/paper/main.tex",
  "engine": "pdflatex",
  "engineReason": null,
  "pdf": null,
  "diagnostics": [
    {
      "severity": "error",
      "file": "chapters/ch1.tex",
      "path": "/work/paper/chapters/ch1.tex",
      "line": 12,
      "message": "Undefined control sequence",
      "category": "undefined-macro",
      "context": "…",
      "hint": null
    }
  ]
}
```

Match on the stable `category` field (`undefined-citation`, `undefined-reference`, `duplicate-label`, `undefined-macro`, `missing-file`, `missing-tool`, `missing-font`, `missing-glyph`, `shell-escape-required`, `package`, `class`, `font`, `pdftex`, `overfull-hbox`, `overfull-vbox`, `underfull-hbox`, `underfull-vbox`, `timeout`, `cancelled`, `environment`).

Exit codes: `0` success, `1` build failed, `2` usage/config error, `3` environment error.
