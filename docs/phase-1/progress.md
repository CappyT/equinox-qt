# Phase 1 progress -- branch `phase-1/mlc-shared-build`

**Date:** 2026-05-03
**Branch HEAD:** `6f2a0c44`
**Status:** in progress (this branch covers approximately 60% of the Phase 1
audit estimate)

---

## What this branch delivers

7 commits on top of `main` (Phase 0 merge):

```
6f2a0c44  refactor(input): SdlInputHandler owns a Session* back-pointer
99b3a669  feat(session): migrate all Li* calls in session.cpp to MlcWrapper
5ab032e5  feat(session): use MlcWrapper for the three LiInitialize* calls
d0947437  build(app): wire MlcWrapper into app.pro
7f2bb288  feat(streaming): expand MlcWrapper to full 42-symbol coverage
a5079be5  feat(streaming): MlcWrapper skeleton + C++ tests
bfd4b8cf  test(mlc-wrapper): TDD scaffold for option-D dlmopen architecture
```

Architecturally:

- `MlcWrapper` C++ class (`app/streaming/MlcWrapper.{h,cpp}`) wraps every Li*
  symbol referenced by the app (42 of them). Each instance owns its own
  `dlmopen(LM_ID_NEWLM, ...)` handle to `libmoonlight-common-c.so`, giving
  ELF-namespace-isolated copies of the protocol library state.
- The TDD scaffold under `tests/mlc-wrapper/` builds the `.so` from the
  project's submodule sources (no patches to `moonlight-common-c.pro`) and
  runs 8 sub-tests (4 C against the dlmopen mechanic, 4 C++ against the
  wrapper API). All pass on Fedora 43.
- `Session` now constructs an `MlcWrapper` in `Session::initialize()` and
  routes all 13 Li* calls in `session.cpp` (init, lifecycle, port helpers,
  IDR request, HDR mode) through it.
- `SdlInputHandler` now holds an `m_OwningSession` back-pointer set at
  construction. Every Li* call across `input/abstouch.cpp`, `gamepad.cpp`,
  `input.cpp`, `keyboard.cpp`, `mouse.cpp`, `reltouch.cpp` (~60 sites) and
  every `Session::get()` / `Session::s_ActiveSession` access (the seven sites
  enumerated in `docs/audit/input-refactor-scope.md`) goes through the
  owning Session's wrapper.
- Static SDL timer callbacks that previously relied on the singleton now
  receive `this` (or a `GamepadState*` whose new `owningHandler` field
  points back to the `SdlInputHandler`) so they can reach the wrapper
  without a singleton.
- `app/app.pro` adds `streaming/MlcWrapper.{cpp,h}` to the build and links
  `-ldl` unconditionally on unix.
- `scripts/run-equinox-dev.sh` builds the `.so` if needed and launches
  `app/moonlight` with `LD_LIBRARY_PATH=tests/mlc-wrapper` so dev runs
  resolve the `.so` via dlmopen without further setup. Phase 3 packaging
  will install the `.so` properly and remove this dance.

The existing static link to `libmoonlight-common-c.a` is intentionally kept
in place: a few callers outside `Session` and the input handlers still
reach Li* directly (see below) and would break the build otherwise.

## What this branch does NOT yet deliver

Direct `Li*` callers still using the static link:

| File | Symbols | Notes |
|------|---------|-------|
| `streaming/audio/renderers/sdlaud.cpp` | `LiGetPendingAudioDuration` | Audio renderer created from `arInit` static callback; needs an owning `Session*` back-pointer (same pattern as `SdlInputHandler`). |
| `streaming/audio/renderers/slaud.cpp` | `LiGetPendingAudioDuration`, `LiGetPendingAudioFrames` | Steam Link audio target only; out of scope on Linux V1. |
| `streaming/video/ffmpeg.cpp` | `LiWakeWaitForVideoFrame`, `LiGetEstimatedRttInfo`, 2× `LiGetMicroseconds` | FFmpeg video renderer; same pattern needed. |
| `streaming/video/ffmpeg-renderers/pacer/pacer.cpp` | 2× `LiGetMicroseconds` | Frame pacer; same pattern. |
| `streaming/video/ffmpeg-renderers/drm.cpp` | `LiGetHdrMetadata` | KMSDRM renderer; same pattern. |
| `streaming/video/ffmpeg-renderers/vt_base.mm` | `LiGetHdrMetadata` | macOS VideoToolbox; out of scope on Linux V1. |
| `backend/computermanager.cpp`, `backend/nvhttp.cpp`, `gui/computermodel.cpp` | `LiTestClientConnectivity`, `LiFindExternalAddressIP4`, `LiGetLaunchUrlQueryParameters`, `LiStringifyPortFlags` | Pre-Session utility calls (lobby/discovery); they run before any Session exists so the static link is correct for them. |
| `app/streaming/video/ffmpeg-renderers/eglvid.cpp` | `Session::get()->getOverlayManager()` 3× | Overlay rendering; needs owning Session. |

Audit scope items still pending:

- **Renderer / pacer / audio refactor** (1–2 person-days). Mirror of the
  `SdlInputHandler` pattern: each `IVideoDecoder`, pacer and audio renderer
  takes an owning `Session*` (or `MlcWrapper*`) at construction. The
  `drSetup` and `arInit` static callbacks in `Session` already have access
  to `s_ActiveSession` so wiring it through is mechanical.
- **Drop `s_ActiveSession` and `s_ActiveSessionSemaphore`** (1–2 person-
  days). Only feasible after the renderer/pacer/audio refactor lands and
  every static `cl*` / `dr*` / `ar*` callback has a way to reach its owning
  Session without the singleton. Concrete options to investigate:
  per-session trampoline functions, slot-indexed callback pools, or
  packaging the callbacks inside a per-namespace shim `.so`.
- **InputRouter** (2–3 person-days). New class that owns the
  `SDL_PollEvent` loop and the `SDL_JoystickID -> Session*` binding map,
  dispatching events to the right `SdlInputHandler` instance. Per
  `docs/audit/input-refactor-scope.md` §4.2.
- **Vulkan compositor** (3–5 person-days). Two viewports, letterbox
  layout, DMA-BUF in via VAAPI. Per `docs/audit/session-refactor-scope.md`
  §4.
- **Long-running stress test against real Sunshine hosts** (1–2 person-
  days, blocked on T0.0 SSH/deploy access). Validates option D under
  realistic load, in particular the `dlclose` gracefulness on disconnect.

## Recommended next branches

| Branch | Scope | Estimate |
|--------|-------|----------|
| `phase-1/renderer-mlc-migration` | renderer/pacer/audio Li* through wrapper, drop static link | 1.5 person-days |
| `phase-1/drop-active-session` | trampoline strategy + remove singleton | 2 person-days |
| `phase-1/input-router` | InputRouter class + integration | 2–3 person-days |
| `phase-1/vulkan-compositor` | Splitscreen renderer | 3–5 person-days |

## Smoke test reproduction

```bash
make -C tests/mlc-wrapper test            # 8/8 PASS
qmake6 moonlight-qt.pro && make -j$(nproc) release
./scripts/run-equinox-dev.sh --version    # prints Moonlight 6.1.0
```

A real streaming session test (loopback Sunshine on the dev box, paired client)
exercises `Session::initialize()` and therefore the dlmopen path; this has not
been re-run since the input refactor commit and should be done before the next
branch lands.
