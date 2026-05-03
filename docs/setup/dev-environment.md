# T0.1 — Development environment

**Phase:** 0 (spike & validation)
**Date:** 2026-05-03
**Target host:** Fedora 43 Workstation, x86-64

---

## 1. Hardware reference

The dev box is a Framework laptop with an integrated AMD Phoenix1 GPU (RDNA3, VCN 4).
This is **not** the deployment target — Equinox V1 ships to a Bazzite-Deck mini PC with
an RX 9060 XT (RDNA4, VCN 5). The dev box is used for editing, building, unit-style
testing, and connecting to a local Sunshine instance for protocol-level testing.

| Component | Value |
|-----------|-------|
| OS | Fedora 43 Workstation |
| Kernel | 6.19.x |
| glibc | 2.42 |
| GCC | 15 |
| Qt | 6.10.3 (system packages) |
| GPU | AMD Phoenix1 (`1002:15bf`, integrated RDNA3) |
| Decoder | VCN 4 (HEVC, AV1, H.264) |

Deployment target hardware reference:

| Component | Value |
|-----------|-------|
| OS | Bazzite-Deck (Fedora Atomic, gaming-tuned) |
| GPU | AMD Radeon RX 9060 XT (RDNA4, VCN 5) |
| Display | 4K 60Hz HDMI 2.1 |
| Network | LAN 1 GbE (path to 2.5 GbE) |

The deployment target is reached via SSH and a deploy script (T0.0, separate doc, work in
progress as of writing).

## 2. Required Fedora packages

Base toolchain plus all Moonlight-Qt build dependencies on Linux:

```
qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel
qt6-qttools-devel qt6-qtwayland-devel
openssl-devel SDL2-devel SDL2_ttf-devel SDL3-devel SDL3_ttf-devel
ffmpeg-devel libva-devel libvdpau-devel
opus-devel pulseaudio-libs-devel alsa-lib-devel
libdrm-devel libplacebo-devel
gcc-c++ make
```

Notes:

- `SDL2-devel` is satisfied on Fedora 43 by `sdl2-compat-devel` (a SDL3-based shim that
  exposes the SDL2 ABI). Both can be installed; the build picks up `pkg-config sdl2`
  regardless of which one provides it.
- `SDL3-devel` is required because upstream master is mid-migration to SDL3 (commit
  `c685021f Update SDL3, dav1d, FFmpeg, and OpenSSL`). A `SDL_compat.h` header in
  `app/` papers over the API differences.
- `ffmpeg-devel` requires the `rpmfusion-free` repository.
- `libplacebo-devel` must be ≥ 7.349.0 for the Vulkan renderer; Fedora 43 ships 7.351.0.
- `ffmpeg-devel` must be ≥ 6.1 for the Vulkan renderer; Fedora 43 ships 7.1.

Versions as built and tested on 2026-05-03:

| Package | Version |
|---------|---------|
| qt6-qtbase-devel | 6.10.3 |
| ffmpeg-devel | 7.1.2 |
| libplacebo-devel | 7.351.0 |
| openssl-devel | 3.5.4 |
| SDL2_ttf-devel | 2.24.0 |
| SDL3-devel | 3.4.4 |
| sdl2-compat-devel | 2.32.64 |
| opus-devel | 1.5.2 |
| libva-devel | 2.22.0 |
| libdrm-devel | 2.4.131 |

## 3. Repositories

```
fedora                          (default)
fedora-cisco-openh264           (default)
rpmfusion-free                  (required for ffmpeg)
rpmfusion-nonfree
rpmfusion-nonfree-nvidia-driver (only if NVIDIA host)
```

Enable rpmfusion if missing:

```bash
sudo dnf install \
  https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm \
  https://mirrors.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm
```

## 4. Tooling

| Tool | Purpose | Version on dev box |
|------|---------|--------------------|
| `qmake6` | Project file → Makefile generation | from qt6-qtbase-devel |
| `make` | Build orchestration | system |
| `git` | VCS, submodule management | system |
| `gh` | GitHub CLI for repo edits and PRs | 2.92.0 |
| `pkg-config` | Build dep resolution | system |
| `gcc` | C/C++ compiler | 15 |

Two GitHub identities are configured in `gh`:
- `gtkalpa` — default active user.
- `CappyT` — owns `github.com/CappyT/equinox-qt`. Switch to it temporarily for any
  `gh repo` operation against the Equinox repo, then switch back.

```bash
gh auth switch --user CappyT
gh repo edit CappyT/equinox-qt --default-branch main
gh auth switch --user gtkalpa
```

## 5. GPU access for runtime testing on the dev box

The user `cappyt` is **not** in the `video` or `render` groups. Access to
`/dev/dri/card1` (DRM primary node, owner `root:video`, perms `crw-rw----+`) relies on the
`systemd-logind` ACL granted to the active graphical session.

Consequences:

- When Equinox is launched from the GUI session by the user, ACLs grant access and the
  AMDGPU userspace probes succeed.
- When Equinox is launched from a non-graphical context (e.g. a remote SSH shell or the
  AI assistant's bash sandbox), `amdgpu_query_info(ACCEL_WORKING)` returns `EACCES (-13)`
  and the binary falls back to whatever subset of the renderer stack works on the
  `renderD128` node (world-readable). `--version` and other non-rendering invocations
  still work.

To make GPU access work from any context (optional):

```bash
sudo usermod -aG video,render cappyt
# logout/login to apply
```

Not strictly required for the project. The Bazzite-Deck deployment target runs the binary
inside its Gaming Mode session, which has full DRM access.

## 6. Repository layout and remotes

```
origin       https://github.com/CappyT/equinox-qt.git
upstream     https://github.com/moonlight-stream/moonlight-qt.git  (read-only, fetch only)
```

Branches:

| Branch | Purpose |
|--------|---------|
| `main` | Equinox V1 work; default branch. Diverges from upstream once Phase 1 starts. |
| `upstream-mirror` | Tracks upstream `master` 1:1. Updated via `git fetch upstream && git merge upstream/master --ff-only` and pushed back. Used as the cherry-pick / merge source for upstream fixes. |
| `phase-N/<short-kebab>` | Per-task work branches under each phase. PR'd into `main` when complete. |
| `spike/<short>` | Disposable spikes. Deleted when concluded. |

The fork was bootstrapped from the upstream snapshot at commit `f222aa79` (post-v6.1.0
master, May 2026). At that point divergence was 0/0.

## 7. Reproduction steps

To reproduce this environment from a fresh Fedora 43 Workstation install:

```bash
# 1. Enable RPM Fusion (free + nonfree)
sudo dnf install -y \
  https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-$(rpm -E %fedora).noarch.rpm \
  https://mirrors.rpmfusion.org/nonfree/fedora/rpmfusion-nonfree-release-$(rpm -E %fedora).noarch.rpm

# 2. Install build deps
sudo dnf install -y \
  qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel \
  qt6-qttools-devel qt6-qtwayland-devel \
  openssl-devel SDL2-devel SDL2_ttf-devel SDL3-devel SDL3_ttf-devel \
  ffmpeg-devel libva-devel libvdpau-devel \
  opus-devel pulseaudio-libs-devel alsa-lib-devel \
  libdrm-devel libplacebo-devel \
  gcc-c++ make git gh

# 3. Clone the fork with submodules
git clone https://github.com/CappyT/equinox-qt.git ~/dev/equinox-qt
cd ~/dev/equinox-qt
git submodule update --init --recursive

# 4. Configure git remotes
git remote add upstream https://github.com/moonlight-stream/moonlight-qt.git
git fetch upstream --tags

# 5. Build (see docs/setup/build-notes.md for details)
qmake6 moonlight-qt.pro
make -j$(nproc) release
```

The resulting binary is `app/moonlight`.
