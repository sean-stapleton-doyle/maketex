# Full Program Execution: BibTeX Build

A step-by-step trace of every class instantiated, every graph node created, and
every decision made when running `maketex testdata/bibtex/main.tex` on a fresh
build. The document uses `\bibliographystyle{plain}` and `\bibliography{refs}`,
citing `knuth1984literate` and `lamport1994latex`.

---

## Startup — objects created in `main()`

```
tf::Executor executor(1)         — 1-thread taskflow pool; owns the thread
SQLite::Database db              — opens/creates .maketex.db in testdata/bibtex/
StateDatabase stateDb(db)        — applies schema (file_states, rules, rule_inputs);
                                   enables WAL journal mode
Blake3Hasher hasher              — stateless; computes BLAKE3 hashes of files
PdfLatexEngine engine(executor)  — holds a ref to executor; knows how to buildArgs()
BiberTool biber(executor)        — same structure, different buildArgs()
BibtexTool bibtex(executor)      — same structure, different buildArgs()
EngineInvocation inv {
  texFile   = .../testdata/bibtex/main.tex
  outputDir = .../testdata/bibtex/
  recorder  = true
  haltOnError = true
}
BuildDriver driver(engine, stateDb, hasher, inv,
                   maxPasses=8, biber=&biber, bibtex=&bibtex)
```

`BuildDriver` stores non-owning references/pointers to all of those. It also
owns a `BuildGraph m_graph`, which starts completely empty.

---

## `driver.run()` begins

Two local variables are set up:

```
FileStateTracker tracker(stateDb, hasher)
bool bibtexRan = false
```

`FileStateTracker` holds references to both `stateDb` and `hasher`. Its job is
the fast path: if a file's `(mtime, size)` matches what is stored in `stateDb`
it returns the cached hash without calling BLAKE3.

**Pre-flight FLS check.** `flsPath(inv)` = `.../testdata/bibtex/main.fls`. On a
fresh build this file does not exist, so `updateGraphFromFls` is skipped and the
graph stays empty.

---

## Pass 1

### `beforeSnapshot`

`takeSnapshot(tracker)` iterates `m_graph.files()` looking for nodes with
`role == Generated`. The graph is empty, so it returns `{}` immediately.

```
beforeSnapshot = {}
```

### Latex runs

`m_engine.run(inv)` is called. `EngineBase::run()`:

1. Calls `PdfLatexEngine::buildArgs(inv)`, producing:
   ```
   ["pdflatex", "-recorder", "-interaction=batchmode",
    "-halt-on-error", "-output-directory=.../testdata/bibtex/",
    ".../testdata/bibtex/main.tex"]
   ```
2. Submits a lambda to the taskflow executor that constructs
   `Process(args, workDir)` where `workDir = .../testdata/bibtex/`.
   `Process` calls `posix_spawnp`, setting up stdout/stderr pipes and using
   `posix_spawn_file_actions_addchdir` to start pdflatex in the tex file's
   directory (so relative `\input{}`/`\include{}` paths resolve correctly).
3. Returns a `std::future<EngineResult>`.

`future.get()` blocks until pdflatex exits. pdflatex:

- Reads `main.tex`, `article.cls`, many font/style files from TeX Live
- Reads `main.aux` at startup to resolve any prior forward references (on a
  fresh build this file doesn't exist yet — pdflatex creates it)
- Writes `main.aux` with `\citation{knuth1984literate}`,
  `\citation{lamport1994latex}`, `\bibstyle{plain}`, `\bibdata{refs}`
- Writes `main.fls`, `main.log`, `main.pdf` (citations show `[?]` placeholders)
- Exits 0

`result.passes` becomes 1. `latexRule.markRan(stateDb, true, "main.tex")` writes
a row to the `rules` table in SQLite.

### Graph update from `.fls`

`updateGraphFromFls` is called with the freshly written `main.fls`.
`FlsParser::parse()` reads it line by line.

It finds roughly:

- `PWD .../testdata/bibtex`
- `INPUT` lines: `main.tex`, `article.cls`, many font/style files in
  `/usr/local/texlive/…`, `main.aux` (LaTeX reads the aux at run start)
- `OUTPUT` lines: `main.log`, `main.aux`, `main.fls`, `main.pdf`

The parser deduplicates within its `seenIn`/`seenOut` sets and returns a
`FlsRecord` with two lists of absolute, canonicalised paths.

Back in `updateGraphFromFls`, a set `inputPaths` is built from all canonical
INPUT paths. Then for each path:

- **Every INPUT** → `m_graph.getOrAddFile(path, FileRole::Source)`.
  `BuildGraph` canonicalises the path with `fs::weakly_canonical`, allocates a
  `FileNode` on the heap, stores it in `m_files` (a
  `std::unordered_map<string, unique_ptr<FileNode>>`), and returns a reference.
  The `RuleNode` named `"pdflatex"` is also created here on the first call to
  `getOrAddRule`, and each file node is added to its input list.

- **Every OUTPUT** → checked against `inputPaths`:
  - `main.aux` appears in both INPUT and OUTPUT → `FileRole::Generated`. The
    node was already created as Source; `setRole(Generated)` upgrades it in
    place. Added to the latex rule's output list.
  - `main.log`, `main.pdf`, `main.fls` appear only in OUTPUT →
    `FileRole::Output`. New nodes, added to the latex rule's output list.

**FileNodes created after pass 1:**

| Path | Role |
|---|---|
| `main.tex` | Source |
| `/usr/local/texlive/…/article.cls` | Source |
| (many more TeX system files) | Source |
| `main.aux` | **Generated** |
| `main.log` | Output |
| `main.fls` | Output |
| `main.pdf` | Output |

**RuleNodes created:**

| Name | Inputs | Outputs |
|---|---|---|
| `"pdflatex"` | all Source nodes + `main.aux` (Generated) | `main.aux` (Generated), `main.log`, `main.fls`, `main.pdf` (Output) |

`main.aux` appears in both the input and output lists of `"pdflatex"`. This
self-loop is the structural signal that the latex rule must be iterated — it
reads and writes the same file.

### `afterSnapshot`

`takeSnapshot` iterates all file nodes. Only `main.aux` has `role == Generated`.
It calls `f->refresh(tracker)`:

- `tracker.snapshot(main.aux)` checks `stateDb` for a stored `(mtime, size)`.
  First run: nothing stored. Cold path: calls `hasher.hashFile(main.aux)` →
  BLAKE3 hash of the file contents. Stores `(mtime, size, hash)` in
  `stateDb.file_states`. Returns the `FileState`.
- `f->state().exists` is true, so the hash is recorded.

```
afterSnapshot = { "main.aux": hash_A }
filesChanged  = ({ "main.aux": hash_A } != {}) = true
```

The aux didn't exist before the first pass, so it counts as "changed".

### Log parse

`LogParser::parse(main.log)`. The log contains:

```
LaTeX Warning: Citation `knuth1984literate' on page 1 undefined
LaTeX Warning: Citation `lamport1994latex' on page 1 undefined
LaTeX Warning: There were undefined references.
```

No `"Rerun to get"` string, no biber signal.

```
log.rerunNeeded      = false
log.biberRerunNeeded = false
log.undefinedCitations = true   (noted but doesn't drive the loop)
```

### Biber check

`log.biberRerunNeeded` is false → biber block skipped entirely.

### Bibtex check

`bibtexRan == false`, so the check runs. `auxPath(inv)` = `main.aux`. It exists.
`AuxParser::parse(main.aux)`:

- `\citation{knuth1984literate}` → adds to `citations`
- `\citation{lamport1994latex}` → adds to `citations`
- `\bibdata{refs}` → adds `"refs"` to `bibDataFiles`
- `\bibstyle{plain}` → no matching prefix; ignored

Returns:
```
AuxRecord {
  citations:    ["knuth1984literate", "lamport1994latex"]
  bibDataFiles: ["refs"]
  inputs:       []
}
```

`!auxRecord.bibDataFiles.empty()` is true → bibtex must run.

`m_bibtex->run(makeToolInv(inv)).get()`. `makeToolInv` produces:
```
ToolInvocation {
  workingDir = .../testdata/bibtex/
  jobName    = "main"
  outputDir  = .../testdata/bibtex/
}
```

`BibtexTool::buildArgs` returns `["bibtex", "main"]`. `ToolBase::run()` submits
to the executor: `Process(["bibtex", "main"], workDir=".../testdata/bibtex/")`.
bibtex:

- Reads `main.aux` → sees `\bibstyle{plain}`, `\bibdata{refs}`,
  `\citation{…}`
- Reads `refs.bib` → finds the two entries
- Writes `main.bbl` (formatted bibliography in LaTeX markup)
- Writes `main.blg` (bibtex run log)
- Exits 0

```
bibtexRan   = true
filesChanged = true   (forced, regardless of snapshot comparison)
```

### Convergence check

```
!log.rerunNeeded && !filesChanged = !false && !true = false
```

Loop continues to pass 2.

---

## Pass 2

### `beforeSnapshot`

`takeSnapshot` hashes `main.aux`. Its `(mtime, size)` still match the value
stored in `stateDb` after pass 1, so the fast path is taken: no BLAKE3
recomputation.

```
beforeSnapshot = { "main.aux": hash_A }
```

### Latex runs

pdflatex runs again with identical argv. This time:

- Reads `main.aux` (has `\citation` entries and `\bibstyle`/`\bibdata`)
- Reads `main.bbl` — it now exists. `\bibliography{refs}` expands to
  `\input{main.bbl}`, so the bibliography section is typeset.
- Writes updated `main.aux` with resolved citation labels
  (e.g., `\bibcite{knuth1984literate}{1}`)
- Writes updated `main.pdf` (bibliography rendered, but in-text `\cite{…}`
  numerals may still be `[?]` if page numbers shifted)
- Exits 0

### Graph update

`updateGraphFromFls` runs again. The FLS now also lists `main.bbl` as an INPUT
(pdflatex read it via `\input`). `getOrAddFile("main.bbl", Source)` creates a
new FileNode. It appears only in INPUT, not OUTPUT, so it stays Source.

**New FileNode:** `main.bbl` → Source. Added to the `"pdflatex"` rule's input
list.

### `afterSnapshot`

Only `main.aux` is Generated. Its mtime changed (pdflatex rewrote it), so the
fast path is bypassed — BLAKE3 runs on the new contents. The file now contains
`\bibcite{…}` entries, so the hash is different.

```
afterSnapshot = { "main.aux": hash_B }
filesChanged  = (hash_B != hash_A) = true
```

### Log parse

pdflatex's log contains:

```
LaTeX Warning: Label(s) may have changed. Rerun to get cross-references right.
```

```
log.rerunNeeded = true
```

### Bibtex check

`bibtexRan == true` → the bibtex block is skipped. bibtex never runs again.

### Convergence check

```
!log.rerunNeeded && !filesChanged = !true && !true = false
```

Loop continues to pass 3.

---

## Pass 3

### `beforeSnapshot`

`main.aux` hashed. mtime changed again since pass 2 wrote it, so BLAKE3 runs →
returns `hash_B`.

```
beforeSnapshot = { "main.aux": hash_B }
```

### Latex runs

pdflatex:

- Reads `main.aux` (now with `\bibcite{…}` labels) and `main.bbl`
- All `\cite{}` in the text now resolve to numerals: `[1]`, `[2]`
- Writes `main.aux` — content is byte-for-byte identical to what it wrote in
  pass 2 (citation numbers stable, page numbers stable)
- Writes final `main.pdf` (fully resolved)
- Exits 0

### Graph update

No new files in the FLS. All `getOrAddFile` calls hit existing nodes. No
structural change to the graph.

### `afterSnapshot`

`main.aux` hashed. mtime updated (pdflatex always rewrites it), so the fast path
is bypassed and BLAKE3 runs — but the content is identical, so the hash is the
same.

```
afterSnapshot = { "main.aux": hash_B }
filesChanged  = (hash_B == hash_B) = false
```

### Log parse

No rerun warnings in the log.

```
log.rerunNeeded = false
```

### Bibtex check

`bibtexRan == true` → skipped.

### Convergence check

```
!log.rerunNeeded && !filesChanged = !false && !false = true
```

`result.success = true`, `result.passes = 3`. Returns immediately.

---

## Back in `main()`

```
maketex: done in 3 passes
```

`main()` returns 0. The taskflow executor's destructor joins the worker thread.
The `SQLite::Database` destructor checkpoints the WAL and closes the file. The
`.maketex.db` on disk now holds:

- `file_states` rows for `main.tex`, `main.aux`, `main.bbl`, and every TeX
  system file that appeared in the FLS
- A `rules` row for `"pdflatex"` recording the last run time and success
- `rule_inputs` rows storing the hash of each input at last run time (used by
  `RuleNode::isStale()` on the next incremental build)

---

## Final graph state

```
RuleNode "pdflatex"
  inputs:  main.tex        (Source)
           article.cls     (Source)
           … (TeX system files) …
           main.aux        (Generated)   ← self-loop: also an output
           main.bbl        (Source)      ← added in pass 2
  outputs: main.aux        (Generated)   ← self-loop
           main.log        (Output)
           main.fls        (Output)
           main.pdf        (Output)
```

`main.aux` in both inputs and outputs of the same rule is the structural
expression of why iteration is required. The bibtex tool has no `RuleNode` in
the graph — it is invoked imperatively by `BuildDriver` in this Stage 1
implementation. A future graph-aware scheduler would add a `"bibtex"` `RuleNode`
with edges `main.aux + refs.bib → main.bbl`, enabling the topological sort to
reason about it and run it in parallel with other independent rules.
