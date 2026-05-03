# T0.4 — `Session` audit and multi-instance refactor scope

**Phase:** 0 (spike & validation)
**Date:** 2026-05-03
**Status:** complete

---

## Executive summary

The handoff plan (§4.2 of the design doc) underestimates the scope of the refactor: **the
multi-instance problem is not in `Session`, it is in `moonlight-common-c`**. The library
that implements the Moonlight client protocol is architecturally
**single-connection-per-process**, with ~29 `extern` globals plus ~314 file-static globals,
~12 threads spawned per connection that read and write that state, and an API that does
not allow user context to be passed through callbacks.

`Session` (`app/streaming/session.h`) inherits this limitation: it is already an implicit
singleton, holding `s_ActiveSession` so the C-style protocol callbacks have a way to find
the instance, and `s_ActiveSessionSemaphore(1)` explicitly serialises sessions.

**Durable project decision** (recorded in persistent memory): we do **not** patch the source
of `moonlight-common-c`. The strategies allowed for Equinox V1 are multi-process (A/B) or
per-namespace isolation via `dlmopen` (D). The D spike is documented in
`docs/spikes/dlmopen-feasibility.md` and passed both the basic feasibility test and the
extended integration test against the real `.so`.

---

## 1. Quantitative findings

### 1.1 Global state in `moonlight-common-c`

| Category | Count | Location |
|----------|-------|----------|
| `extern` globals | 29 | `src/Limelight-internal.h` |
| File-static globals | ~314 | 18 `.c` files |
| Top offender | 78 statics | `src/ControlStream.c` |
| Second | 40 statics | `src/VideoDepacketizer.c` |
| Third | 32 statics | `src/RtspConnection.c` |

The 29 extern globals cover: remote/local addresses, stream config, callback sets
(connection/video/audio), negotiated video format, interruption flag, opus configs
(high/normal), RTSP/control/audio/video port numbers, ping payloads, encryption flags,
Sunshine feature flags. Full list in **Appendix A**.

### 1.2 Threading model per connection

`moonlight-common-c` spawns at least **12 threads** during a live connection:

| File | Thread name | Role |
|------|-------------|------|
| `Connection.c` | `AsyncTerm` | Async termination callback |
| `AudioStream.c` | `AudioPing` | UDP heartbeat |
| `AudioStream.c` | `AudioRecv` | Packet receive |
| `AudioStream.c` | `AudioDec` | Decoder loop |
| `VideoStream.c` | `VideoPing` | UDP heartbeat |
| `VideoStream.c` | `VideoRecv` | Packet receive |
| `VideoStream.c` | `VideoDec` | Decoder loop |
| `InputStream.c` | `InputSend` | Controller event upload |
| `ControlStream.c` | `ControlRecv` | Control channel receive |
| `ControlStream.c` | `LossStats` | Reporting |
| `ControlStream.c` | `ReqIdrFrame` | IDR request |
| `ControlStream.c` | `CtrlAsyncCb` | Async callback dispatch |
| `ControlStream.c` | `InvRefFrames` | Reference frame invalidation (conditional) |

Every one of these threads reads or writes its file-local globals. A second concurrent
in-process connection would touch the same globals → guaranteed race.

### 1.3 Relevant `moonlight-common-c` API

```c
// LiStartConnection accepts a void* renderContext, but it is process-wide:
int LiStartConnection(PSERVER_INFORMATION serverInfo,
                      PSTREAM_CONFIGURATION streamConfig,
                      PCONNECTION_LISTENER_CALLBACKS clCallbacks,
                      PDECODER_RENDERER_CALLBACKS drCallbacks,
                      PAUDIO_RENDERER_CALLBACKS arCallbacks,
                      void* renderContext, int drFlags, ...);

// Limelight.h:558-559, explicit comment:
//   "it is not safe to start another connection before the first
//    LiStartConnection() call returns"
```

The 13 callbacks in `CONNECTION_LISTENER_CALLBACKS` are bare C function pointers **with no
user-data parameter**. Same for `DECODER_RENDERER_CALLBACKS` and `AUDIO_RENDERER_CALLBACKS`.
There is no way to register two distinct callback sets for two different `Session`
instances, not even in theory.

### 1.4 `Session` on the app side

`app/streaming/session.h:285-286`:

```cpp
static Session* s_ActiveSession;
static QSemaphore s_ActiveSessionSemaphore;
```

And `app/streaming/session.cpp:50-67`:

```cpp
CONNECTION_LISTENER_CALLBACKS Session::k_ConnCallbacks = {
    Session::clStageStarting, ...
    Session::clRumble, ...
};

Session* Session::s_ActiveSession;
QSemaphore Session::s_ActiveSessionSemaphore(1);
```

Every `cl*`, `ar*`, `dr*` thunk (15+ static functions in `session.cpp`) reaches through
`s_ActiveSession->m_FieldX` to emit Qt signals or update state.
`s_ActiveSessionSemaphore(1)` explicitly prevents two `Session` instances from coexisting
in the same process — that is a hard serialisation, not an incidental one.

The **per-instance** members (`m_Preferences`, `m_StreamConfig`, `m_VideoCallbacks`,
`m_VideoDecoder`, `m_DecoderLock`, `m_InputHandler`, `m_OpusDecoder`, `m_AudioRenderer`,
`m_OverlayManager`, etc.) are already cleanly scoped per instance. Refactoring `Session`
**as a class** is light. The hard part is what lives **underneath**.

---

## 2. Strategies

Option **C** (in-process refactor of `moonlight-common-c`) is excluded by project
decision. That leaves A, B, D:

| | **A** Two `equinox` processes | **B** Decoder child + parent compositor | **D** `dlmopen` ELF namespace |
|---|---|---|---|
| `moonlight-common-c` source changes | none | none | build as `.so` (zero source change) |
| Compositing | external (gamescope/wm) or a third process | native in parent | native, single-process |
| Zero-copy DMA-BUF | hard (cross-process) | medium (DMA-BUF over Unix socket + `SCM_RIGHTS`) | native |
| Estimated added latency | +1 frame minimum (external compositing) | ~0 frames (parent renders) | ~0 frames |
| UI cohesion (binding mode, hot-swap, settings) | hard, requires IPC | medium | simple, single event loop |
| Audio (PipeWire mix) | free | free | free |
| Phase 1 effort estimate | 2-3 weeks (2-proc lifecycle + IPC + compositor) | 3-4 weeks (DMA-BUF IPC + child↔parent protocol) | 1.5-2 weeks (build .so + dlmopen wrapper + threading/openssl validation) |
| Known risks | Awkward dual UI, lifecycle sync | DMA-BUF cross-process is fragile on non-mainline drivers | OpenSSL/pthread state under separate namespaces |

### Quick scoring

- **A** — "safe but ugly". Guaranteed to work (it is the T0.5 POC path), but compromises
  binding mode UX and compositing. Solid fallback.
- **B** — same robustness as A, plus better zero-copy, but the child↔parent protocol for
  passing DMA-BUF handles is non-trivial and has little prior art in Moonlight-like
  scenarios.
- **D** — "clever middle ground". Single process → simple UX, native zero-copy, smallest
  refactor. The caveats (OpenSSL/pthread/signals under split namespaces) are testable in 2-3
  days of validation.

---

## 3. Recommendation

**V1 path: D (`dlmopen` + ELF namespace isolation).**

The extended D validation was carried out in the same session as this audit (see
`docs/spikes/dlmopen-feasibility.md` §7). Findings:

- 22 extern globals tested with the real `.so` → all isolated, per-namespace writes are
  independent.
- libssl + libcrypto + libc are each loaded twice under `LM_ID_NEWLM` → OpenSSL and pthread
  state are namespace-local.
- `PltCreateThread` invoked from each namespace → threads spawn correctly, callbacks
  resolve to the calling namespace.
- Signal handler caveat: the only handler installed by the library is `SIGPIPE → SIG_IGN`,
  which is idempotent — no conflict between namespaces.
- Memory: +~18 MB mapped, +~6 MB RSS for the two namespace loads. Negligible.

**B** remains as a theoretical fallback if Phase 1 surfaces runtime issues not caught by
the spike (e.g. pathological behaviour on disconnect, or stress test with a real host).
Probably will not be needed.

**A** stays as the bottom-fallback POC throwaway — it is the T0.5 setup from the handoff,
guaranteed to work but compromises UX (dual binding mode is awkward).

**C** is excluded by durable project decision (no source modifications to
`moonlight-common-c`).

**Estimated Phase 1 effort (post-validation):**

| Component | Person-days |
|-----------|-------------|
| C++ wrapper around `dlmopen` + `dlsym` table for the full `Li*` API | 2-3 |
| `Session` refactor to call the wrapper instead of direct `Li*` | 2-3 |
| Split `s_ActiveSession`/`s_ActiveSessionSemaphore` (one per `Session`) | 1-2 |
| `SdlInputHandler` refactor away from singleton assumptions | 1-2 |
| Basic Vulkan compositor (two viewports, letterbox) | 3-5 |
| Integration testing (two dummy sessions without real hosts) | 2-3 |
| **Phase 1 total** | **11-18 person-days** (~2-3.5 weeks part-time) |

Consistent with the handoff's Phase 1 budget (2 weeks).

---

## 4. Open questions

1. **`SdlInputHandler` global state.** Audit not yet done on `app/streaming/input/`.
   Estimate: similar profile to `Session`, light refactor. To be quantified at Phase 1
   kickoff.
2. ~~OpenSSL via dlmopen.~~ Resolved by spike: libssl/libcrypto are loaded twice, state is
   namespace-local.
3. ~~`DL_NNS` limit.~~ Confirmed: glibc 2.42 default = 16, we use 2.
4. **"Host disconnect mid-stream" case.** In model D, `dlclose` on the orphan namespace at
   session end must release the spawned threads cleanly. To be validated in Phase 1 with a
   real host.
5. **Building `moonlight-common-c` as `.so` inside the project.** The spike uses an
   out-of-tree build (direct `gcc`). In Phase 1 we need to reproduce it inside
   `moonlight-common-c.pro` (toggle `CONFIG: staticlib → dll` plus `-fPIC`). Decide whether
   to keep both targets (static for any backwards-compat path + shared for Equinox) or only
   shared.

---

## Appendix A — Full extern globals list

From `moonlight-common-c/src/Limelight-internal.h` (29 entries):

```
char*                              RemoteAddrString
struct sockaddr_storage            RemoteAddr
struct sockaddr_storage            LocalAddr
SOCKADDR_LEN                       AddrLen
int[4]                             AppVersionQuad
STREAM_CONFIGURATION               StreamConfig
CONNECTION_LISTENER_CALLBACKS      ListenerCallbacks
DECODER_RENDERER_CALLBACKS         VideoCallbacks
AUDIO_RENDERER_CALLBACKS           AudioCallbacks
int                                NegotiatedVideoFormat
volatile bool                      ConnectionInterrupted
bool                               HighQualitySurroundSupported
bool                               HighQualitySurroundEnabled
OPUS_MULTISTREAM_CONFIGURATION     NormalQualityOpusConfig
OPUS_MULTISTREAM_CONFIGURATION     HighQualityOpusConfig
int                                AudioPacketDuration
bool                               AudioEncryptionEnabled
bool                               ReferenceFrameInvalidationSupported
uint16_t                           RtspPortNumber
uint16_t                           ControlPortNumber
uint16_t                           AudioPortNumber
uint16_t                           VideoPortNumber
SS_PING                            AudioPingPayload
SS_PING                            VideoPingPayload
uint32_t                           ControlConnectData
uint32_t                           SunshineFeatureFlags
uint32_t                           EncryptionFeaturesSupported
uint32_t                           EncryptionFeaturesRequested
uint32_t                           EncryptionFeaturesEnabled
```

---

*End of T0.4. For the `dlmopen` spike see `docs/spikes/dlmopen-feasibility.md`.*
