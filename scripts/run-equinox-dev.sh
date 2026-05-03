#!/usr/bin/env bash
#
# Dev launcher for Equinox.
#
# Builds the shared libmoonlight-common-c.so from the project's submodule sources
# (under tests/mlc-wrapper) and launches app/moonlight with LD_LIBRARY_PATH set so
# MlcWrapper::load can resolve "libmoonlight-common-c.so" via dlmopen.
#
# Use this during development. Phase 3 packaging will install the .so at a system
# path and remove the LD_LIBRARY_PATH dance.
#
# Usage:
#   scripts/run-equinox-dev.sh [moonlight args...]

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SO_DIR="$REPO/tests/mlc-wrapper"
BIN="$REPO/app/moonlight"

# Ensure the .so is built (Makefile is incremental; no-op if up to date).
make -s -C "$SO_DIR" libmoonlight-common-c.so

if [[ ! -x "$BIN" ]]; then
    echo "$BIN missing or not executable. Run 'qmake6 moonlight-qt.pro && make -j\$(nproc) release' first." >&2
    exit 1
fi

# OPENSSL_CONF=/dev/null tells the libcrypto loaded inside the dlmopen
# namespace to skip its config-file init, which is the path that segfaults
# inside ERR_set_mark when two libcrypto instances co-exist in the same
# process. See docs/phase-1/progress.md for the diagnosis.
exec env LD_LIBRARY_PATH="$SO_DIR${LD_LIBRARY_PATH+:$LD_LIBRARY_PATH}" \
         OPENSSL_CONF=/dev/null \
         "$BIN" "$@"
