# Packaging & Distribution

Release binaries for `macos-arm64`, `macos-x86_64`, `linux-x86_64`, and `linux-arm64` are built automatically by [`.github/workflows/release.yml`](../.github/workflows/release.yml) whenever a `v*` tag is pushed.

The same workflow also renders and (when configured) publishes the **Homebrew tap** formula and **AUR (`maketex-bin`)** package.

## 1. One-time Homebrew tap setup

1. Create a public GitHub repository named **`homebrew-tap`** under your account (`sean-stapleton-doyle/homebrew-tap`) with a `Formula/` directory (or empty `README.md`).
2. Create a Fine-Grained Personal Access Token scoped **only** to `sean-stapleton-doyle/homebrew-tap` with **Contents: Read and write**.
3. Add it as a repository secret in `maketex`:
   - **Settings → Secrets and variables → Actions → New repository secret**
   - Name: `HOMEBREW_TAP_TOKEN`
4. Push a release tag (`make tag-release VERSION=v0.2.3 PUSH=1`). The `publish-homebrew` job will generate `Formula/maketex.rb` from `SHA256SUMS.txt` and push it to `homebrew-tap`.

Users can then install and upgrade with:

```sh
brew install sean-stapleton-doyle/tap/maketex
```

### Seeding `homebrew-tap` manually for an existing release

```sh
curl -fsSLO https://github.com/sean-stapleton-doyle/maketex/releases/download/v0.2.2/SHA256SUMS.txt
./packaging/homebrew/render-formula.sh 0.2.2 SHA256SUMS.txt > Formula/maketex.rb
```

## 2. One-time Arch Linux AUR (`maketex-bin`) setup

1. Register an account on [aur.archlinux.org](https://aur.archlinux.org/) and add a dedicated Ed25519 deploy key (`ssh-keygen -t ed25519 -C "maketex-aur-ci" -f ~/.ssh/aur_maketex -N ""`) to your AUR profile.
2. Add the private key as a repository secret in `maketex`:
   - Name: `AUR_SSH_PRIVATE_KEY` (full contents of `~/.ssh/aur_maketex`)
   - Optional: `AUR_GIT_NAME` and `AUR_GIT_EMAIL` (defaults to the repo maintainer)
3. Push a release tag. The `publish-aur` job will render `PKGBUILD` and `.SRCINFO`, clone `ssh://aur@aur.archlinux.org/maketex-bin.git`, and push the updated package.

Users can then install with:

```sh
yay -S maketex-bin   # or: paru -S maketex-bin
```

### Seeding AUR manually for an existing release

```sh
curl -fsSLO https://github.com/sean-stapleton-doyle/maketex/releases/download/v0.2.2/SHA256SUMS.txt
./packaging/aur/render-aur.sh 0.2.2 SHA256SUMS.txt /tmp/maketex-bin
```

## 3. `curl | sh` installer

[`scripts/install.sh`](../scripts/install.sh) installs the latest (or `--version <tag>`) prebuilt archive directly from GitHub Releases, verifies its SHA-256 checksum against `SHA256SUMS.txt`, and places `maketex` in `~/.local/bin` (or `/usr/local/bin` when run as root, or `--prefix <dir>`):

```sh
curl -fsSL https://raw.githubusercontent.com/sean-stapleton-doyle/maketex/main/scripts/install.sh | sh
```
