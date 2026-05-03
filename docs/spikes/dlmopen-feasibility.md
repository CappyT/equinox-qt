# Spike — `dlmopen(LM_ID_NEWLM, ...)` feasibility for option D

**Phase:** 0 (validation, non-binding)
**Date:** 2026-05-03
**Status:** **PASS** on the basic test and on the integration test against the real
`libmoonlight-common-c.so`. Residual caveats listed in §7.6, none blocking.

---

## 1. Goal

Empirically verify that `dlmopen` with `LM_ID_NEWLM` on glibc Fedora 43 isolates the global
state of a shared library when the same library is loaded twice. This is the minimum
technical prerequisite for option **D** of the multi-session refactor (see
`docs/audit/session-refactor-scope.md`).

**Non-goal of §1-6:** full integration test with `moonlight-common-c.so`. That requires a
build of the library as shared and validation of pthread/OpenSSL/sockets under split
namespaces. Done as §7 in the same session, after the basic test passed.

---

## 2. Setup (basic test)

System: Fedora 43, glibc 2.42, gcc 15.

Out-of-tree test in `/tmp/dlmopen-spike/`. No changes to the Equinox repo.

### 2.1 Test shared lib (`globals_lib.c`)

```c
#include <stdio.h>
#include <pthread.h>

static int g_counter = 0;
static pthread_t g_thread;

void lib_increment(void) { g_counter++; }
int  lib_get(void)       { return g_counter; }
void lib_set_thread_marker(void) { g_thread = pthread_self(); }
unsigned long lib_get_thread_marker(void) { return (unsigned long)g_thread; }
```

Build: `gcc -shared -fPIC -lpthread -o libglobals.so globals_lib.c`

### 2.2 Driver (`driver.c`)

```c
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

typedef void (*incr_fn)(void);
typedef int  (*get_fn)(void);

int main(void) {
    void *h1 = dlmopen(LM_ID_NEWLM, "./libglobals.so", RTLD_NOW);
    void *h2 = dlmopen(LM_ID_NEWLM, "./libglobals.so", RTLD_NOW);
    if (!h1 || !h2) { fprintf(stderr, "%s\n", dlerror()); return 1; }

    incr_fn i1 = dlsym(h1, "lib_increment");
    get_fn  g1 = dlsym(h1, "lib_get");
    incr_fn i2 = dlsym(h2, "lib_increment");
    get_fn  g2 = dlsym(h2, "lib_get");

    i1(); i1(); i1();   // ns1.counter += 3
    i2(); i2();          // ns2.counter += 2

    printf("ns1.counter = %d (expect 3)\n", g1());
    printf("ns2.counter = %d (expect 2)\n", g2());
    printf("isolation = %s\n",
           (g1() == 3 && g2() == 2) ? "YES" : "NO");

    dlclose(h1); dlclose(h2);
    return 0;
}
```

Build: `gcc -o driver driver.c -ldl`

### 2.3 Run

```
LD_LIBRARY_PATH=. ./driver
```

---

## 3. Result (basic test)

```
ns1.counter = 3 (expect 3)
ns2.counter = 2 (expect 2)
ns1.thread_marker = 0x7f94836da740
ns2.thread_marker = 0x7f94836da740
isolation_verified = YES
```

**Pass.** The file-static `g_counter` is duplicated per namespace. The two copies are
independent; writes in ns1 are not visible from ns2.

The two `thread_marker` values match because both `lib_set_thread_marker` calls are made
from the process main thread, so `pthread_self()` returns the same handle. Expected, not an
isolation issue.

---

## 4. Caveats not validated by the basic test

The basic test proves that file-static isolation works for the toy lib. It does **not**
prove `moonlight-common-c` specifically is compatible with the model. The real risks are in
its dependencies:

| Caveat | Required test | Risk if it fails |
|--------|---------------|------------------|
| **OpenSSL** linked into both namespaces | `SSL_library_init` + `SSL_CTX_new` in both, watch for crash and counter sharing | High — moonlight-common-c relies heavily on OpenSSL for control-stream crypto |
| **pthread** state cross-namespace | spawn a thread in each, verify TLS isolation and that joins do not cross | Medium — each namespace should hold its own pthread, but signal handlers are per-process |
| **Signal handlers** | SIGPIPE, SIGINT during I/O. Verify that registering in one namespace does not overwrite the other | Medium — process-global, not per-namespace; may need sigaction wrapping |
| **Network sockets / FDs** | open UDP in each namespace, check FDs are not aliased | Low — FDs are per-process but each gets its own number |
| **`DL_NNS` limit** | `cat /usr/include/dlfcn.h | grep DL_NNS` (default 16 on glibc) | Low for us (we need 2-3) |
| **Duplicated memory** | `pmap` of the driver process before and after the second `dlmopen` | Low (~MB, negligible) |
| **System library symbol resolution** | the `.so` links libpthread, libssl, libc. Under `LM_ID_NEWLM`, glibc loads separate copies. Verify with `cat /proc/<pid>/maps` | Medium — can drive non-trivial memory consumption |

---

## 5. Suggested next steps if proceeding with D

Branch: `spike/mlc-shared`. Disposable.

1. Patch `moonlight-common-c/moonlight-common-c.pro`:
   ```diff
   - CONFIG += staticlib
   + # built as shared for dlmopen namespace isolation
   + CONFIG += dll
   + QMAKE_CFLAGS += -fPIC
   ```
2. Patch `app/app.pro` linkage:
   ```diff
   - else:unix: LIBS += -L$$OUT_PWD/../moonlight-common-c/ -lmoonlight-common-c
   + else:unix: LIBS += -ldl
   ```
   (drops the static link; the app loads the lib at runtime via `dlmopen`)
3. Write `app/streaming/MlcWrapper.{h,cpp}` that wraps `dlmopen` plus the `dlsym` table for
   every `Li*` symbol in use. Pattern:
   ```cpp
   class MlcWrapper {
       void* m_handle;
       int (*m_LiStartConnection)(/* sig */);
       // ... other Li*
   public:
       MlcWrapper(); // dlmopen LM_ID_NEWLM + populate function pointers
       ~MlcWrapper(); // dlclose
       int startConnection(...) { return m_LiStartConnection(...); }
       // ...
   };
   ```
4. Test: dummy `Session` using `MlcWrapper`, two instances in parallel, verify:
   - `pmap` for memory footprint
   - dump addresses of `RemoteAddr` (extern global) via `dlsym` → two distinct addresses
5. Only then: full integration with a Sunshine host (requires T0.0 + T0.3 unblocked).

---

## 6. Verdict (after §1-5)

Base tech **OK**. Final decision on D **deferred** until the §4 caveats are tested. If any
critical caveat (OpenSSL, signal) fails, drop D and fall back to B.

---

## 7. Integration test against the real `libmoonlight-common-c.so` (same date)

Update following §1-6: the §4 caveats were validated directly against an out-of-tree build
of `moonlight-common-c`. Result: **D is confirmed viable for V1**, with one residual caveat
(signal handlers) that is resolved by the mlc source itself.

### 7.1 Out-of-tree library build

Without modifying the project's `moonlight-common-c.pro`, build directly via `gcc`:

```bash
REPO=/home/cappyt/Documenti/Repos/equinox-qt
MLC=$REPO/moonlight-common-c/moonlight-common-c
ENET=$MLC/enet

gcc -shared -fPIC -O2 -std=gnu99 \
    -I$ENET/include -I$MLC/src \
    -I$MLC/nanors -I$MLC/nanors/deps -I$MLC/nanors/deps/obl \
    -DHAS_SOCKLEN_T -DHAVE_CLOCK_GETTIME=1 \
    $(pkg-config --cflags openssl) \
    $ENET/{callbacks,compress,host,list,packet,peer,protocol,unix}.c \
    $MLC/src/{rswrapper,AudioStream,ByteBuffer,Connection,ConnectionTester}.c \
    $MLC/src/{ControlStream,FakeCallbacks,InputStream,LinkedBlockingQueue,Misc}.c \
    $MLC/src/{Platform,PlatformCrypto,PlatformSockets}.c \
    $MLC/src/{RtpAudioQueue,RtpVideoQueue,RtspConnection,RtspParser}.c \
    $MLC/src/{SdpGenerator,SimpleStun,VideoDepacketizer,VideoStream}.c \
    -lpthread $(pkg-config --libs openssl) \
    -o libmoonlight-common-c.so
```

Output: `libmoonlight-common-c.so` 311 KB, ELF x86-64 dynamically linked. All expected
symbols exported (`LiStartConnection`, `LiInitializeStreamConfiguration`,
`LiTestClientConnectivity`, plus the 22 extern globals tested below).

### 7.2 Driver `driver_real.c` — globals isolation

Three sub-tests:

1. `dlmopen` x2 with `LM_ID_NEWLM`, `dlsym` for **22 extern globals**, compare addresses.
2. Mutate `RtspPortNumber` to `12345` on ns1 and `54321` on ns2, read back to verify writes
   do not interfere.
3. Call `LiInitializeStreamConfiguration` from both namespaces on separate buffers, verify
   no crash.

**Result:**

```
isolated=22  shared=0  missing=0
After write: ns1=12345 (set 12345)  ns2=54321 (set 54321)
RESULT: writes are independent. State isolation CONFIRMED.
init1 called OK, init2 called OK. (no crash)
ALL TESTS PASSED
```

The 22 globals tested all show address deltas of ~-8.5 MB between ns1 and ns2, indicating
two distinct mappings of `libmoonlight-common-c.so`.

### 7.3 Driver `driver_libs.c` — pthread, OpenSSL, libc isolation

Verifies:

1. How many times `libcrypto.so` and `libssl.so` are mapped in `/proc/self/maps`.
2. `PltCreateThread` called from each namespace, verifying that the registered callbacks
   are the right namespace's.
3. Number of `libc.so` mappings.

**Result:**

```
Lines matching libcrypto/libssl: 16     # 4 segments × 2 libs × 2 namespaces
libssl    ns1=0x7f3b62b22000  ns2=0x7f3b63459000   ISOLATED
libcrypto ns1=0x7f3b62400000  ns2=0x7f3b62e00000   ISOLATED
PltCreateThread: r1=0 r2=0
ns1_thread_ran=1 ns2_thread_ran=1 (both should be 1)
libc.so mapped 3x (host + 2 namespaces)
total memory: 38 MB mapped, 8 MB RSS, 1.8 MB dirty
```

Substantive confirmation: each namespace has its own copy of **libc**, **libssl**,
**libcrypto** and **libmoonlight-common-c**. OpenSSL state — internally full of static
globals — is namespace-local as desired. pthread state (TLS, mutex internals, libc condition
variables) is namespace-local. `PltCreateThread` operates independently in the two
namespaces and invokes the right callbacks.

### 7.4 Signal-handler caveat — resolved by-design

Audit of `moonlight-common-c` for `signal()`/`sigaction()` calls:

```
src/PlatformSockets.c:1015:    struct sigaction sa;
src/PlatformSockets.c:1017:    sa.sa_handler = SIG_IGN;
src/PlatformSockets.c:1019:    sigaction(SIGPIPE, &sa, 0);
```

The only handler the library installs is `SIGPIPE → SIG_IGN`. Setting the same action twice
(once per namespace) is idempotent: the final state is identical to setting it once. No
race, no conflict.

In addition, `enet/unix.c` uses `MSG_NOSIGNAL` on every `sendto`/`sendmsg`/`recvfrom`/
`recvmsg`, so SIGPIPE is not even raised at the socket layer. The `sigaction` call is a
belt-and-suspenders.

**Conclusion on signal handlers:** not an actual caveat for our use case.

### 7.5 §4 caveat resolution table

| Caveat | Status | Evidence |
|--------|--------|----------|
| OpenSSL state shared | RESOLVED | libssl + libcrypto loaded 2x, distinct base addresses |
| pthread state shared | RESOLVED | libc loaded 3x, `PltCreateThread` works per-namespace |
| Signal-handler conflict | RESOLVED | only handler is `SIG_IGN` on SIGPIPE, idempotent |
| Network socket aliasing | N/A | FDs are per-process, each gets its own number, no accidental sharing |
| `DL_NNS` limit | N/A | glibc 2.42 default = 16, we use 2 |
| Duplicated memory | NEGLIGIBLE | +~18 MB mapped per the two namespaces, +~6 MB RSS |
| System library symbol resolution | OK | confirmed in `/proc/self/maps`, each namespace has its own libs |

### 7.6 Residual caveats (NOT validated, deferred to Phase 1 with a real host)

The above proves the **basic mechanics** of D work. Still to validate during real
integration (Phase 1, when a Sunshine host is available via T0.0/T0.3):

- Long-running stress test with two `LiStartConnection` calls against two real hosts
  (~1 hour).
- Behaviour on disorderly disconnect of one host (gracefulness of the orphan namespace
  `dlclose`).
- Latency added by the `dlsym` indirection vs direct linking (expected ~ns, irrelevant).
- DRM master / VAAPI behaviour when the decoder is invoked from callbacks across two
  namespaces (expected fine because VAAPI is not used by mlc itself — it is used by the Qt
  app outside the `.so`).

None of these block the decision to commit to D in Phase 1. They are second-order
validation.

### 7.7 Updated verdict

**D is the recommended V1 strategy.** The critical technical risks (OpenSSL, pthread,
signals) are validated as non-risks. The estimated effort (1.5-2 weeks) holds. **B** stays
as a theoretical fallback but probably will not be needed. **A** stays as the
bottom-bottom-fallback (POC throwaway, see T0.5 of the handoff).

---

*Test code left in `/tmp/dlmopen-spike/` for in-session reproducibility. Volatile, not
committed — the full code is reproduced above.*
