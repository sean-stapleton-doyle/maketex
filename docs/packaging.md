# Packaging & Distribution

Pushing a `v*` tag triggers [`.github/workflows/release.yml`](../.github/workflows/release.yml). It builds release tarballs for `macos-arm64`, `macos-x86_64`, `linux-x86_64`, and `linux-arm64`, writes `SHA256SUMS.txt`, and renders the Homebrew and AUR packages.

## Homebrew tap setup

1. Create a public repo named `sean-stapleton-doyle/homebrew-tap`.
2. Create a Fine-Grained PAT for that repo with **Contents: Read and write**.
3. Add it to `maketex` as Actions secret `HOMEBREW_TAP_TOKEN`.
4. Push a tag (`make tag-release VERSION=v0.2.3 PUSH=1`). CI updates `Formula/maketex.rb` automatically.

Manual render for an existing release:

```sh
curl -fsSLO https://github.com/sean-stapleton-doyle/maketex/releases/download/v0.2.2/SHA256SUMS.txt
./packaging/homebrew/render-formula.sh 0.2.2 SHA256SUMS.txt > Formula/maketex.rb
```

## Arch Linux AUR (`maketex-bin`) setup

1. Create an [aur.archlinux.org](https://aur.archlinux.org/) account and upload an Ed25519 SSH key.
2. Add the private key to `maketex` as Actions secret `AUR_SSH_PRIVATE_KEY` (optional: `AUR_GIT_NAME`, `AUR_GIT_EMAIL`).
3. Push a tag. CI updates `PKGBUILD` and `.SRCINFO` on `aur.archlinux.org/maketex-bin.git`.

Manual render for an existing release:

```sh
curl -fsSLO https://github.com/sean-stapleton-doyle/maketex/releases/download/v0.2.2/SHA256SUMS.txt
./packaging/aur/render-aur.sh 0.2.2 SHA256SUMS.txt /tmp/maketex-bin
```

## Shell installer

[`scripts/install.sh`](../scripts/install.sh) downloads the matching release tarball, verifies its SHA-256 checksum, and installs `maketex` into `~/.local/bin` (or `--prefix <dir>`):

```sh
curl -fsSL https://raw.githubusercontent.com/sean-stapleton-doyle/maketex/main/scripts/install.sh | sh
```
