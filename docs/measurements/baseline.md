# T0.3 — Baseline single-session measurement

**Phase:** 0 (spike & validation)
**Date:** 2026-05-03
**Setup:** loopback (Sunshine + Moonlight on the same Fedora 43 dev box)
**Status:** complete

---

## 1. Setup

| Component | Value |
|-----------|-------|
| Host (Sunshine) | Fedora 43 dev box (Framework, AMD Phoenix1 iGPU) |
| Sunshine version | 2026.428.130031 (LizardByte stable copr) |
| Client (Moonlight / equinox build) | `app/moonlight` from `phase-0/audit-and-spike-d` (commit `3e372b9f`) |
| Network | loopback (127.0.0.1) |
| Display capture | KMS via `/dev/dri/card1` (Wayland session) |
| Encoder | **`libx264` software** (forced by HW probe failures on Phoenix1, see §3) |
| Decoder | software (Moonlight `videodec=0` Auto, fallback to SW) |
| Codec | H.264 |
| Resolution | 1280×720 |
| Target FPS | 60 |

This is a **protocol mechanics validation**, not a hardware performance test. The dev box
is not the V1 deployment target. Numbers below confirm that the moonlight-common-c
protocol stack, end-to-end pipeline, and equinox build can sustain a streaming session
without packet loss or jitter.

## 2. Measurements (Moonlight client overlay, `Ctrl+Alt+Shift+S`)

| Metric | Value |
|--------|-------|
| Incoming frame rate from network | 60.03 FPS |
| Decoding frame rate | 60.03 FPS |
| Rendering frame rate | 60.03 FPS |
| Host processing latency (min/max/avg) | **12.3 / 14.6 / 12.9 ms** |
| Frames dropped due to network connection | 0.00% |
| Frames dropped due to network jitter | 0.00% |
| Average network latency variance | 0.0 ms |
| Average frame queue delay | 0.02 ms |
| Average rendering time (incl. monitor v-sync) | 0.56 ms |

End-to-end estimate (host processing + network ~0 on loopback + decode + render):

```
~12.9 (encode) + ~0 (network) + decode (sub-ms SW on idle desktop) + 0.56 (render)
≈ 14-15 ms total
```

Well under the handoff's V1 target of **≤ 50 ms**.

Sunshine-side encoder stats (from `~/.config/sunshine/sunshine.log`, 49-second session at
16:49:50):

```
Streaming bitrate target:  7308 kbps
Achieved (mostly idle):    1515 kbps
I-frames: 9     avg QP 26.65   avg size 16254 B
P-frames: 2907  avg QP  4.59   avg size  3116 B   (89.7% skip)
```

Bitrate sits well below target because the captured desktop was mostly static; libx264 is
encoding 89.7% skip macroblocks. Real game content would push closer to the target.

## 3. Encoder selection notes

Sunshine probed encoders in this order before falling back to software:

| Encoder | Result |
|---------|--------|
| `nvenc` | Failed (no NVIDIA GPU) — expected |
| `h264_vulkan` | Failed: "Driver does not support required encode feedback flags (BUFFER_OFFSET and BYTES_WRITTEN)" — radv on Phoenix1 has incomplete Vulkan video encode support |
| `h264_vaapi` | Failed: "No usable encoding profile found" — **VCN 4 in Phoenix1 dropped H.264 encode hardware support** (only HEVC and AV1 encode are present) |
| `software` (libx264) | OK |

Mesa 25.3.6 + radv on Phoenix1 (AMD Radeon 780M, RDNA3, VCN 4) reports HEVC/AV1 encode
profiles via VAAPI but no H.264 encode profile. Sunshine probes H.264 first by default
when the client requests H.264; switching the client to HEVC or AV1 codec preference
*could* unlock `hevc_vaapi`, but on this dev box the corresponding HW *decode* probe in
Moonlight (Vulkan video path) also fails for HEVC, leaving software as the consistent
working configuration for loopback testing.

**This is a Phoenix1 dev-box quirk. The V1 deployment target is RX 9060 XT (RDNA4 / VCN 5)
which has full HW encode for H.264, HEVC, AV1 and no Vulkan video extension gaps in radv
at the time of writing.** Phoenix1 limitations do not affect V1 architecture or product
behaviour.

## 4. What this baseline proves

- The full Moonlight ↔ Sunshine pipeline (handshake, control stream, RTP video, RTP audio,
  Opus init, encode/decode, render) works end-to-end through the equinox build.
- The protocol sustains 60 FPS without dropped frames or jitter induced loss on loopback.
- `moonlight-common-c` does not exhibit any visible regression compared to upstream
  behaviour — expected, since the equinox tree is at exactly upstream `f222aa79` with
  zero source modifications so far.
- The render path (libplacebo → presenter) is healthy: 0.56 ms rendering time including
  v-sync indicates the Vulkan renderer integration works on the dev box's Phoenix1 GPU
  even though encode HW is missing.

## 5. What this baseline does NOT prove (deferred)

- Real LAN performance (cabled 1 GbE → 2.5 GbE).
- HW encode latency budget on the deployment target (target: ≤ 5 ms host processing).
- HEVC and AV1 codec paths.
- Long-run stability (>10 minutes sustained).
- Behaviour under packet loss / network jitter.
- Two-host concurrent streaming (T0.5).

These belong to T0.5 (dual-instance POC) on this dev box for the protocol concurrency
validation, and to Phase 1 stress testing for real-LAN performance.

## 6. T0.3 conclusion

**Pass.** Baseline pipeline works. Latency budget on the worst-case software encode path
on a constrained iGPU is already 3x under the V1 target. Confidence that the V1
hardware path on the deployment target will be comfortably under target is high.

---

*Next: T0.5 POC dual-instance (two Sunshine instances on different ports + two equinox
client instances in parallel) to validate two concurrent connections at the protocol
level.*
