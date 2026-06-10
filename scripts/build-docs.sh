#!/usr/bin/env bash
# Generates the API reference with hyde (in Docker) and builds the Hugo site.
#
# Usage:
#   cd /path/to/maketex
#   bash scripts/build-docs.sh
#
# Prerequisites:
#   - Docker desktop running
#   - hugo installed on the host
#   - The project has been built at least once (build/_deps must exist)

set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"

echo "=== Pulling hyde Docker image ==="
docker pull ghcr.io/adobe/hyde:latest

echo ""
echo "=== Running hyde inside Docker ==="
docker run \
    --platform linux/x86_64 \
    --mount type=bind,source="$REPO",target=/mnt/host \
    --rm \
    ghcr.io/adobe/hyde:latest \
    bash /mnt/host/scripts/hyde-inside-docker.sh

echo ""
echo "=== Building Hugo site ==="
cd "$REPO/docs-site"
hugo --minify

echo ""
echo "Done. Site built at $REPO/docs-site/public/"
echo "To preview: cd docs-site && hugo server"
