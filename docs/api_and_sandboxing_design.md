# Compile API & Sandboxing Design (Overleaf Alternative)

> **Status: proposed (2026-11-26).** Design document for embedding
> `maketex::maketex` inside a Drogon C++ HTTP/WebSocket service to power an
> open-source Overleaf alternative.

## Motivation

`maketex` is structured as a standalone C++ core library (`maketex::maketex`,
depending only on `BLAKE3` and `SQLiteCpp`) with a thin CLI layer on top. That
boundary makes it well-suited to power a hosted or self-hosted collaborative
LaTeX editor backend (using [Drogon](https://drogon.org/)).

However, compiling **untrusted, multi-tenant LaTeX** inside a long-lived async
web server has fundamentally different security, isolation, and concurrency
requirements from a local CLI tool. This document captures the threat model,
the minimal seams needed in `maketex` core, the sandboxing strategy, and the
Drogon API architecture.

---

## High-Level Architecture

```
Browser (CodeMirror + PDF.js)
        │  HTTPS / WebSocket
        ▼
┌────────────────────────────────────────────────────────┐
│  Drogon API Server (async IO event loops)              │
│   ├─ REST: compile trigger, PDF download, SyncTeX      │
│   ├─ WS:   live BuildObserver progress & diagnostics   │
│   └─ ProjectCoordinator (per-project latest-wins       │
│        std::stop_source + bounded CompileWorkerPool)   │
└───────────────────────────┬────────────────────────────┘
                            │ worker threads (never IO threads)
                            ▼
┌────────────────────────────────────────────────────────┐
│  maketex::maketex (core library)                       │
│   ├─ BuildDriver (fixed-point loop, BLAKE3, SQLite)    │
│   ├─ DiagnosticExtractor (structured errors/warnings)  │
│   └─ IProcessRunner seam (injected by server)          │
└───────────────────────────┬────────────────────────────┘
                            │ sandboxed exec (bwrap / nsjail / gVisor)
                            ▼
┌────────────────────────────────────────────────────────┐
│  Isolated Compile Sandbox (per engine/tool invocation) │
│   ├─ RO bind: TeX Live tree, /usr, /lib                │
│   ├─ RW bind: /work (single project dir + .maketex/)   │
│   ├─ No network, clean env, private tmpfs /tmp         │
│   └─ cgroups v2 (CPU, RAM, PIDs) + rlimits + timeout   │
└────────────────────────────────────────────────────────┘
```

---

## Threat Model

TeX is Turing-complete and designed for single-user desktops, not untrusted
tenants. Even with `--shell-escape` disabled, a malicious `.tex` file can
attempt:

| Threat | Example Vector | Mitigation |
|---|---|---|
| **Server secret exfiltration** | `\directlua{tex.print(os.getenv("DATABASE_URL"))}` | Clean child environment (`inheritEnv = false` + explicit allowlist) |
| **Host file read / cross-tenant read** | `\input{/etc/passwd}`, Lua `io.open("/var/lib/projects/other/main.tex")`, `% !TEX root = ../../other/main.tex`, uploaded symlinks | Mount namespace exposing *only* `/work` + RO TeX tree; strip/reject symlinks escaping project root; enforce root containment in `DocumentProbe` |
| **Host file write** | `\openout` to `../` or `/tmp` | Mount namespace + `openout_any=p` + private `tmpfs` `/tmp` |
| **Network exfiltration / SSRF** | LuaSocket, `biber` remote `<bcf:datasource type="url">`, `tlmgr` | Unshared network namespace (`--unshare-net` / no network interface); disable auto-install in server mode |
| **CPU / infinite loop DoS** | `\def\x{\x}\x` | Per-pass & total compile timeouts + process-group kill (`SIGKILL`) + cgroup CPU quota |
| **Memory / OOM DoS** | TeX hash/pool exhaustion, giant `\message` stdout spam | cgroup `memory.max` + `WaitLimits::maxOutputBytes` cap in `Process` |
| **Disk / inode bomb** | `\loop \write` generating GBs of aux/log/PDF | `RLIMIT_FSIZE` + project workspace disk quota + `tmpfs` size limit |
| **Fork / orphan bomb** | Restricted `\write18` (`epstopdf`/`gs`) or enabled shell-escape spawning grandchildren | `POSIX_SPAWN_SETPGROUP` + `kill(-pgid, ...)` + cgroup `pids.max` |

---

## Required Seams in `maketex` Core

`maketex::maketex` already avoids `stdout`/`stderr`, signal handlers, `chdir()`,
and `setenv()`. Only a small set of core additions are needed to make it
server-ready without coupling the library to any specific sandbox or web
framework:

### 1. `IProcessRunner` / `ProcessLaunchSpec` seam
Currently `EngineBase` and `BuildDriver` call `launchProcess(...)` which
constructs `maketex::Process` directly. Introducing an optional runner hook on
`BuildOptions`:

```cpp
struct ProcessLaunchSpec {
    std::vector<std::string> argv;
    std::filesystem::path workingDir;
    std::vector<std::pair<std::string, std::string>> env;
    bool inheritEnv = true;
    WaitLimits limits;
};

using ProcessRunner = std::function<ProcessResult(const ProcessLaunchSpec&)>;
```

- **Default (CLI)**: spawns directly via `maketex::Process` (zero behavior change).
- **Server**: injects a runner that wraps `argv` in `bwrap` / `nsjail` (remapping
  host project paths to `/work` inside the sandbox and mapping them back if
  needed), or dispatches over gRPC/Unix socket to a dedicated sandboxed compile
  worker pool.

### 2. Environment isolation (`inheritEnv = false`)
Add `bool inheritEnv = true` (and optional explicit `baseEnv`) to `Process` and
`BuildOptions`. When `inheritEnv == false`, `Process` constructs `cenvp`
strictly from `baseEnv` + `extraEnv` instead of copying the host `environ`.

### 3. Process-group teardown (`POSIX_SPAWN_SETPGROUP`)
In [src/Process.cpp](../src/Process.cpp), place each spawned child in its own
process group (`POSIX_SPAWN_SETPGROUP`, `pgroup = 0`) and signal `-m_pid` in
`kill()` and `terminateAndReap()`. This guarantees that timeouts and
cancellations reap TeX *and* any grandchild processes (`epstopdf`, `gs`,
`pygmentize`).

### 4. Bounded output capture (`WaitLimits::maxOutputBytes`)
Add `std::optional<std::size_t> maxOutputBytes` to `WaitLimits`. Once captured
stdout+stderr exceeds the cap, continue draining the pipes (so the child does
not deadlock on a full pipe) while discarding excess bytes and setting a
`truncated` flag on `ProcessResult`.

### 5. Path containment & pre-configured TeX roots
- Add a `std::optional<std::filesystem::path> projectRoot` guard (or validate
  in `resolveRootDocument` / `BuildOptions`) so `% !TEX root` and relative
  paths cannot escape the tenant's workspace.
- Allow callers to pass `std::optional<std::vector<std::filesystem::path>>
  texSystemRoots` in `BuildOptions` so containerized/sandboxed workers don't
  probe the host `kpsewhich` when TeX Live lives at a fixed path inside the
  sandbox.
- Add a small path-sanitization helper (or document the boundary contract):
  API responses must return `Diagnostic::file` (project-relative) and **never**
  expose `Diagnostic::sourcePath` or absolute host paths in `BuildResult::published`.

---

## Sandboxing Strategy

Sandboxing should be layered — TeX config hardening alone is **not** sufficient
(LuaLaTeX exposes full Lua I/O, and `pdftex`/`xetex` still have decades-old C
parsers).

### Layer 1: TeX-level hardening (defense in depth)
Provide a locked-down `texmf.cnf` via `TEXMFCNF` inside the sandbox:
- `openin_any = p` (paranoid: no reading files outside current/subdirectories)
- `openout_any = p` (paranoid: no writing outside output directory)
- `shell_escape = f` (disabled by default; never enable for untrusted tenants,
  or only `p` with a minimal `shell_escape_commands` allowlist)
- Disable `biber --validate-control` remote URL fetches (`--no-remote` / network
  namespace)

### Layer 2: Linux Namespace Sandbox (`bwrap` or `nsjail`) — Recommended Default
For self-hosted and single-host Linux deployments, **Bubblewrap (`bwrap`)** or
**`nsjail`** provides strong isolation with ~5–15 ms overhead per invocation
(preserving fast incremental builds and `.fmt` preamble caches):

```sh
bwrap \
  --unshare-all \
  --die-with-parent \
  --new-session \
  --ro-bind /usr /usr \
  --ro-bind /lib /lib \
  --ro-bind /lib64 /lib64 \
  --ro-bind /usr/local/texlive /usr/local/texlive \
  --bind /var/lib/maketex-api/projects/<id> /work \
  --tmpfs /tmp \
  --proc /proc \
  --dev /dev \
  --chdir /work \
  --clearenv \
  --setenv PATH /usr/local/texlive/bin/x86_64-linux:/usr/bin:/bin \
  --setenv HOME /tmp \
  --setenv TEXMFVAR /tmp/texmf-var \
  --setenv TEXMFCNF /etc/maketex-sandbox: \
  -- pdflatex ...
```

Pair with:
- **cgroups v2** (`memory.max=1G`, `memory.swap.max=0`, `cpu.max=100000 100000`,
  `pids.max=64`)
- **rlimits** (`RLIMIT_FSIZE=100MB`, `RLIMIT_NOFILE=256`)
- **seccomp-bpf** profile (built directly into `nsjail`)
- **Pre-build symlink check**: reject any symlink inside `/work` whose target
  resolves outside `/work`.

### Layer 3: Public Multi-Tenant SaaS (Strong Kernel Isolation)
If exposing the service to anonymous public users on the internet, run each
compile worker (or per-compile container) inside **gVisor (`runsc`)** or
**Firecracker / Kata microVMs** to protect against Linux kernel privilege
escalation exploits. Because `maketex`'s `IProcessRunner` abstracts process
execution, swapping from `bwrap` to gVisor/remote workers requires zero changes
to `BuildDriver`.

---

## Drogon API Design

### Threading & Concurrency Model
- **Never call `runBuild()` on a Drogon IO loop thread.** `runBuild()` blocks
  on `poll(2)`, `flock(2)`, and SQLite I/O.
- Run compiles on a dedicated, bounded `CompileWorkerPool` (e.g., `std::jthread`
  pool sized to `min(cores, maxConcurrentCompiles)` with a bounded queue and
  HTTP `429 Too Many Requests` backpressure).
- **Per-project latest-wins cancellation**:
  - Maintain a `std::unordered_map<ProjectId, ActiveCompile>` holding a
    `std::stop_source` and monotonic `generation` counter.
  - When a user triggers a new compile (e.g. rapid Cmd+S in the editor), call
    `active.stopSource.request_stop()` on the in-flight build and schedule the
    new generation immediately instead of piling up builds behind `BuildLock`.
- **Event bridging**: `BuildOptions::observer` runs on the worker thread; post
  progress payloads onto Drogon's WebSocket connections via
  `wsConn->getLoop()->queueInLoop(...)` (or `wsConn->send(...)`, which Drogon
  makes thread-safe).

### Endpoints

| Method & Path | Purpose |
|---|---|
| `POST /api/v1/projects/{id}/compile` | Trigger a build (`main`, optional `engine`, `draft`, `synctex`). Returns `202 Accepted` with `{ "compileId": "..." }` (or `200` for synchronous short-poll mode). |
| `WS /api/v1/projects/{id}/compile/stream` | Streams live `BuildEvent`s (`pass_started`, `tool_started`, `rerun_scheduled`, `skipped_up_to_date`, `completed`) and final structured diagnostics. |
| `GET /api/v1/projects/{id}/pdf` | Serves the published PDF with `ETag` (BLAKE3 hash) and HTTP Range support for PDF.js. |
| `GET /api/v1/projects/{id}/synctex/forward?file=...&line=...&col=...` | Forward search (source line → PDF page & bounding box) via `synctex view`. |
| `GET /api/v1/projects/{id}/synctex/inverse?page=...&x=...&y=...` | Inverse search (PDF click → source file & line) via `synctex edit`. |
| `DELETE /api/v1/projects/{id}/cache` | Cleans `.maketex/` (including `.aux`, `.maketex.db`, and `.fmt` cache) for a clean rebuild. |

### JSON Result Contract (Sanitized)
```json
{
  "compileId": "cmp_01J...",
  "success": true,
  "upToDate": false,
  "engine": "pdflatex",
  "engineReason": "",
  "passes": 2,
  "durationMs": 412,
  "pdfEtag": "blake3:9f4c...",
  "synctexAvailable": true,
  "diagnostics": [
    {
      "severity": "warning",
      "category": "undefined-citation",
      "file": "chapters/intro.tex",
      "line": 42,
      "message": "Citation 'knuth84' on page 2 undefined",
      "hint": ""
    }
  ]
}
```

### Workspace & Cache Lifecycle
- Each project gets an isolated directory `/var/lib/maketex-api/projects/<id>/`
  with its own `.maketex/` subdirectory holding `.maketex.db`, `.aux`/`.bbl`,
  and precompiled `.fmt` preamble caches.
- Keeping `.maketex/` warm between edits is what delivers sub-second live
  previews — paired with an LRU background reaper that cleans `.maketex/` for
  projects idle longer than N days to cap storage growth.
- Because `StateDatabase` stores paths **relative to `projectRoot`**, moving or
  restoring a project workspace (e.g. from object storage to an ephemeral compile
  node) preserves incremental cache hits automatically.

---

## Security Verification Plan (Before Launch)

Before exposing the API to untrusted users, ship an automated security test
suite verifying that malicious `.tex` fixtures are blocked cleanly:
1. **Env exfiltration**: LuaLaTeX `os.getenv("MAKETEX_TEST_SECRET")` returns `nil`.
2. **Host file read**: `\input{/etc/passwd}` and Lua `io.open("/etc/passwd")` fail.
3. **Cross-tenant escape**: `% !TEX root = ../other-tenant/main.tex` and symlinked
   `evil.tex -> /etc/passwd` are rejected before invoking TeX.
4. **Infinite loop**: `\def\x{\x}\x` terminates at `processTimeout` with zero
   surviving child/grandchild processes.
5. **Stdout flood**: infinite `\message{...}` loop is capped by
   `maxOutputBytes` without spiking server RSS.
6. **Network isolation**: Lua or `biber` URL fetch fails immediately inside the
   unshared network namespace.

---

## Phased Implementation Roadmap

1. **Phase 1 — Core seams (`maketex` repo)**: `POSIX_SPAWN_SETPGROUP`,
   `WaitLimits::maxOutputBytes`, `inheritEnv`/`baseEnv`, `ProcessRunner` hook,
   `projectRoot` containment, and optional `texSystemRoots` override.
2. **Phase 2 — Sandbox runner**: `bwrap` / `nsjail` `ProcessRunner`
   implementation + cgroups v2 + automated security test suite above.
3. **Phase 3 — Drogon service**: worker pool, per-project latest-wins
   cancellation, REST + WebSocket streaming, PDF serving with `ETag`, SyncTeX
   forward/inverse endpoints.
4. **Phase 4 — Scale & hardening**: LRU workspace cache eviction, Prometheus
   metrics (compile duration, pass count, queue depth, cache hit rate), and
   optional gVisor/Firecracker backend for public multi-tenant deployments.
