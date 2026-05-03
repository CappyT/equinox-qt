/*
 * test_dlmopen.c -- pin the architectural assumptions of MlcWrapper.
 *
 * Each sub-test loads ./libmoonlight-common-c.so via dlmopen(LM_ID_NEWLM, ...) and
 * verifies a property the V1 architecture depends on:
 *
 *   1. basic_load_and_init       -- the shared library loads, a representative
 *                                   Li* symbol resolves, and calling it does what
 *                                   it advertises (zeroes a buffer prefix).
 *
 *   2. dual_namespace_globals    -- when the same .so is loaded twice with
 *                                   LM_ID_NEWLM, every extern global in
 *                                   Limelight-internal.h that we expose has a
 *                                   distinct address per namespace.
 *
 *   3. dual_namespace_writes     -- a write through ns1's pointer to RtspPortNumber
 *                                   does not affect ns2's value, and vice versa.
 *
 *   4. openssl_libs_isolated     -- /proc/self/maps shows libssl, libcrypto and
 *                                   libmoonlight-common-c each loaded twice (one
 *                                   independent copy per namespace) so OpenSSL
 *                                   internal state is namespace-local.
 *
 * Exit code: 0 when all tests pass, 1 otherwise.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#define MLC_PATH "./libmoonlight-common-c.so"

typedef void (*li_init_sc_t)(void *);

/* Subset of the extern globals declared in moonlight-common-c/src/Limelight-internal.h.
 * These are the ones whose isolation matters most for two concurrent connections;
 * extending the list later is mechanical. */
static const char *KNOWN_GLOBALS[] = {
    "ListenerCallbacks", "VideoCallbacks", "AudioCallbacks",
    "StreamConfig", "RemoteAddrString", "RemoteAddr", "LocalAddr",
    "NegotiatedVideoFormat", "ConnectionInterrupted",
    "RtspPortNumber", "ControlPortNumber", "AudioPortNumber", "VideoPortNumber",
    "AudioPacketDuration", "AudioEncryptionEnabled",
    "HighQualitySurroundSupported", "HighQualitySurroundEnabled",
    "ReferenceFrameInvalidationSupported",
    "EncryptionFeaturesSupported", "EncryptionFeaturesEnabled",
    "AppVersionQuad", "SunshineFeatureFlags",
    NULL
};

#define PASS(name)            do { printf("[PASS] %s\n", name); return 0; } while (0)
#define FAIL(name, fmt, ...)  do { printf("[FAIL] %s: " fmt "\n", name, ##__VA_ARGS__); return 1; } while (0)

static int test_basic_load_and_init(void)
{
    const char *name = "basic_load_and_init";

    void *h = dlmopen(LM_ID_NEWLM, MLC_PATH, RTLD_NOW);
    if (!h) FAIL(name, "dlmopen: %s", dlerror());

    li_init_sc_t init = (li_init_sc_t)dlsym(h, "LiInitializeStreamConfiguration");
    if (!init) {
        const char *err = dlerror();
        dlclose(h);
        FAIL(name, "dlsym LiInitializeStreamConfiguration: %s", err ? err : "(null)");
    }

    /* STREAM_CONFIGURATION is opaque here; LiInitializeStreamConfiguration zeroes
     * it. A buffer comfortably larger than the actual struct lets us verify the
     * call does its job without depending on the struct definition. */
    char buf[1024];
    memset(buf, 0xCC, sizeof(buf));
    init(buf);

    int zeroed = 1;
    for (size_t i = 0; i < 64; i++) {
        if (buf[i] != 0) { zeroed = 0; break; }
    }
    dlclose(h);

    if (!zeroed) FAIL(name, "LiInitializeStreamConfiguration left non-zero bytes in struct prefix");
    PASS(name);
}

static int test_dual_namespace_globals(void)
{
    const char *name = "dual_namespace_globals";

    void *h1 = dlmopen(LM_ID_NEWLM, MLC_PATH, RTLD_NOW);
    void *h2 = dlmopen(LM_ID_NEWLM, MLC_PATH, RTLD_NOW);
    if (!h1 || !h2) {
        const char *err = dlerror();
        if (h1) dlclose(h1);
        if (h2) dlclose(h2);
        FAIL(name, "dlmopen: %s", err ? err : "(null)");
    }

    int isolated = 0, shared = 0, missing = 0;
    for (size_t i = 0; KNOWN_GLOBALS[i]; i++) {
        void *a = dlsym(h1, KNOWN_GLOBALS[i]);
        void *b = dlsym(h2, KNOWN_GLOBALS[i]);
        if (!a || !b) { missing++; continue; }
        if (a == b) shared++; else isolated++;
    }

    dlclose(h1); dlclose(h2);

    printf("    isolated=%d shared=%d missing=%d\n", isolated, shared, missing);
    if (missing > 0) FAIL(name, "%d expected globals not exported by the .so", missing);
    if (shared > 0)  FAIL(name, "%d globals shared across namespaces (must be 0)", shared);
    if (isolated == 0) FAIL(name, "no globals tested -- KNOWN_GLOBALS array is empty?");
    PASS(name);
}

static int test_dual_namespace_writes(void)
{
    const char *name = "dual_namespace_writes";

    void *h1 = dlmopen(LM_ID_NEWLM, MLC_PATH, RTLD_NOW);
    void *h2 = dlmopen(LM_ID_NEWLM, MLC_PATH, RTLD_NOW);
    if (!h1 || !h2) {
        const char *err = dlerror();
        if (h1) dlclose(h1);
        if (h2) dlclose(h2);
        FAIL(name, "dlmopen: %s", err ? err : "(null)");
    }

    uint16_t *p1 = (uint16_t *)dlsym(h1, "RtspPortNumber");
    uint16_t *p2 = (uint16_t *)dlsym(h2, "RtspPortNumber");
    if (!p1 || !p2) {
        dlclose(h1); dlclose(h2);
        FAIL(name, "dlsym RtspPortNumber");
    }

    *p1 = 12345;
    *p2 = 54321;
    uint16_t v1 = *p1, v2 = *p2;

    dlclose(h1); dlclose(h2);

    if (v1 != 12345 || v2 != 54321) {
        FAIL(name, "writes interfered (ns1=%u expected 12345, ns2=%u expected 54321)", v1, v2);
    }
    PASS(name);
}

static int count_loaded_lib(const char *needle)
{
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", getpid());
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    int count = 0;
    char line[2048];
    while (fgets(line, sizeof(line), f)) {
        /* Each .so contributes multiple memory mappings (text, rodata, data, bss).
         * We count only the executable text segment to get one entry per load. */
        if (strstr(line, " r-xp ") == NULL) continue;
        if (strstr(line, needle)) count++;
    }
    fclose(f);
    return count;
}

static int test_openssl_libs_isolated(void)
{
    const char *name = "openssl_libs_isolated";

    /* Measured as a delta: previous tests in this binary may leave residual
     * mappings (notably libssl/libcrypto, which are notoriously hard to unload
     * because of their global init state -- dlclose lowers the refcount but the
     * kernel keeps the pages mapped). What matters for option D is that EACH
     * fresh dlmopen brings in a fresh independent copy. */
    int mlc_before    = count_loaded_lib("libmoonlight-common-c.so");
    int libssl_before = count_loaded_lib("libssl.so");
    int libcryp_before = count_loaded_lib("libcrypto.so");

    void *h1 = dlmopen(LM_ID_NEWLM, MLC_PATH, RTLD_NOW);
    void *h2 = dlmopen(LM_ID_NEWLM, MLC_PATH, RTLD_NOW);
    if (!h1 || !h2) {
        const char *err = dlerror();
        if (h1) dlclose(h1);
        if (h2) dlclose(h2);
        FAIL(name, "dlmopen: %s", err ? err : "(null)");
    }

    int mlc_delta    = count_loaded_lib("libmoonlight-common-c.so") - mlc_before;
    int libssl_delta = count_loaded_lib("libssl.so")                - libssl_before;
    int libcryp_delta = count_loaded_lib("libcrypto.so")            - libcryp_before;

    dlclose(h1); dlclose(h2);

    printf("    delta after 2 dlmopens: mlc=%+d  libssl=%+d  libcrypto=%+d\n",
           mlc_delta, libssl_delta, libcryp_delta);

    if (mlc_delta != 2)     FAIL(name, "mlc grew by %d (expected +2)", mlc_delta);
    if (libssl_delta != 2)  FAIL(name, "libssl grew by %d (expected +2)", libssl_delta);
    if (libcryp_delta != 2) FAIL(name, "libcrypto grew by %d (expected +2)", libcryp_delta);
    PASS(name);
}

int main(void)
{
    int (*tests[])(void) = {
        test_basic_load_and_init,
        test_dual_namespace_globals,
        test_dual_namespace_writes,
        test_openssl_libs_isolated,
    };
    const size_t n = sizeof(tests) / sizeof(*tests);

    int failed = 0;
    for (size_t i = 0; i < n; i++) {
        if (tests[i]() != 0) failed++;
    }

    printf("\n=== %zu/%zu passed", n - failed, n);
    if (failed) printf(", %d failed ===\n", failed);
    else        printf(" ===\n");
    return failed ? 1 : 0;
}
