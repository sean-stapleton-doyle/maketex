+++
title = 'maketex in CI'
date = '2026-06-12T00:00:00Z'
+++

# maketex in CI

maketex is designed to be dropped into a pipeline without ceremony: one
binary, one command, machine-readable output, and a build cache that
survives being restored on a different runner.

## The short version

```yaml
# .github/workflows/paper.yml
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
      # ... install TeX Live or use a texlive container ...
      - run: maketex build paper/main.tex --format=github
      - uses: actions/upload-artifact@v4
        with:
          name: paper
          path: paper/main.pdf
```

## What to cache

Cache the `.maketex/` directory next to your root document. It contains
everything an incremental rebuild needs: the state database (file hashes,
rule records, the build manifest) and the engine's recorder/aux outputs.

Properties that make this safe:

- **Path-portable.** Paths are stored relative to the document's
  directory, and the recorder file is re-rooted on parse, so a cache
  restored at a different absolute checkout path still hits. Sources
  outside the document's directory fall back to absolute paths and simply
  rebuild after a move — never a wrong result.
- **Self-invalidating.** The database carries a manifest (schema version
  plus a fingerprint of the engine, tool roster, and build flags). A cache
  from an incompatible maketex version or different build options is
  detected on open and reset — you get a clean full build and a `note:`
  diagnostic, not silent misbehaviour. A different TeX Live version
  changes the hashes of tracked distribution files and rebuilds for the
  same reason.
- **mtime-independent.** A fresh checkout gives every file a new mtime.
  maketex's fast path misses, falls back to BLAKE3 content hashing, finds
  the hashes unchanged, and still exits "up to date". Restoring a cache
  costs one hashing pass, not a rebuild.

## Machine-readable output

`--format=github` emits one [workflow command] per diagnostic —
`::error file=chapters/ch1.tex,line=12,title=LaTeX::Undefined control
sequence` — which GitHub renders as inline annotations on the PR diff.
Warnings (undefined citations, overfull boxes, package warnings) become
`::warning` annotations.

`--format=json` prints exactly one JSON document on stdout (notices go to
stderr), suitable for any other CI system or tooling:

```json
{
  "formatVersion": 1,
  "success": false,
  "upToDate": false,
  "passes": 1,
  "error": "latex exited with code 1",
  "document": "/work/paper/main.tex",
  "engine": "pdflatex",
  "pdf": null,
  "diagnostics": [
    {
      "severity": "error",
      "file": "chapters/ch1.tex",
      "path": "/work/paper/chapters/ch1.tex",
      "line": 12,
      "message": "Undefined control sequence",
      "category": "undefined-macro",
      "context": "…"
    }
  ]
}
```

`category` is a stable machine tag (`undefined-citation`,
`undefined-reference`, `missing-file`, `missing-font`, `missing-glyph`,
`package`, `overfull-hbox`, `undefined-macro`) — match on it rather than on
message text.

Exit codes are conventional: `0` success, `1` build failure or runtime
error, `2` configuration error.

[workflow command]: https://docs.github.com/en/actions/using-workflows/workflow-commands-for-github-actions

## Cache keys

The `hashFiles(...)` key above gives an exact hit when sources are
unchanged and falls back to the newest previous cache (`restore-keys`)
otherwise — maketex then rebuilds only what the changed files invalidate.
Including the TeX Live version in the key (e.g. from `kpsewhich
--version`) avoids paying one detection-triggered full rebuild after a
distribution upgrade, but is optional: correctness never depends on the
key.
