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
  diagnostic, not silent misbehaviour. Files inside the TeX distribution
  are not tracked, so a TeX Live upgrade alone does **not** invalidate the
  cache: include the TeX Live version in your cache key (see
  [Cache keys](#cache-keys)) if you want a rebuild after upgrading.
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

`category` is a stable machine tag — match on it rather than on message
text: `undefined-citation`, `undefined-reference`, `duplicate-label`,
`undefined-macro`, `missing-file`, `missing-tool`, `missing-font`,
`missing-glyph`, `shell-escape-required`, `package`, `class`, `font`,
`pdftex`, `overfull-hbox`, `overfull-vbox`, `underfull-hbox`,
`underfull-vbox`, `timeout`, `cancelled`, `environment`. `hint` is an
actionable next step (an install command, a flag to add) or `null`. `pdf`
is the published PDF, or `null` when none was produced or it could not be
published.

Exit codes: `0` success, `1` the document did not build, `2` usage or
configuration error, `3` environment error (engine or tool not installed,
database or file system trouble).

[workflow command]: https://docs.github.com/en/actions/using-workflows/workflow-commands-for-github-actions

## Cache keys

The `hashFiles(...)` key above gives an exact hit when sources are
unchanged and falls back to the newest previous cache (`restore-keys`)
otherwise — maketex then rebuilds only what the changed files invalidate.
Include the TeX Live version in the key (for example, from `kpsewhich
--version`) if a distribution upgrade should rebuild the document. maketex
doesn't track files inside the distribution, so without it a restored
cache counts as up to date even when the packages it used have changed.
