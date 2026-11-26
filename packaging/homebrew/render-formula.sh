#!/bin/sh
# Render Formula/maketex.rb for Homebrew from a release version and SHA256SUMS.txt.
#
# Usage:
#   packaging/homebrew/render-formula.sh <version> <path/to/SHA256SUMS.txt>

set -eu

if [ $# -ne 2 ]; then
    echo "usage: $0 <version> <SHA256SUMS.txt>" >&2
    exit 1
fi

VERSION="${1#v}"
SUMS_FILE="$2"
REPO="sean-stapleton-doyle/maketex"
BASE_URL="https://github.com/${REPO}/releases/download/v${VERSION}"

sha_for() {
    target="$1"
    file="maketex-${VERSION}-${target}.tar.gz"
    sha="$(awk -v f="$file" '$2 == f { print $1; exit }' "$SUMS_FILE")"
    if [ -z "$sha" ]; then
        echo "error: checksum for ${file} not found in ${SUMS_FILE}" >&2
        exit 1
    fi
    printf '%s' "$sha"
}

SHA_MACOS_ARM64="$(sha_for macos-arm64)"
SHA_MACOS_X86_64="$(sha_for macos-x86_64)"
SHA_LINUX_ARM64="$(sha_for linux-arm64)"
SHA_LINUX_X86_64="$(sha_for linux-x86_64)"

cat <<EOF
class Maketex < Formula
  desc "Modern, zero-config C++ replacement for latexmk"
  homepage "https://github.com/${REPO}"
  version "${VERSION}"
  license "MIT"

  on_macos do
    if Hardware::CPU.arm?
      url "${BASE_URL}/maketex-${VERSION}-macos-arm64.tar.gz"
      sha256 "${SHA_MACOS_ARM64}"
    else
      url "${BASE_URL}/maketex-${VERSION}-macos-x86_64.tar.gz"
      sha256 "${SHA_MACOS_X86_64}"
    end
  end

  on_linux do
    if Hardware::CPU.arm?
      url "${BASE_URL}/maketex-${VERSION}-linux-arm64.tar.gz"
      sha256 "${SHA_LINUX_ARM64}"
    else
      url "${BASE_URL}/maketex-${VERSION}-linux-x86_64.tar.gz"
      sha256 "${SHA_LINUX_X86_64}"
    end
  end

  def install
    bin.install "bin/maketex"
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/maketex --version")
  end
end
EOF
