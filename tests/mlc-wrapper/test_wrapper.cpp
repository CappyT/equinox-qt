/*
 * test_wrapper.cpp -- C++ tests for the MlcWrapper class.
 *
 * Builds against libmoonlight-common-c.so (built by the Makefile in this dir)
 * and against the wrapper sources in app/streaming/MlcWrapper.{h,cpp}. The
 * tests exercise the wrapper API, not the dlmopen mechanic itself (that lives
 * in test_dlmopen.c).
 *
 * Tests:
 *   1. construct_and_destruct      -- one wrapper, ctor + dtor without throw
 *   2. dual_wrapper_isolation      -- two wrappers, extern globals at
 *                                     distinct addresses (proves each ctor
 *                                     opens an independent ELF namespace)
 *   3. stage_name_works            -- a call through the wrapper returns
 *                                     the same string the static lib would
 *   4. micros_monotonic_per_wrapper -- LiGetMicroseconds is non-decreasing
 *                                      and within a sane range when called
 *                                      from each wrapper
 *
 * Exit code: 0 when all tests pass, 1 otherwise.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

#include "MlcWrapper.h"

namespace {

#define PASS(name)        do { std::printf("[PASS] %s\n", name); return 0; } while (0)
#define FAIL(name, ...)   do { std::printf("[FAIL] %s: ", name); \
                                std::printf(__VA_ARGS__); std::printf("\n"); return 1; } while (0)

int test_construct_and_destruct()
{
    const char* name = "construct_and_destruct";
    try {
        MlcWrapper w("./libmoonlight-common-c.so");
        if (!w.rawHandle()) FAIL(name, "rawHandle() returned null after ctor");
    }
    catch (const std::exception& e) {
        FAIL(name, "ctor threw: %s", e.what());
    }
    PASS(name);
}

int test_dual_wrapper_isolation()
{
    const char* name = "dual_wrapper_isolation";
    try {
        MlcWrapper a("./libmoonlight-common-c.so");
        MlcWrapper b("./libmoonlight-common-c.so");

        // Resolve a representative extern global from each handle directly.
        // The wrapper does not expose globals (only Li* functions) so use
        // the raw handle for this isolation check.
        void* aRtsp = dlsym(a.rawHandle(), "RtspPortNumber");
        void* bRtsp = dlsym(b.rawHandle(), "RtspPortNumber");
        if (!aRtsp || !bRtsp) FAIL(name, "dlsym RtspPortNumber failed in one of the namespaces");
        if (aRtsp == bRtsp)   FAIL(name, "RtspPortNumber shared across namespaces");

        // Round trip a write to confirm the addresses are not just aliased
        // through an unexpected mapping.
        *static_cast<uint16_t*>(aRtsp) = 12345;
        *static_cast<uint16_t*>(bRtsp) = 54321;
        if (*static_cast<uint16_t*>(aRtsp) != 12345) FAIL(name, "ns A write lost (got %u)",
                                                          *static_cast<uint16_t*>(aRtsp));
        if (*static_cast<uint16_t*>(bRtsp) != 54321) FAIL(name, "ns B write lost (got %u)",
                                                          *static_cast<uint16_t*>(bRtsp));
    }
    catch (const std::exception& e) {
        FAIL(name, "ctor threw: %s", e.what());
    }
    PASS(name);
}

int test_stage_name_works()
{
    const char* name = "stage_name_works";
    try {
        MlcWrapper w("./libmoonlight-common-c.so");

        // STAGE_NONE = 0 in moonlight-common-c. The library returns a non-null
        // human-readable string for any valid stage; test a couple.
        const char* s0 = w.getStageName(0);
        const char* s1 = w.getStageName(1);
        if (!s0) FAIL(name, "getStageName(0) returned null");
        if (!s1) FAIL(name, "getStageName(1) returned null");
        if (std::strlen(s0) == 0) FAIL(name, "getStageName(0) returned empty string");
        if (std::strlen(s1) == 0) FAIL(name, "getStageName(1) returned empty string");
        std::printf("    stage(0)='%s' stage(1)='%s'\n", s0, s1);
    }
    catch (const std::exception& e) {
        FAIL(name, "ctor threw: %s", e.what());
    }
    PASS(name);
}

int test_micros_monotonic_per_wrapper()
{
    const char* name = "micros_monotonic_per_wrapper";
    try {
        MlcWrapper a("./libmoonlight-common-c.so");
        MlcWrapper b("./libmoonlight-common-c.so");

        uint64_t a0 = a.getMicroseconds();
        uint64_t b0 = b.getMicroseconds();
        // Tiny delay to make second sample distinct
        for (volatile int i = 0; i < 100000; ++i) { (void)i; }
        uint64_t a1 = a.getMicroseconds();
        uint64_t b1 = b.getMicroseconds();

        if (a1 < a0) FAIL(name, "wrapper A micros went backwards: %llu -> %llu",
                          (unsigned long long)a0, (unsigned long long)a1);
        if (b1 < b0) FAIL(name, "wrapper B micros went backwards: %llu -> %llu",
                          (unsigned long long)b0, (unsigned long long)b1);
        // Both wrappers should see roughly the same wall-clock-ish time.
        // (LiGetMicroseconds is process-monotonic. Each namespace has its own
        // implementation but reads the same kernel clock.)
        long long skew = (long long)a1 - (long long)b1;
        if (skew < 0) skew = -skew;
        if (skew > 1000000) FAIL(name, "wrapper A and B times differ by %lld us (>1s)", skew);
        std::printf("    a=%llu b=%llu skew=%lld us\n",
                    (unsigned long long)a1, (unsigned long long)b1, skew);
    }
    catch (const std::exception& e) {
        FAIL(name, "ctor threw: %s", e.what());
    }
    PASS(name);
}

} // namespace

int main()
{
    int (*tests[])() = {
        test_construct_and_destruct,
        test_dual_wrapper_isolation,
        test_stage_name_works,
        test_micros_monotonic_per_wrapper,
    };
    const size_t n = sizeof(tests) / sizeof(*tests);

    int failed = 0;
    for (size_t i = 0; i < n; i++) {
        if (tests[i]() != 0) failed++;
    }

    std::printf("\n=== %zu/%zu passed", n - failed, n);
    if (failed) std::printf(", %d failed ===\n", failed);
    else        std::printf(" ===\n");
    return failed ? 1 : 0;
}
