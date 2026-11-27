"""
LLDB Python callbacks for maketex debugging.

Load with:
    (lldb) command script import debug/maketex_callbacks.py

Or it is loaded automatically by debug/maketex.lldb.
"""

import lldb


def __lldb_init_module(debugger, internal_dict):
    print("[maketex] debug callbacks loaded")


# ── helpers ───────────────────────────────────────────────────────────────────

def _read_string(val):
    """Return the std::string content from an SBValue, or a fallback."""
    # clangd / libc++ layout: __r_.__value_ or __s.__data_
    summary = val.GetSummary()
    if summary:
        return summary.strip('"')
    return str(val)


def _hash_hex(hash_val):
    """
    Format a maketex::Hash (std::array<uint8_t, 32>) as a short hex prefix.
    Prints the first 8 bytes (16 hex chars) — enough to distinguish hashes.
    """
    try:
        bytes_out = []
        data = hash_val.GetChildMemberWithName("__elems_")
        if not data or not data.IsValid():
            # Some libc++ layouts use a different name
            data = hash_val
        for i in range(min(8, data.GetNumChildren())):
            child = data.GetChildAtIndex(i)
            bytes_out.append(f"{child.GetValueAsUnsigned():02x}")
        return "".join(bytes_out) + "…"
    except Exception as e:
        return f"<hash error: {e}>"


def _snapshot_summary(snap_val):
    """
    Summarise an std::unordered_map<std::string, Hash> (the snapshot type).
    Prints path basename → first 8 hash bytes for each entry.
    """
    lines = []
    n = snap_val.GetNumChildren()
    for i in range(n):
        bucket = snap_val.GetChildAtIndex(i)
        if not bucket.IsValid():
            continue
        # Each child of unordered_map is a [key, value] pair
        key_val = bucket.GetChildMemberWithName("first")
        val_val = bucket.GetChildMemberWithName("second")
        if not key_val.IsValid() or not val_val.IsValid():
            continue
        path = _read_string(key_val)
        # Show only the filename, not the full path
        basename = path.split("/")[-1] if "/" in path else path
        hex_prefix = _hash_hex(val_val)
        lines.append(f"    {basename}: {hex_prefix}")
    return "\n".join(lines) if lines else "    (empty)"


# ── breakpoint callbacks ──────────────────────────────────────────────────────

def on_hash_compare(frame, bp_loc, dict):
    """
    Hit on BuildDriver.cpp:157 — bool filesChanged = (afterSnapshot != beforeSnapshot)

    Prints both snapshots so you can see exactly which files changed and
    whether their hashes differ between passes.
    """
    before = frame.FindVariable("beforeSnapshot")
    after  = frame.FindVariable("afterSnapshot")
    pass_  = frame.FindVariable("pass")

    pass_num = pass_.GetValueAsUnsigned(0) + 1 if pass_.IsValid() else "?"

    print(f"\n[maketex] ── Hash comparison (pass {pass_num}) ──────────────────")
    print(f"  beforeSnapshot ({before.GetNumChildren()} entries):")
    print(_snapshot_summary(before))
    print(f"  afterSnapshot  ({after.GetNumChildren()} entries):")
    print(_snapshot_summary(after))

    return True  # stop here so you can inspect further


def on_graph_new_file(frame, bp_loc, dict):
    """
    Hit on BuildGraph.cpp:17 — new FileNode being allocated.

    Only fires for the allocation branch (node creation), not cache hits,
    so every print represents a genuinely new node entering the graph.
    """
    path_val = frame.FindVariable("path")
    role_val = frame.FindVariable("role")
    path_str = _read_string(path_val)
    basename = path_str.split("/")[-1] if "/" in path_str else path_str
    role_int = role_val.GetValueAsUnsigned(0)
    role_name = {0: "Source", 1: "Generated", 2: "Output"}.get(role_int, str(role_int))

    # Skip noisy TeX system files
    skip_prefixes = ("/usr/local/texlive", "/Library/TeX")
    if not any(path_str.startswith(p) for p in skip_prefixes):
        print(f"[maketex] + FileNode  {role_name:<12} {basename}")
    return False  # don't stop — too noisy to pause on every file


def on_graph_new_rule(frame, bp_loc, dict):
    """
    Hit on BuildGraph.cpp:29 — new RuleNode being allocated.
    """
    name_val = frame.FindVariable("name")
    name_str = _read_string(name_val)
    print(f"[maketex] + RuleNode  '{name_str}'")
    return True  # stop so you can inspect the graph state


def on_graph_update(frame, bp_loc, dict):
    """
    Hit on BuildDriver.cpp:68 — updateGraphFromFls entry.

    Prints which .fls file is being parsed.
    """
    fls_val = frame.FindVariable("fls")
    fls_str = _read_string(fls_val) if fls_val.IsValid() else "?"
    basename = fls_str.split("/")[-1] if "/" in fls_str else fls_str
    print(f"\n[maketex] ── updateGraphFromFls({basename}) ────────────────────")
    return True  # stop so you can step through the parse


def on_engine_result(frame, bp_loc, dict):
    """
    Hit on BuildDriver.cpp:140 (++result.passes) — immediately after future.get()
    returns, so engineResult is fully populated.

    Prints exit code, stdout length, and whether the engine succeeded.
    """
    res   = frame.FindVariable("engineResult")
    pass_ = frame.FindVariable("pass")
    result = frame.FindVariable("result")

    exit_code = res.GetChildMemberWithName("exitCode").GetValueAsSignedIfAvailable()[0]
    stdout_val = res.GetChildMemberWithName("stdoutOutput")
    stderr_val = res.GetChildMemberWithName("stderrOutput")

    pass_num = pass_.GetValueAsUnsigned(0) + 1 if pass_.IsValid() else "?"
    stdout_len = stdout_val.GetByteSize() if stdout_val.IsValid() else 0
    success = "✓" if exit_code == 0 else "✗"

    print(f"\n[maketex] ── Engine result (pass {pass_num}) {success} ──────────────────")
    print(f"  exitCode:   {exit_code}")
    print(f"  stdout:     {stdout_val.GetSummary() or '(empty)'}")
    print(f"  stderr:     {stderr_val.GetSummary() or '(empty)'}")

    return True  # stop so you can inspect engineResult
