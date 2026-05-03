# `tests/mlc-wrapper`

TDD scaffold for the Phase 1 `MlcWrapper` (the C++ class that will own the
`dlmopen` handle and the `dlsym` table for the `Li*` API exposed by
`moonlight-common-c`).

This directory exists so the architectural assumptions behind option **D** (single
process with two `dlmopen(LM_ID_NEWLM, ...)` namespaces of the protocol library)
are pinned by executable tests **before** any production wrapper code is written.
The tests use the real `moonlight-common-c` sources from the project's submodule
and never modify the upstream library.

## What it does

`make test` rebuilds `libmoonlight-common-c.so` from the project's submodule
sources via direct `gcc -shared -fPIC` (no changes to
`moonlight-common-c.pro`), then builds and runs `test_dlmopen` which contains
four sub-tests:

| Test | Asserts |
|------|---------|
| `basic_load_and_init` | The `.so` loads under `dlmopen(LM_ID_NEWLM)`, `LiInitializeStreamConfiguration` resolves and zeroes its argument |
| `dual_namespace_globals` | All extern globals exposed by the `.so` (subset listed in `KNOWN_GLOBALS[]`) have distinct addresses across two namespaces |
| `dual_namespace_writes` | Writing to `RtspPortNumber` in ns1 does not bleed into ns2 |
| `openssl_libs_isolated` | `/proc/self/maps` shows `libssl`, `libcrypto`, and `libmoonlight-common-c` each mapped twice (one independent copy per namespace) |

Exit code is `0` only when all four tests pass.

## Run

```
cd tests/mlc-wrapper
make test
```

Expected output ends with `=== 4/4 passed ===`.

## Cleanup

```
make clean
```

Build artefacts (`libmoonlight-common-c.so`, `test_dlmopen`) are gitignored.

## When to update

- Upstream `moonlight-common-c` adds or removes a `.c` file → update `MLC_SOURCES`
  in `Makefile` to mirror `moonlight-common-c.pro`.
- Upstream adds an extern global that two sessions would race on → add it to
  `KNOWN_GLOBALS[]` in `test_dlmopen.c`.
- A new architectural assumption needs pinning → add a new `test_*` function and
  list it in `main`'s array.

## Why an out-of-tree `.so` build

Phase 1 will eventually patch `moonlight-common-c.pro` itself to produce the
shared library directly inside the qmake build, but until `MlcWrapper` is solid
enough that the rest of the app can switch off the static link, building the
`.so` here keeps the existing app build unchanged and isolates the experiment.
The integration into the project build is a separate task with its own commit.

## Reference

`docs/spikes/dlmopen-feasibility.md` documents the exploratory work that
inspired these tests; this directory is the test-first version of the same
checks plus the OpenSSL/library isolation assertion, set up to run repeatedly
as the implementation evolves.
