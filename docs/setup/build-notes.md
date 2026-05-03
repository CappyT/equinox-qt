# T0.2 — Build notes

**Phase:** 0 (spike & validation)
**Date:** 2026-05-03
**Status:** baseline build verified on Fedora 43 from `upstream/master` (commit `f222aa79`).

---

## 1. Build system

`moonlight-qt` uses **qmake** (Qt's classic project file generator), not CMake. Three
project files matter:

| File | Role |
|------|------|
| `moonlight-qt.pro` | Top-level subdirs: `moonlight-common-c`, `qmdnsengine`, `app`, `h264bitstream` |
| `app/app.pro` | The actual Qt application |
| `moonlight-common-c/moonlight-common-c.pro` | Builds `libmoonlight-common-c.a` (static lib by default) |

The build is in-tree — qmake generates a `Makefile` in each subdir, plus separate
`Makefile.Debug` and `Makefile.Release`. `make release` builds the release flavour;
`make debug` builds the debug flavour; `make` builds both.

## 2. Submodules

The repo uses 5 git submodules, two of which have nested submodules (`enet`, `simde`
inside `moonlight-common-c`). They must be initialised before building:

```bash
git submodule update --init --recursive
```

| Submodule | Purpose |
|-----------|---------|
| `app/SDL_GameControllerDB` | Controller mapping database for SDL |
| `h264bitstream/h264bitstream` | H.264 bitstream parsing |
| `libs` | Pre-built bundles for Windows / macOS only — empty content on Linux |
| `moonlight-common-c/moonlight-common-c` | The shared C protocol library |
| `qmdnsengine/qmdnsengine` | mDNS service discovery for Sunshine host detection |
| `moonlight-common-c/moonlight-common-c/enet` | UDP reliable transport (nested) |
| `moonlight-common-c/moonlight-common-c/nanors/deps/simde` | SIMD intrinsics shim (nested) |

## 3. First build

```bash
qmake6 moonlight-qt.pro
make -j$(nproc) release
```

Expected output:

- `qmake6` reports `Checking for SL... no` and `Checking for EGL... yes`. The first is the
  Steam Link compile test failing because `STEAMLINK_SDK_PATH` is not set; this is the
  expected and correct outcome — Steam Link is the Valve hardware target, not an Equinox
  deployment target.
- `make` runs to completion. On a 16-core dev box the release build takes ~2 minutes.
- The binary lands at `app/moonlight` (~38 MB stripped).

The binary's reported version is `Moonlight 6.1.0` because `version.txt` has not been
bumped upstream past v6.1.0 (September 2024); the actual code is post-v6.1.0 master.

## 4. Verification

A non-graphical sanity check:

```bash
app/moonlight --version
```

The dev box prints two `amdgpu_query_info(ACCEL_WORKING) failed (-13)` lines before
`Moonlight 6.1.0`. These warnings are caused by the user not being in the `video` group on
the Fedora dev box (DRM primary node ACL only applies to the active graphical session
seat — see `dev-environment.md` §5). They do not block the binary from running, and they
will not appear on the Bazzite-Deck deployment target where the gaming-mode session has
full DRM access.

For an actual streaming test, see the runtime doc and T0.3 baseline measurement.

## 5. Notable enabled features in this build

`qmake6` enables the following preprocessor flags at configure time on Fedora 43:

```
HAS_X11
HAVE_FFMPEG
HAVE_LIBVA       HAVE_LIBVA_X11   HAVE_LIBVA_WAYLAND   HAVE_LIBVA_DRM
HAVE_LIBVDPAU
HAVE_DRM         HAVE_DRM_MASTER_HOOKS
HAVE_LIBPLACEBO_VULKAN
HAVE_EGL
HAS_WAYLAND
```

Notably:
- **Vulkan renderer is enabled** (`HAVE_LIBPLACEBO_VULKAN`) thanks to `libplacebo`
  ≥ 7.349.0 and `ffmpeg` ≥ 6.1.
- **VAAPI is enabled** for hardware decode on AMD/Intel.
- **Wayland support compiled in.**

These match the deployment target requirements for V1.

## 6. SDL2 vs SDL3 status in upstream

Upstream master has begun the SDL3 migration (commit `c685021f Update SDL3, dav1d, FFmpeg,
and OpenSSL`) but is not yet pure SDL3. The build links SDL2 (`PKGCONFIG += openssl sdl2
SDL2_ttf`, `LIBS += ... -lSDL2 -lSDL2_ttf`) and the application source includes a
`SDL_compat.h` that papers over the API differences. SDL3 itself is referenced from a few
files (`backend/systemproperties.cpp`, `main.cpp`).

On Fedora 43 the `sdl2-compat` package provides the SDL2 ABI on top of SDL3 internals, so
installing both `SDL3-devel` and either `SDL2-devel` or `sdl2-compat-devel` is correct;
the build picks the right `pkg-config sdl2` regardless.

When Equinox starts the SDL3 migration of its own code (`SdlInputHandler`, etc.), this
status is the upstream baseline to align with.

## 7. Build the protocol library as a shared object (Phase 1 prep)

The default `moonlight-common-c.pro` declares `CONFIG += staticlib`. For the V1 architecture
we need it as a shared object so two ELF namespaces can be opened with `dlmopen` (see
`docs/audit/session-refactor-scope.md` §3 and `docs/spikes/dlmopen-feasibility.md` §7).

In Phase 0 the spike compiled `moonlight-common-c` directly with `gcc -shared -fPIC`
out-of-tree. The Phase 1 task is to reproduce that inside the project's `.pro`:

```diff
 # moonlight-common-c/moonlight-common-c.pro
-CONFIG += staticlib
+CONFIG += dll
+QMAKE_CFLAGS += -fPIC
```

And in `app/app.pro` switch the link line on Linux from `-lmoonlight-common-c` static link
to runtime `dlmopen`:

```diff
-else:unix: LIBS += -L$$OUT_PWD/../moonlight-common-c/ -lmoonlight-common-c
+else:unix: LIBS += -ldl
```

Plus add a small wrapper class (`MlcWrapper`) that owns the `dlmopen` handle and a
function-pointer table for the `Li*` API. This is Phase 1 work, not part of T0.2.

## 8. Clean rebuild

To start over from a clean tree:

```bash
make distclean 2>/dev/null || true
rm -rf .qmake.cache .qmake.stash Makefile app/Makefile* app/release app/debug \
       moonlight-common-c/Makefile* moonlight-common-c/release moonlight-common-c/debug \
       moonlight-common-c/*.a \
       qmdnsengine/Makefile* qmdnsengine/release qmdnsengine/debug qmdnsengine/*.a \
       h264bitstream/Makefile* h264bitstream/release h264bitstream/debug h264bitstream/*.a \
       config.tests/EGL/EGL config.tests/EGL/main.o config.log
qmake6 moonlight-qt.pro
make -j$(nproc) release
```

These build outputs are not currently in `.gitignore`. They are tolerated as untracked
files but pollute `git status`. Fixing the `.gitignore` to cover them is a small follow-up
(not done here to keep upstream-tracked files unchanged).
