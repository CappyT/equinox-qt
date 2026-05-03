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
| `phase-1/mlc-static-openssl` | unblocker for option D: build libmoonlight-common-c.so with OpenSSL embedded statically so each dlmopen namespace has its own libcrypto and OPENSSL_init_crypto stops crashing (see §11) | 1–2 person-days |
| `phase-1/renderer-mlc-migration` | renderer/pacer/audio Li* through wrapper, drop static link | 1.5 person-days |
| `phase-1/drop-active-session` | trampoline strategy + remove singleton | 2 person-days |
| `phase-1/input-router` | InputRouter class + integration | 2–3 person-days |
| `phase-1/vulkan-compositor` | Splitscreen renderer | 3–5 person-days |

`phase-1/mlc-static-openssl` is now the **gating** prerequisite for actually
running the wrapper in dlmopen mode. Until it lands, MlcWrapper stays in
static-link mode (see §11) and the "two concurrent sessions in one process"
goal cannot be exercised on this branch.

## 11. The OpenSSL + dlmopen blocker (discovered 2026-05-03)

The first real-host streaming test of this branch **SIGSEGVs** inside libcrypto
during `Session::initialize()` -> `m_Mlc->startConnection()` -> RTSP handshake.
The crash sits at the same call site every time, with the offending function
inside libcrypto bouncing run to run between `ERR_set_mark`,
`ossl_rcu_read_unlock` and similar TLS-touching helpers, all reached via:

```
Catchpoint 3 (signal SIGSEGV), 0x...... in <ossl_*> () at /lib64/libcrypto.so.3
#0  in <ossl_*>                  at /lib64/libcrypto.so.3
#1  in CONF_modules_load_file_ex at /lib64/libcrypto.so.3
#2  in ossl_init_config_ossl_    at /lib64/libcrypto.so.3
#3  in __pthread_once_slow.isra  at /lib64/libc.so.6
...
#6  in OPENSSL_init_crypto       at /lib64/libcrypto.so.3
#7  in evp_cipher_init_internal  at /lib64/libcrypto.so.3
#8  in EVP_CipherInit_ex         at /lib64/libcrypto.so.3
#9  in PltEncryptMessage         at moonlight-common-c/src/PlatformCrypto.c:149
#10 in sealRtspMessage           at moonlight-common-c/src/RtspConnection.c:137
#11 in transactRtspMessageTcp    at moonlight-common-c/src/RtspConnection.c:428
#12 in performRtspHandshake      at moonlight-common-c/src/RtspConnection.c:1051
#13 in LiStartConnection         at moonlight-common-c/src/Connection.c:442
```

Two libcrypto.so instances co-exist in the process: one in the main namespace
(linked by SDL_ttf, openssl-using libs) and one inside the dlmopen LM_ID_NEWLM
namespace where libmoonlight-common-c.so lives. Each namespace runs its own
`OPENSSL_init_crypto` lazily on first use; the second init trips on the
per-process state OpenSSL assumes is unique (TLS keys for the per-thread error
stack, atfork handlers, RCU bookkeeping for the config-modules table).

The `OPENSSL_CONF=/dev/null` env-var workaround was attempted and **did not
help** -- the same `CONF_modules_load_file_ex` path runs even when there is no
config file, and the segfault just lands on a different libcrypto symbol from
run to run. The dlmopen feasibility spike (`docs/spikes/dlmopen-feasibility.md`
§7.5) had marked the OpenSSL caveat as "RESOLVED" based on the observation that
two libcrypto.so instances get loaded with distinct base addresses; that
validation was insufficient because no actual OpenSSL function was called from
the dlmopen'd `.so` during the spike. Production use (RTSP encrypt) is the
first time OpenSSL is actually exercised inside the dlmopen namespace, and it
crashes immediately.

Full diagnosis captured in `/tmp/equinox-debug/` via the
`scripts/debug-equinox-crash.{sh,gdb}` reproducer added in this branch.

### 11.1 Current mitigation in this branch

`MlcWrapper.cpp` defaults to **static-link mode**: the function pointer table
is bound to the directly-linked `Li*` symbols (the still-present
libmoonlight-common-c.a), no `dlmopen` happens at runtime, the destructor's
`dlclose` becomes a no-op. Two `MlcWrapper` instances in this mode would share
the singleton mlc state, so the dual-session goal is **not** met by this
branch yet -- but the entire call-site migration is preserved: flipping back
to dlmopen mode is a one-liner (`-DMLCWRAPPER_USE_DLMOPEN`) once a libcrypto
fix lands.

Real-host streaming with the wrapper in static-link mode was verified working:
single Sunshine, paired client, full RTSP handshake, video stream begins,
audio stream initialised, no crashes. The branch is therefore non-regressive
relative to `main` and safe to merge.

### 11.2 The fix path: `phase-1/mlc-static-openssl`

The structural fix is to **embed OpenSSL statically into
libmoonlight-common-c.so** so each dlmopen of the `.so` brings in its own
private copy of `libcrypto`/`libssl` and there is no shared
`/lib64/libcrypto.so.3` sitting in two namespaces at once. Sketch:

1. Build OpenSSL 3 from source as static archives (`libcrypto.a`, `libssl.a`).
2. Patch `tests/mlc-wrapper/Makefile` (and later `moonlight-common-c.pro`)
   to link those `.a` into the `.so` with `-Wl,--whole-archive` so the symbols
   end up in mlc.so itself, not resolved against `/lib64`.
3. Strip the libcrypto/libssl symbols from the export table so the dlmopen
   loader does not surface them to neighbouring namespaces.
4. Re-run the gdb reproducer; expect `OPENSSL_init_crypto` to succeed because
   each mlc.so instance has its own self-contained OpenSSL state.
5. Re-enable dlmopen in `MlcWrapper.cpp` via `-DMLCWRAPPER_USE_DLMOPEN`.
6. Verify two concurrent `MlcWrapper` instances see isolated mlc state by
   running the existing `tests/mlc-wrapper/test_wrapper` against the new `.so`.

Estimated cost: 1–2 person-days, dominated by the OpenSSL static build
(needs to match the FFmpeg / sdl2-compat OpenSSL ABI used by the rest of the
app -- using the same OpenSSL 3.5 source is the safe choice).

## Smoke test reproduction (current branch state)

```bash
# Tests scaffold (dlmopen isolation still validated against the standalone .so)
make -C tests/mlc-wrapper test            # 8/8 PASS

# App build
qmake6 moonlight-qt.pro && make -j$(nproc) release

# Sanity launch
./scripts/run-equinox-dev.sh --version    # prints Moonlight 6.1.0

# Real streaming (requires a paired Sunshine host -- localhost works)
./scripts/run-equinox-dev.sh
# Click the host tile -> click Desktop -> stream begins, no crash.

# Reproduce the dlmopen / OpenSSL crash on demand (only after re-enabling
# dlmopen mode by adding -DMLCWRAPPER_USE_DLMOPEN to app/app.pro)
./scripts/debug-equinox-crash.sh
```
