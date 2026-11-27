#!/bin/sh
# Install the latest (or requested) prebuilt maketex binary from GitHub Releases.
#
# Usage:
#   curl -fsSL https://raw.githubusercontent.com/sean-stapleton-doyle/maketex/main/scripts/install.sh | sh
#   curl -fsSL ... | sh -s -- --version v0.2.2 --prefix /usr/local/bin
#
# Environment variables:
#   MAKETEX_VERSION      Tag to install (e.g. v0.2.2); defaults to latest release
#   MAKETEX_INSTALL_DIR  Destination bin directory (default: ~/.local/bin, or /usr/local/bin if root)

set -eu

REPO="sean-stapleton-doyle/maketex"
VERSION="${MAKETEX_VERSION:-}"
INSTALL_DIR="${MAKETEX_INSTALL_DIR:-}"

usage() {
    cat <<'EOF'
Usage: install.sh [--version <tag>] [--prefix <bin-dir>]

Options:
  -v, --version <tag>   Release tag to install (e.g. v0.2.2). Default: latest.
  -p, --prefix <dir>    Directory to place the maketex binary in.
                        Default: $HOME/.local/bin (or /usr/local/bin when root).
  -h, --help            Show this help message.
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        -v|--version)
            [ $# -ge 2 ] || { echo "error: --version requires an argument" >&2; exit 1; }
            VERSION="$2"
            shift 2
            ;;
        -p|--prefix)
            [ $# -ge 2 ] || { echo "error: --prefix requires an argument" >&2; exit 1; }
            INSTALL_DIR="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown option: $1" >&2
            usage >&2
            exit 1
            ;;
    esac
done

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        echo "error: required command not found: $1" >&2
        exit 1
    fi
}

need_cmd curl
need_cmd tar

OS_RAW="$(uname -s)"
ARCH_RAW="$(uname -m)"

case "$OS_RAW" in
    Darwin) OS="macos" ;;
    Linux)  OS="linux" ;;
    *)
        echo "error: unsupported OS: $OS_RAW (prebuilt binaries are available for macOS and Linux)" >&2
        exit 1
        ;;
esac

case "$ARCH_RAW" in
    arm64|aarch64) ARCH="arm64" ;;
    x86_64|amd64)  ARCH="x86_64" ;;
    *)
        echo "error: unsupported architecture: $ARCH_RAW" >&2
        exit 1
        ;;
esac

TARGET="${OS}-${ARCH}"

if [ -z "$VERSION" ]; then
    # Follow the /releases/latest redirect without consuming GitHub API rate limit.
    LATEST_URL="$(curl --proto '=https' --tlsv1.2 -fsSL -o /dev/null -w '%{url_effective}' "https://github.com/${REPO}/releases/latest")"
    VERSION="${LATEST_URL##*/}"
    if [ -z "$VERSION" ] || [ "$VERSION" = "latest" ]; then
        echo "error: failed to resolve latest release tag from ${LATEST_URL}" >&2
        exit 1
    fi
fi

# Normalize tag with leading 'v' for release URL, without leading 'v' for archive name.
case "$VERSION" in
    v*) TAG="$VERSION"; VER_NUM="${VERSION#v}" ;;
    *)  TAG="v${VERSION}"; VER_NUM="$VERSION" ;;
esac

if [ -z "$INSTALL_DIR" ]; then
    if [ "$(id -u)" -eq 0 ]; then
        INSTALL_DIR="/usr/local/bin"
    else
        INSTALL_DIR="${HOME}/.local/bin"
    fi
fi

ARCHIVE="maketex-${VER_NUM}-${TARGET}.tar.gz"
BASE_URL="https://github.com/${REPO}/releases/download/${TAG}"

TMP_DIR="$(mktemp -d)"
cleanup() {
    rm -rf "$TMP_DIR"
}
trap cleanup EXIT INT TERM

echo "Downloading maketex ${TAG} (${TARGET})..."
curl --proto '=https' --tlsv1.2 -fsSL "${BASE_URL}/${ARCHIVE}" -o "${TMP_DIR}/${ARCHIVE}"
curl --proto '=https' --tlsv1.2 -fsSL "${BASE_URL}/SHA256SUMS.txt" -o "${TMP_DIR}/SHA256SUMS.txt"

EXPECTED_SHA="$(awk -v file="$ARCHIVE" '$2 == file { print $1; exit }' "${TMP_DIR}/SHA256SUMS.txt")"
if [ -z "$EXPECTED_SHA" ]; then
    echo "error: ${ARCHIVE} not found in SHA256SUMS.txt" >&2
    exit 1
fi

if command -v sha256sum >/dev/null 2>&1; then
    ACTUAL_SHA="$(sha256sum "${TMP_DIR}/${ARCHIVE}" | awk '{ print $1 }')"
elif command -v shasum >/dev/null 2>&1; then
    ACTUAL_SHA="$(shasum -a 256 "${TMP_DIR}/${ARCHIVE}" | awk '{ print $1 }')"
else
    echo "error: neither sha256sum nor shasum found to verify download" >&2
    exit 1
fi

if [ "$ACTUAL_SHA" != "$EXPECTED_SHA" ]; then
    echo "error: checksum mismatch for ${ARCHIVE}" >&2
    echo "  expected: ${EXPECTED_SHA}" >&2
    echo "  actual:   ${ACTUAL_SHA}" >&2
    exit 1
fi

tar -xzf "${TMP_DIR}/${ARCHIVE}" -C "$TMP_DIR"

BIN_SRC="${TMP_DIR}/maketex-${VER_NUM}-${TARGET}/bin/maketex"
if [ ! -x "$BIN_SRC" ]; then
    echo "error: expected binary missing in archive: ${BIN_SRC}" >&2
    exit 1
fi

mkdir -p "$INSTALL_DIR"
if command -v install >/dev/null 2>&1; then
    install -m 0755 "$BIN_SRC" "${INSTALL_DIR}/maketex"
else
    cp "$BIN_SRC" "${INSTALL_DIR}/maketex"
    chmod 0755 "${INSTALL_DIR}/maketex"
fi

echo "Installed $("${INSTALL_DIR}/maketex" --version) to ${INSTALL_DIR}/maketex"

case ":${PATH}:" in
    *":${INSTALL_DIR}:"*) ;;
    *)
        echo ""
        echo "Note: ${INSTALL_DIR} is not on your PATH. Add it to your shell profile:"
        echo "  export PATH=\"${INSTALL_DIR}:\$PATH\""
        ;;
esac
