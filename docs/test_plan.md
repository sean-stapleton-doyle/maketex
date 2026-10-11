# Test gap plan

Status: proposed (2026-10-09).

maketex has 392 tests: unit tests plus an integration suite that builds the
`testdata/` fixtures with real engines. They all pass, including under
AddressSanitizer and UndefinedBehaviorSanitizer. This plan covers the code
those tests never run. Each gap is ranked by how likely it is to hide a
wrong result, and each comes with the seam that makes it testable.

## Progress

- **End-to-end suite** (`test/e2e/`, CTest label `e2e`): runs the real
  `maketex` binary through `CliRunner` (args, working directory, isolated
  `HOME`/cache, exit code, stdout, stderr) with a `FakeToolchain` of stub
  programs on `PATH`. It covers:
  - document discovery, `% !TEX root`, and the optional `.tex`;
  - exit codes 0, 1, 2, and 3;
  - JSON and GitHub output, `-o`, `maketex.toml`, and `--timeout`;
  - `watch --no-tui` rebuilding on an edit and on a missing `\input`
    appearing;
  - package installs with a fake `tlmgr` (writable tree, root-owned tree,
    `--no-install-missing`, `doctor --install-missing`);
  - paths with spaces and non-ASCII characters;
  - moved and copied projects.

  Its first run found four bugs, all fixed:
  - `-o build` was read as the `build` command;
  - moving a project reset its state, because the fingerprint held the
    document's directory;
  - `watch --no-tui` output piped to another program arrived only at exit;
  - a missing program cost a `tlmgr search` round trip on every build.
- This covers work item 3's CLI half and much of work item 4 (exit codes,
  JSON on failure). The `FakeToolchain` seam is built.

## How the gaps were found

Every new or changed code path was checked for a test that references it.
The tables list what had none. A line-coverage report doesn't exist yet;
work item 9 adds one, and its numbers should replace this list.

## Shared seams to build first

Three helpers unlock most of the work below. Build them before the
individual tests.

### A fake toolchain on `PATH`

A `FakeToolchain` test helper writes small shell scripts — `kpsewhich`,
`tlmgr`, `biber`, `makeglossaries`, `pygmentize` — into a temp `bin/`,
prepends it to `PATH`, and restores `PATH` afterwards. Each script appends
its arguments to a log file the test reads back, and its output and exit
code are set by the test. `test/PackageInstaller_test.cpp` already does
this by hand for `kpsewhich`; generalise that fixture.

This lets tests run the real `Process` and `launchProcess` code against
programs that aren't installed, or behave on demand: a `tlmgr` that
"installs" a `.sty` into a directory on `TEXINPUTS`, a `biber` that writes
a `.bbl`, a tool that hangs.

### Commands that write to streams, not `std::cout`

`runBuildCommand`, `runWatchCommand`, `runCleanCommand`, and
`runDoctorCommand` write straight to `std::cout` and `std::cerr`, so their
output and exit codes can't be checked in-process. Give each an output
pair (`std::ostream& out, std::ostream& err`), with `main` passing the
standard streams. This is a mechanical change and unlocks work item 4.

### Exposed internals with real behaviour

Move `BuildLock` (in `src/Build.cpp`) and `mirrorSourceDirectories`
behind small headers in `include/maketex/`, or into the `detail`
namespace, so they can be tested directly instead of only through
`runBuild`.

## Work items

Priorities: **P0** — wrong output or a broken feature can ship unnoticed.
**P1** — failure modes users will hit, now unverified. **P2** — hardening.

| # | Priority | Gap | Test to write | Seam |
|---|---|---|---|---|
| 1 | P0 | `BiberTool` has no unit tests and biber has never run on a development machine. | Unit: `applies` only when the pass wrote the `.bcf`; `inputs` lists the `.bcf` and every `.bcf` datasource found (beside the document, in the output directory, through `BIBINPUTS`); `outputs` is the `.bbl`. Driver: editing a `.bib` that only biber reads reruns biber. | `ToolContext` built from lambdas, as `BibtexTool_test.cpp` does. |
| 2 | P0 | `HashTriggeredTool` has no unit tests: makeglossaries' two triggers, and pythontex's unknown outputs (every run must be followed by an engine pass). | Unit: `applies` with either trigger, `inputs` omits missing triggers, `buildArgs` replaces `$job`. Driver: a tool with no declared outputs always sets `outputsChanged`. | Same as item 1. |
| 3 | P0 | The CI workflow has never run. Linux, full TeX Live, and the biber, glossaries, pythontex, minted, pgfplots, and luacode fixtures are all unverified. | Push `review-fixes` and fix whatever the first run finds. Then make every CI integration case required: fail the job if any case is skipped. | GitHub Actions. |
| 4 | P1 | Command layer: exit code 3, JSON printed when the build throws, `clean --all` on a shared database, the `-o .` protection for `.bbl`/`.ist`/`.xdy`, and `doctor`'s exit code and sections. | One test file per command, asserting stdout, stderr, and the exit code. `clean`: two documents share an output directory; `--all` on one keeps the database and the other document's state. | Stream-based commands; `FakeToolchain` for `doctor`. |
| 5 | P1 | `buildInstallingMissing`: the install-and-rebuild loop, the "never try a package twice" rule, the round limit, and the notes it adds. | A fake `tlmgr` that drops `minted.sty` into a `TEXINPUTS` directory. Assert two rounds, one `tlmgr install minted` call, and an "installed minted" note. A second fake that fails: assert one call and a failure note. | `FakeToolchain`; a fixture that loads two missing packages. |
| 6 | P1 | `StateDatabase`: the per-job fingerprint reset, `jobs()`, and `forgetJob()`. | Two jobs share a database; changing one job's fingerprint drops only its rules. `forgetJob` removes rules, `#outputs` rules, and the fingerprint row. | In-memory database (existing). |
| 7 | P1 | The build lock: nothing runs two builds of one document at once. | Two threads take `BuildLock` on the same path; the second waits until the first releases it. A lock in an unwritable directory doesn't block the build. | Exposed `BuildLock`. |
| 8 | P1 | `Process` escalation: a child that ignores SIGTERM is never tested. `--timeout` is only tested as parsing. | `sh -c 'trap "" TERM; sleep 30'` with a 200 ms timeout ends within 3 s. An integration case with `processTimeout` and a document that loops forever. | Existing `Process` tests. |
| 9 | P2 | No coverage numbers. | Add a `MAKETEX_COVERAGE` CMake option (Clang `-fprofile-instr-generate -fcoverage-mapping`) and a CI job that publishes an `llvm-cov` report. Set a floor on `src/` line coverage at the current level and raise it as items land. | CMake option and a CI job. |
| 10 | P2 | `tlmgrLookup` parsing and its cache, and `detectDistribution` for MiKTeX, Debian, and Fedora. | A fake `tlmgr` printing real `search --global --file` output; assert the parsed package and that the cache file answers a second lookup without running `tlmgr`. Fake `kpsewhich --version` / `TEXMFROOT` answers for each distribution. | `FakeToolchain`; cache directory under the temp dir. |
| 11 | P2 | Smaller paths: the xelatex retry (only the lualatex retry is tested), the 20,000-entry and `node_modules` limits in `mirrorSourceDirectories`, publishing an unchanged PDF without touching its mtime, the watch loop's engine pinning and missing-file watching. | One focused test each. Watch: drive `watchLoop` with a stop token and a fake build function. | Exposed `mirrorSourceDirectories`; for watch, inject the build function into `watchLoop`. |
| 12 | P2 | The "source saved mid-pass" check on coarse-timestamp filesystems (1 s HFS+, 2 s FAT) is only tested on APFS with fakes. | Document the limit and test that an edit in the same second never causes an endless rebuild. Making the check timestamp-independent would need a pre-pass hash and is a design change, not a test. | `FakeEngine` that sets mtimes explicitly. |

## End-to-end scenarios still to write

- `clean`: keeps the PDF; `--all` removes `.maketex/`; a shared output
  directory; `-o .` keeps a hand-written `.ist`; `--dry-run` deletes nothing.
- `watch`: a save during a build triggers another build; quitting while a
  pass runs leaves no TeX process behind; the TUI (needs a pseudo-terminal).
- The sudo confirmation (`[Y/n]`) under a pseudo-terminal, answered yes
  and no.
- Incremental: editing a figure; a new `\section` in a TOC document; a
  prose edit in an `\include`d chapter; a new `\index` entry; deleting the
  published PDF (republished without a rebuild) and `.maketex/` (one full
  rebuild).
- Two `maketex` processes building one document at once.
- Removing biblatex from a document stops biber runs.
- Failure fixtures: minted without `--shell-escape`, biblatex without
  biber, oscillating references (the pass limit), a missing
  `\includegraphics` file, a missing glyph.

## Order of work

1. Push the branch so CI runs (item 3). It costs nothing and may reorder
   everything else.
2. Unit-test the tool classes (items 1 and 2). They need no new seams and
   cover the code with the most untested branches.
3. Build the three shared seams.
4. Items 4–8, then items 9–12.

## Done when

- CI runs the full fixture suite on Linux with no skipped integration
  cases, and unit tests on macOS.
- Every row in the table has a test that fails if the behaviour it
  describes regresses. Check this by reverting the fix and watching the
  test fail.
- A coverage report is published per CI run, with a floor that only goes
  up.
