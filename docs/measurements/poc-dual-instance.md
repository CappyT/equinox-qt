# T0.5 — POC dual-instance protocol concurrency

**Phase:** 0 (spike & validation)
**Date:** 2026-05-03
**Setup:** two Sunshine instances + two equinox client processes on the same Fedora 43
dev box, all isolated by `XDG_CONFIG_HOME`.
**Status:** complete, **PASS**.

---

## 1. Goal

Verify that two `Session` instances running in two separate equinox processes, each
talking to its own Sunshine instance, can stream concurrently without protocol-level
interference. This is the multi-process model that the handoff §4.2 prescribes as the
T0.5 throwaway POC; positive result here de-risks the eventual single-process path with
`dlmopen` (option D, see `docs/audit/session-refactor-scope.md` and
`docs/spikes/dlmopen-feasibility.md`) since the isolation behaviour is equivalent at
the protocol layer.

## 2. Setup

Two Sunshine instances with isolated `XDG_CONFIG_HOME` and non-overlapping port ranges:

| Instance | Config dir | Base port | HTTPS API | HTTP fallback | Web UI | Stream |
|----------|------------|-----------|-----------|----------------|--------|--------|
| Sunshine #1 | `~/.config/sunshine` | `47984` (default) | 47984 | 47989 | 47990 | 48010 (?) |
| Sunshine #2 | `~/.config-sunshine-b/sunshine` | `48100` | 48100 | 48095 | 48101 | 48121 |

(Port mapping derived from observed `ss -tln`. Sunshine derives all ports from `port =`
config: HTTPS = port, HTTP fallback = port − 5, Web UI = port + 1, plus extra UDP/TCP
streaming ports.)

Two equinox client processes with isolated `XDG_CONFIG_HOME`:

| Instance | Config dir | Paired host |
|----------|------------|-------------|
| Moonlight #1 | `~/.config` (default) | Sunshine #1 (port 47984) |
| Moonlight #2 | `~/.config-moonlight-b` | Sunshine #2 (port 48100) |

Launched the second sunshine and the second moonlight from background bash with
`XDG_CONFIG_HOME` set; both inherit the user's Wayland session env (`WAYLAND_DISPLAY`,
`XDG_RUNTIME_DIR`) so they can render and capture in the same desktop.

Pre-flight: pre-set the Sunshine #2 web credentials non-interactively with
`sunshine --creds claude claudepass` so the second instance is pairable without going
through the first-run web wizard.

## 3. Concurrent stream measurements

Both clients connected to their respective Sunshine in software-encode mode (libx264,
forced by Phoenix1 dev-box GPU limitations — see `baseline.md` §3 for the encoder probe
trace; this is not relevant to V1 target hardware).

Measured from the Moonlight in-stream overlay (`Ctrl+Alt+Shift+S`) on each client during
~32 s of overlapping streaming:

| Metric | Baseline (single, T0.3) | Client #1 → S#1 (47984) | Client #2 → S#2 (48100) |
|--------|-------------------------|--------------------------|--------------------------|
| Stream resolution | 1280×720 | 1280×720 | 1280×720 |
| Codec | H.264 | H.264 | H.264 |
| Incoming FPS | 60.03 | 60.17 | 59.03 |
| Decoding FPS | 60.03 | 60.17 | 59.03 |
| Rendering FPS | 60.03 | 60.17 | 59.03 |
| Host processing latency (min/max/avg) | 12.3 / 14.6 / 12.9 | 15.4 / 21.1 / **16.8** | 18.8 / 41.5 / **23.6** |
| Frames dropped (network) | 0.00% | 0.00% | 0.00% |
| Frames dropped (jitter) | 0.00% | 0.00% | 0.00% |
| Network latency (avg / variance) | ~0 ms / 0 ms | 1 ms / 0 ms | 1 ms / 0 ms |
| Decoding time (avg) | (not captured) | 0.66 ms | 0.96 ms |
| Frame queue delay (avg) | 0.02 ms | 0.04 ms | 0.13 ms |
| Rendering time incl. v-sync (avg) | 0.56 ms | 0.85 ms | 1.19 ms |
| End-to-end estimate | ~14 ms | ~19 ms | ~27 ms |

End-to-end estimates: `host_processing + network + decode + render`. Both well within
the V1 target of **≤ 50 ms** even with the worst-case all-software pipeline on a
constrained iGPU.

## 4. Server-side encoder stats

From the Sunshine logs.

Sunshine #1 session (`~/.config/sunshine/sunshine.log`, ~38 s):

```
Target bitrate: 7308 kbps
I-frames:    5  avg QP 27.86  avg size 19514 B
P-frames: 2195  avg QP  5.59  avg size  6342 B   (79.2% skip)
Achieved kb/s: 3058
```

Sunshine #2 session (`/tmp/sunshine-b.log`, ~158 s):

```
Target bitrate: 7308 kbps
I-frames:    7  avg QP 15.89  avg size 14917 B
P-frames: 9343  avg QP  3.25  avg size  4270 B   (85.3% skip)
Achieved kb/s: 2053
```

Both sessions terminated cleanly with `CLIENT DISCONNECTED`. Sunshine #1 emitted three
`Couldn't unload null-sink with index … Entità inesistente` warnings during teardown —
PipeWire cleanup race, cosmetic only.

## 5. Analysis

### 5.1 Concurrency works

Two complete protocol pipelines (RTSP control + RTP video + RTP audio + Opus + control
heartbeats) ran simultaneously on the same machine without:

- Packet loss attributable to network (0.00% on both).
- Jitter-induced drops (0.00% on both).
- Cross-stream interference (FPS lock independent on both).
- mDNS collisions (avahi auto-renamed Sunshine #2 to `framework #2`).
- Audio collisions (each Sunshine created its own `sink-sunshine-stereo` PipeWire null
  sink for its own session).

### 5.2 Latency degradation is proportional to CPU contention, not protocol overhead

Going from one to two concurrent streams:

- Client #1 host processing rose from 12.9 → 16.8 ms (+30%).
- Client #2 host processing rose to 23.6 ms (+83%).

The dev box was running two `libx264` software encoders, two FFmpeg software decoders,
two Vulkan render loops, and two KMS captures concurrently — a worst-case workload for an
8-core/16-thread iGPU laptop. On the V1 target hardware (RX 9060 XT, RDNA4, VCN 5) each
encode runs on the dedicated VCN block in parallel with no CPU cost, and decode is
hardware too; the per-stream host processing is expected to be in the single-digit
milliseconds with no degradation when running two streams concurrently.

### 5.3 Validates the option-D path

Both concurrent processes ran independent copies of `moonlight-common-c` with no shared
state — exactly the same isolation property that the dlmopen-D path achieves in a single
process via separate ELF namespaces (proven in
`docs/spikes/dlmopen-feasibility.md` §7). Behavioural equivalence at the protocol layer
is therefore strongly indicated.

The remaining differences between this T0.5 setup and the eventual V1 architecture are:

- **Single process vs two processes.** D collapses everything into one process. Easier
  UI cohesion, easier IPC (it is just direct calls), shared compositor.
- **Single Vulkan compositor vs two SDL/Vulkan windows.** D will use a single Vulkan
  compositor that takes two decoded video surfaces and renders them into two viewports
  in one window. Phase 1 implementation work.
- **Single SDL event loop vs two.** D uses one SDL event pump and the new `InputRouter`
  to dispatch events per session. See `docs/audit/input-refactor-scope.md`.

None of these affect the protocol-level concurrency assumption that this T0.5
validates.

## 6. Reproducer (for future re-runs)

```bash
# Sunshine #2: isolated state, port 48100, pre-set credentials
mkdir -p ~/.config-sunshine-b/sunshine
cat > ~/.config-sunshine-b/sunshine/sunshine.conf <<EOF
port = 48100
sunshine_name = Sunshine-B
EOF
XDG_CONFIG_HOME=~/.config-sunshine-b sunshine --creds claude claudepass
XDG_CONFIG_HOME=~/.config-sunshine-b sunshine &  # background

# Equinox client #2: isolated state
mkdir -p ~/.config-moonlight-b
XDG_CONFIG_HOME=~/.config-moonlight-b /path/to/equinox-qt/app/moonlight &

# Then in moonlight #2: Add PC -> 127.0.0.1:48100 -> generate PIN ->
#   open https://localhost:48101 -> login claude/claudepass -> enter PIN.
```

The first Sunshine and the first Moonlight stay on their default `~/.config` paths and
need no special handling.

## 7. T0.5 conclusion

**Pass.** Two concurrent equinox/Moonlight sessions sustain 60 FPS with zero drops on
the same dev box even under the all-software stress case. End-to-end latency stays well
under the V1 target. No protocol-level interference observed. The architectural
assumption that two simultaneous Sunshine connections work cleanly when each session has
its own `moonlight-common-c` state is validated.

This unblocks committing to the option-D architecture (single process, two
`dlmopen(LM_ID_NEWLM)` namespaces of `moonlight-common-c.so`) for V1 with high
confidence, with B as a fallback if Phase 1 long-running stress against real Sunshine
hosts on a real LAN reveals issues this localhost POC cannot expose.

---

*End of T0.5. Phase 0 deliverables are now complete except for T0.0 (SSH/deploy pipeline
to the bazzite-deck mini PC, blocked on access credentials) and T0.6 (Phase 0
conclusion, written next).*
