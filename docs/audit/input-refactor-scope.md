# T0.4 (extension) — `SdlInputHandler` audit and routing-design scope

**Phase:** 0 (spike & validation)
**Date:** 2026-05-03
**Status:** complete

---

## Executive summary

`SdlInputHandler` itself is dramatically cleaner than `Session`. Zero file-static globals,
all state already per-instance via `m_*` members, only one `static const` lookup table
(`k_ButtonMap`), and the five `static` SDL timer callbacks all use `void* param` correctly
to retrieve their per-instance context. The class as written can be instantiated multiple
times in the same process with no internal collision.

The catch is **outside** the class: code in `input/*.cpp` reaches back into `Session` via
the `Session::get()` / `Session::s_ActiveSession` singleton pattern in seven places. Those
need to be redirected to a per-instance owning-session pointer.

The genuinely new component required is an **input router** that demultiplexes the
process-global SDL event queue to the correct `SdlInputHandler` instance based on the
controller binding map (handoff §5).

Net effect on Phase 1 estimate: roughly the same total (~2-3 days), but composition shifts
from "refactor" to "back-pointer fix + new router code".

---

## 1. Class shape (`app/streaming/input/input.h`)

`SdlInputHandler` is a regular C++ class, not a singleton:

- Constructor: `SdlInputHandler(StreamingPreferences& prefs, int streamWidth, int streamHeight)`
- Multiple instances are allowed by the API.
- All runtime state lives in `m_*` instance members.

Private static functions (all timer callbacks, signature `Uint32(*)(Uint32, void*)`):

- `longPressTimerCallback`
- `mouseEmulationTimerCallback`
- `releaseLeftButtonTimerCallback`
- `releaseRightButtonTimerCallback`
- `dragTimerCallback`

Each is registered with SDL passing a per-instance pointer as `param` (verified for
`mouseEmulationTimerCallback`: `auto gamepad = reinterpret_cast<GamepadState*>(param);`).
**No `Session::get()` from inside any of them.**

Static class data:

- `static const int k_ButtonMap[]` — read-only lookup table for the SDL button → Limelight
  flag mapping. No isolation issue.

Per-instance state (~30 fields), notable ones:

- `m_Window` — the `SDL_Window*` for this session
- `m_GamepadState[MAX_GAMEPADS]` — array of 16 controller slots, instance-owned
- `m_KeysDown` — `QSet<short>` of currently-pressed keys
- `m_StreamWidth/Height` — per-session geometry
- `m_GamepadMask`, `m_MultiController`, etc. — per-session policy

**`MAX_GAMEPADS = 16`** per instance. For Equinox V1, each session uses only slot 0
(Sunshine sees a single Player 1 per host); the other 15 slots stay empty. Wasteful but
fully compatible with the existing event handling.

## 2. File-by-file static count

```
228 abstouch.cpp     0 statics
1008 gamepad.cpp     0 statics
451 input.cpp        0 statics
465 keyboard.cpp     0 statics
312 mouse.cpp        0 statics
175 reltouch.cpp     0 statics
2639 total           0 statics
```

No file-static globals in any of the 6 files. Every variable is either a class member, a
function-local automatic, or a constant.

## 3. The seven `Session::get()` / `s_ActiveSession` callers

| File | Line | Call | Effect |
|------|------|------|--------|
| `keyboard.cpp` | 45 | `Session::s_ActiveSession->toggleFullscreen()` | Alt+Enter etc. → fullscreen |
| `keyboard.cpp` | 57-58 | `Session::get()->getOverlayManager().setOverlayState(...)` | Toggle stats overlay |
| `keyboard.cpp` | 147 | `Session::get()->setShouldExit(true)` | Quit combo |
| `gamepad.cpp` | 333 | `Session::get()->notifyMouseEmulationMode(false)` | Disable mouse emu on combo |
| `gamepad.cpp` | 343 | `Session::get()->notifyMouseEmulationMode(true)` | Enable mouse emu on combo |
| `gamepad.cpp` | 389-390 | `Session::get()->getOverlayManager().setOverlayState(...)` | Toggle stats overlay (gamepad) |
| `gamepad.cpp` | 727 | `Session::get()->notifyMouseEmulationMode(false)` | Stop mouse emu on focus loss |
| `input.cpp` | 204 | `Session::get()->notifyMouseEmulationMode(false)` | Same |

In single-session today these calls all reach the unique `Session`. In dual-session, an
event from controller bound to side A must affect side A's session, not whichever session
is "currently active". The fix is a back-pointer:

```cpp
class SdlInputHandler {
public:
    explicit SdlInputHandler(Session* owningSession,
                             StreamingPreferences& prefs,
                             int streamWidth, int streamHeight);
    // ...
private:
    Session* m_OwningSession;  // not owned, lifetime tied to Session
    // ...
};
```

Then replace each `Session::get()->X()` with `m_OwningSession->X()` (in input/*.cpp and in
input.h-private helpers as needed). The seven callers above plus any pulled in by their
helpers. **Effort: half a day max, very mechanical.**

## 4. SDL global state implications

### 4.1 SDL subsystem init

`input.cpp:156-184` calls `SDL_InitSubSystem(SDL_INIT_JOYSTICK | GAMECONTROLLER | HAPTIC)`
in the constructor and the matching `SDL_QuitSubSystem` in the destructor. SDL refcounts
init/quit calls per subsystem, so two `SdlInputHandler` instances in the same process
will:

- First instance ctor: actually inits subsystems.
- Second instance ctor: refcount bump, no-op work.
- First instance dtor: refcount drop, no-op work.
- Second instance dtor: refcount drop to zero, actual cleanup.

Harmless. The only observable effect is that the order of construction matters for who
"owns" the underlying init, but since the work is idempotent, this does not affect
correctness.

### 4.2 SDL event queue (the routing problem)

`SDL_PollEvent` pulls from a process-global queue. Every controller event goes into the
same queue regardless of which `SdlInputHandler` "should" receive it. With two
`SdlInputHandler` instances in the same process, naive code where each one calls
`SDL_PollEvent` would race for events.

The Equinox solution (handoff §5) is an **input router** sitting between SDL and the
`SdlInputHandler` instances:

```
SDL event queue
     │
     ▼
┌────────────────────────────────────┐
│ Equinox InputRouter                │
│  - reads SDL events (single owner) │
│  - looks up SDL_JoystickID in      │
│    binding map                     │
│  - dispatches to handler.handleX() │
└────────┬────────────────┬──────────┘
         │ Side Left      │ Side Right
         ▼                ▼
  SdlInputHandler  SdlInputHandler
       (Session A)      (Session B)
```

The router owns the `SDL_PollEvent` loop and the `map<SDL_JoystickID, ControllerBinding>`
state. Each `SdlInputHandler` continues to own its own per-instance state and handles
the per-event work; it just receives events via direct method calls instead of pulling
from the global queue.

Mouse and keyboard events go to whichever side has focus (default: side that owns the
window the mouse is in, or both if window is split-screen logical). Implementation
detail for Phase 2 binding-mode UI.

### 4.3 SDL_PushEvent (gamepad.cpp:375)

The codebase does push events back into the queue (e.g. for cross-thread callbacks). Under
the router model, pushed events still go to the global queue, the router reads them, and
dispatches based on whatever discriminator the push embedded (typically the source
controller's `SDL_JoystickID`).

## 5. Phase 1 effort estimate (revised)

Replaces the line "SdlInputHandler refactor away from singleton assumptions: 1-2 days"
from `session-refactor-scope.md` §3 with two finer items:

| Component | Person-days |
|-----------|-------------|
| Add `m_OwningSession` back-pointer in `SdlInputHandler`, replace seven `Session::get()` callers | 0.5 |
| Build the new `InputRouter` class (event pump + binding map + dispatch) | 2-3 |

Other Phase 1 items unchanged. Total Phase 1 budget remains in the 11-18 person-days
window from the original audit.

## 6. Open questions

1. **Where does the `InputRouter` live in the process?** Likely owned by the Equinox top-
   level object that also manages the two `Session` instances. Out-of-scope for Phase 0,
   to be decided at Phase 1 design kickoff.
2. **Mouse/keyboard routing in split-screen.** The single mouse cursor and the single
   physical keyboard need a focus model. Default: keyboard goes to whichever side has the
   game window currently grabbed; mouse follows the cursor's X coordinate (left half →
   side A, right half → side B). To be confirmed in Phase 2 with the binding-mode UI work.
3. **Hot-swap during play.** Already covered by the handoff §5.5 design — SDL emits
   `CONTROLLERDEVICEADDED`/`REMOVED` events, the router updates the binding map, an
   orphaned side gets a re-binding prompt while the other keeps playing. Implementation
   in Phase 2.

---

*End of T0.4 extension.*
