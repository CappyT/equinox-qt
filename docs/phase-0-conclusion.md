# Phase 0 — conclusion

**Date:** 2026-05-03
**Branch:** `phase-0/audit-and-spike-d` (6 commits ahead of `main`)
**Recommendation:** **GO**, with the caveat that T0.0 (deploy pipeline to the bazzite-deck
mini PC) is still blocked on SSH access and must complete before Phase 1 stream-on-target
testing can begin.

---

## 1. Executive summary

Phase 0 set out to validate the architectural assumptions in the project handoff before
committing to the implementation plan. The validation produced one significant
correction to the original plan and one concrete architectural decision:

- **Correction:** the multi-instance refactor scope is dominated by `moonlight-common-c`
  (the upstream protocol library), not by the `Session` class on the app side. The
  library is single-connection-per-process by design — 29 `extern` globals, ~314
  file-static globals, ~12 threads spawned per connection, no user-context propagation
  through callbacks.
- **Decision:** Equinox does **not** patch the source of `moonlight-common-c`. Instead
  the V1 architecture builds the library as a shared object and uses
  `dlmopen(LM_ID_NEWLM, ...)` to load two ELF-namespace-isolated copies in the same
  process — proven feasible by integration test against the real `.so`.

All Phase 0 deliverables that do not depend on remote test hardware are complete and
committed. The two remaining open items (T0.0 SSH/deploy, T0.3/T0.5 stress on real LAN)
are work-stream parallel to Phase 1 and do not block its start.

## 2. Per-task outcomes

| Task | Output | Status |
|------|--------|--------|
| T0.0 SSH/deploy to bazzite-deck mini PC | (none) | **Blocked on user — awaiting SSH credentials, hostname, deploy target paths** |
| T0.1 Dev environment | `docs/setup/dev-environment.md` | Complete |
| T0.2 Build baseline upstream | `docs/setup/build-notes.md`, working `app/moonlight` binary | Complete |
| T0.3 Baseline single-session | `docs/measurements/baseline.md` | Complete (loopback on dev box; real LAN deferred to Phase 1) |
| T0.4 Audit `Session` | `docs/audit/session-refactor-scope.md` | Complete |
| T0.4 (extension) Audit `SdlInputHandler` + InputRouter scope | `docs/audit/input-refactor-scope.md` | Complete |
| T0.5 POC dual-instance | `docs/measurements/poc-dual-instance.md` | Complete (loopback on dev box; real-host stress deferred to Phase 1) |
| Spike — `dlmopen` feasibility for option D | `docs/spikes/dlmopen-feasibility.md` | Complete, **PASS** including integration with real `libmoonlight-common-c.so` |
| T0.6 Phase 0 conclusion | `docs/phase-0-conclusion.md` (this file) | Complete |
| Branch surgery | `master` → `upstream-mirror`, new `main` as Equinox default branch | Complete |
| Personal docs gitignored | `.gitignore` updated for downstream-only `.md` | Complete |

## 3. Architectural decisions confirmed

D1 — D11 from the handoff §2 stand. New durable decisions added in Phase 0:

- **No source modifications to `moonlight-common-c`.** Saved as project memory. Multi-
  session is solved at the process / namespace level (option D primary, B and A as
  fallbacks).
- **English-only artifacts.** All committed files (`docs/`, code, comments, commit
  messages) are in English. Italian stays in chat / personal notes.
- **No `Co-Authored-By` trailers** in commit messages.

## 4. Architecture for V1

```
┌────────────── Equinox process ──────────────────────┐
│                                                      │
│   ┌──────────┐         ┌──────────┐                 │
│   │ MlcWrapper│         │ MlcWrapper│   <- two C++  │
│   │  (ns A)   │         │  (ns B)   │      wrappers │
│   │ dlmopen   │         │ dlmopen   │      around   │
│   │ LM_ID_    │         │ LM_ID_    │      dlmopen  │
│   │ NEWLM #1  │         │ NEWLM #2  │      handles  │
│   └─────┬────┘         └─────┬────┘                 │
│         │                    │                       │
│         ▼                    ▼                       │
│   ┌──────────┐         ┌──────────┐                 │
│   │ Session A│         │ Session B│                  │
│   └─────┬────┘         └─────┬────┘                  │
│         │ DMA-BUF            │ DMA-BUF               │
│         ▼                    ▼                       │
│   ┌──────────────────────────────┐                   │
│   │  Vulkan compositor           │                   │
│   │  (two viewports, letterbox)  │                   │
│   └─────────────┬────────────────┘                   │
│                 │                                    │
│  ┌─────────────────────────────────┐                 │
│  │ InputRouter                     │                 │
│  │  SDL_PollEvent → demux by       │                 │
│  │  SDL_JoystickID binding map →   │                 │
│  │  dispatch to SdlInputHandler A/B│                 │
│  └─────────────────────────────────┘                 │
└──────────────────────────────────────────────────────┘
```

Key components, all to be implemented in Phase 1:

1. `MlcWrapper` C++ class wrapping `dlmopen` plus the full `Li*` symbol table.
2. `Session` refactored to delegate all `Li*` calls to its `MlcWrapper`, and to drop
   `s_ActiveSession` plus `s_ActiveSessionSemaphore`.
3. Vulkan compositor with two viewports, letterbox layout per handoff D5 (Strategy A;
   strategies B and C deferred).
4. `InputRouter` owning `SDL_PollEvent` and the controller binding map; `SdlInputHandler`
   gets a back-pointer to its owning `Session` and replaces seven `Session::get()` callers.

## 5. Phase 1 effort estimate (revised)

From `session-refactor-scope.md` §3 plus the input-refactor adjustments from
`input-refactor-scope.md` §5:

| Component | Person-days |
|-----------|-------------|
| Patch `moonlight-common-c.pro` to build as shared `.so` (`-fPIC`, link rework) | 0.5 |
| `MlcWrapper` C++: `dlmopen` lifetime + `dlsym` table for the `Li*` API | 2-3 |
| `Session` refactor to use `MlcWrapper` instead of direct `Li*` calls | 2-3 |
| Drop `s_ActiveSession` / split `s_ActiveSessionSemaphore` per `Session` | 1-2 |
| `SdlInputHandler` add `m_OwningSession`, replace seven `Session::get()` callers | 0.5 |
| `InputRouter` (event pump + controller binding map + dispatch) | 2-3 |
| Vulkan compositor (two viewports, letterbox, DMA-BUF in via VAAPI) | 3-5 |
| Integration testing on dev box (two dummy sessions without real hosts) | 2-3 |
| **Total Phase 1** | **13.5 - 19.5 person-days** (~2.5 - 4 weeks part-time) |

Consistent with the handoff's Phase 1 budget of 2 weeks part-time, on the optimistic end.

## 6. Risks identified and mitigations

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| `dlmopen` long-running stress against real Sunshine hosts surfaces issues not seen in the loopback POC | Medium | Medium | Phase 1 milestone: 1-hour stress test against two real Sunshine hosts before locking in option D; fallback to option B (decoder child + parent compositor over Unix socket DMA-BUF) keeps option C off the table |
| Vulkan video decode on RX 9060 XT has the same extension gaps as on Phoenix1 | Low | Medium | Verify on the deployment target before implementing the Vulkan compositor; fall back to VAAPI decode if Vulkan path is incomplete |
| Bazzite-Deck mini PC has unforeseen Wayland/gaming-mode behaviour not seen on Fedora dev box | Medium | Low | T0.0 + an early Phase 1 sanity build on the target unblocks discovery |
| Building `moonlight-common-c` as `.so` triggers downstream packaging issues we have not seen yet (e.g. `RPATH`, install layout) | Low | Low | Validate within Phase 1's first 0.5 days of build-system work before committing to the rewrite |
| Sunshine on Phoenix1 has `Couldn't start http server on ports [48005, 48005]: bind: Indirizzo già in uso` when `port = 48010` is configured (mystery non-listening port collision) | (already triggered) | Workaround | Use `port = 48100` or any other base whose `port − 5` is free; documented in `poc-dual-instance.md` |

## 7. Open items needing user input

- **T0.0 unblock.** Hostname/IP for the bazzite-deck mini PC, SSH user, public-key install,
  layout for the deploy target (binary path, where to drop the Flatpak bundle when Phase 3
  packaging starts).
- **Q1 from handoff §3 (naming).** Resolved by branch creation — the project is **Equinox**.
- **Q2 (host pair persistence).** Defer to Phase 2 UX work (binding mode and host selection
  UI).
- **Q3 (binding-mode combo `L1+R1+Select`).** Defer to Phase 2.
- **Q4 (Strategy B virtual display in V1?).** Stay with Strategy A (letterbox) for V1 per
  handoff D5; B is opt-in for V1.1.
- **Q5 (in-app telemetry overlay).** Defer to Phase 3.
- **Q6 (SDL2 → SDL3 migration scope).** Upstream is mid-migration (`SDL_compat.h` shim
  present); follow upstream rather than driving our own migration.

## 8. Recommendation

**GO for Phase 1.**

All technical assumptions that Phase 0 was meant to validate are validated. The
multi-instance refactor scope is well understood and bounded. The chosen architecture
(option D — single process with two `dlmopen` namespaces) has empirical support from the
spike against the real `libmoonlight-common-c.so` for the critical isolation properties
(globals, OpenSSL, pthread, signal handlers). The protocol-level concurrency assumption
that Equinox depends on has been independently validated by the dual-instance POC.

Phase 1 can start immediately on a `phase-1/mlc-shared-build` branch with the
`moonlight-common-c.pro` toggle and `MlcWrapper` skeleton. T0.0 (deploy pipeline to the
bazzite-deck mini PC) and Phase 1 stress testing against real Sunshine hosts on a real
LAN run in parallel as soon as access is unblocked.

---

*User sign-off required to formally close Phase 0. After sign-off, merge
`phase-0/audit-and-spike-d` into `main` and cut `phase-1/mlc-shared-build` from the merge
commit.*
