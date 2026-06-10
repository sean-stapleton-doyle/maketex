#!/usr/bin/env bash
# Run this script INSIDE the hyde Docker container.
# The repo root must be mounted at /mnt/host.
#
# Usage (from the container shell):
#   bash /mnt/host/scripts/hyde-inside-docker.sh

set -euo pipefail

REPO=/mnt/host
OUT="$REPO/docs-site/content/api"
INCLUDE="$REPO/include"

# Flags passed to Clang when parsing headers.
# - Drop -arch arm64 (this is a linux/x86_64 container).
# - Use c++20; the hyde Docker image ships Clang 14 which doesn't support c++26.
CLANG_FLAGS=(
    -std=c++20
    -I"$INCLUDE"
    -I"$REPO/build/_deps/blake3-src/c"
    -isystem "$REPO/build/_deps/sqlitecpp-src/include"
    -isystem "$REPO/build/_deps/sqlitecpp-src/sqlite3"
    -isystem "$REPO/build/_deps/taskflow-src"
    -Wno-unknown-attributes
    -Wno-unknown-pragmas
)

# All public headers to document.
HEADERS=(
    "$INCLUDE/StateDatabase.h"
    "$INCLUDE/FileStateTracker.h"
    "$INCLUDE/IFileHasher.h"
    "$INCLUDE/Blake3Hasher.h"
    "$INCLUDE/Process.h"
    "$INCLUDE/EngineInvocation.h"
    "$INCLUDE/IEngine.h"
    "$INCLUDE/EngineBase.h"
    "$INCLUDE/engines/PdfLatexEngine.h"
    "$INCLUDE/BuildDriver.h"
    "$INCLUDE/graph/FileNode.h"
    "$INCLUDE/graph/RuleNode.h"
    "$INCLUDE/graph/BuildGraph.h"
    "$INCLUDE/parsers/TextFileParser.h"
    "$INCLUDE/parsers/FlsParser.h"
    "$INCLUDE/parsers/LogParser.h"
    "$INCLUDE/parsers/AuxParser.h"
)

mkdir -p "$OUT"

for header in "${HEADERS[@]}"; do
    echo "→ $header"
    hyde \
        --hyde-update \
        --hyde-yaml-dir="$OUT" \
        --hyde-src-root="$REPO" \
        --use-system-clang \
        --extra-arg-before=-xc++ \
        --extra-arg=-Wno-documentation-unknown-command \
        "$header" \
        -- "${CLANG_FLAGS[@]}" || true
done

# Hyde creates a root index.md (layout: library) which turns the api/ dir into
# a leaf bundle and prevents class pages from rendering as standalone pages.
# We use _index.md (branch bundle) instead, so delete hyde's version.
rm -f "$OUT/index.md"

# Hyde mirrors the source directory tree into the output, creating a spurious
# include/ subdirectory with header copies. Remove it.
rm -rf "$OUT/include"

echo ""
echo "Done. Output written to $OUT"
