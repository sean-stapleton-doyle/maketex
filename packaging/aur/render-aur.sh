#!/bin/sh
# Render AUR PKGBUILD and .SRCINFO for maketex-bin from a release version and SHA256SUMS.txt.
#
# Usage:
#   packaging/aur/render-aur.sh <version> <path/to/SHA256SUMS.txt> <out-dir>

set -eu

if [ $# -ne 3 ]; then
    echo "usage: $0 <version> <SHA256SUMS.txt> <out-dir>" >&2
    exit 1
fi

VERSION="${1#v}"
SUMS_FILE="$2"
OUT_DIR="$3"
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

SHA_X86_64="$(sha_for linux-x86_64)"
SHA_AARCH64="$(sha_for linux-arm64)"

mkdir -p "$OUT_DIR"

cat > "${OUT_DIR}/PKGBUILD" <<EOF
# Maintainer: Sean Stapleton Doyle <seanstapletondoyle@gmail.com>
pkgname=maketex-bin
pkgver=${VERSION}
pkgrel=1
pkgdesc="Modern, zero-config C++ replacement for latexmk"
arch=('x86_64' 'aarch64')
url="https://github.com/${REPO}"
license=('MIT')
depends=('glibc')
optdepends=(
    'texlive-basic: pdflatex and core TeX Live tools'
    'texlive-xetex: xelatex engine support'
    'texlive-luatex: lualatex engine support'
    'biber: biblatex bibliography processor'
)
provides=('maketex')
conflicts=('maketex')
source_x86_64=("${BASE_URL}/maketex-\${pkgver}-linux-x86_64.tar.gz")
source_aarch64=("${BASE_URL}/maketex-\${pkgver}-linux-arm64.tar.gz")
sha256sums_x86_64=('${SHA_X86_64}')
sha256sums_aarch64=('${SHA_AARCH64}')

package() {
    local _arch="x86_64"
    if [ "\$CARCH" = "aarch64" ]; then
        _arch="arm64"
    fi
    local _dir="\${srcdir}/maketex-\${pkgver}-linux-\${_arch}"

    install -Dm755 "\${_dir}/bin/maketex" "\${pkgdir}/usr/bin/maketex"
    install -Dm644 "\${_dir}/LICENSE" "\${pkgdir}/usr/share/licenses/\${pkgname}/LICENSE"
    install -Dm644 "\${_dir}/README.md" "\${pkgdir}/usr/share/doc/\${pkgname}/README.md"
}
EOF

cat > "${OUT_DIR}/.SRCINFO" <<EOF
pkgbase = maketex-bin
	pkgdesc = Modern, zero-config C++ replacement for latexmk
	pkgver = ${VERSION}
	pkgrel = 1
	url = https://github.com/${REPO}
	arch = x86_64
	arch = aarch64
	license = MIT
	depends = glibc
	optdepends = texlive-basic: pdflatex and core TeX Live tools
	optdepends = texlive-xetex: xelatex engine support
	optdepends = texlive-luatex: lualatex engine support
	optdepends = biber: biblatex bibliography processor
	provides = maketex
	conflicts = maketex
	source_x86_64 = ${BASE_URL}/maketex-${VERSION}-linux-x86_64.tar.gz
	sha256sums_x86_64 = ${SHA_X86_64}
	source_aarch64 = ${BASE_URL}/maketex-${VERSION}-linux-arm64.tar.gz
	sha256sums_aarch64 = ${SHA_AARCH64}

pkgname = maketex-bin
EOF
