#!/usr/bin/env bash
#
# Build a static OpenSSL (libcrypto.a, libssl.a) for embedding into
# libmoonlight-common-c.so. Each dlmopen LM_ID_NEWLM load of the .so will
# carry its own self-contained OpenSSL state, side-stepping the
# OPENSSL_init_crypto SIGSEGV that the system shared libcrypto produces
# when loaded into a second ELF namespace (see docs/phase-1/progress.md
# section 11).
#
# Output:
#   tests/mlc-wrapper/openssl-static/lib/libcrypto.a
#   tests/mlc-wrapper/openssl-static/lib/libssl.a
#   tests/mlc-wrapper/openssl-static/include/...
#
# This whole tree is .gitignored. Re-run when the OpenSSL version in
# OPENSSL_VERSION below changes.

set -euo pipefail

OPENSSL_VERSION="${OPENSSL_VERSION:-3.6.2}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PREFIX="$SCRIPT_DIR/openssl-static"
SRC_DIR="$SCRIPT_DIR/.openssl-src"
TARBALL="openssl-${OPENSSL_VERSION}.tar.gz"
URL="https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/${TARBALL}"

# Skip if already built and up-to-date.
if [[ -f "$PREFIX/lib64/libcrypto.a" && -f "$PREFIX/lib64/libssl.a" ]]; then
    BUILT_VER="$(grep -h "^OpenSSL " "$PREFIX/share/doc/openssl"/*/CHANGES.md 2>/dev/null | head -1 | awk '{print $2}')"
    if [[ -z "$BUILT_VER" || "$BUILT_VER" == "${OPENSSL_VERSION}" ]]; then
        echo "==> $PREFIX already has libcrypto.a + libssl.a (version $BUILT_VER); skipping rebuild"
        echo "    rm -rf '$PREFIX' '$SRC_DIR' to force rebuild"
        exit 0
    fi
    echo "==> $PREFIX has OpenSSL $BUILT_VER, expected $OPENSSL_VERSION; rebuilding"
fi

mkdir -p "$SRC_DIR"
cd "$SRC_DIR"

if [[ ! -f "$TARBALL" ]]; then
    echo "==> Downloading $URL"
    if command -v curl >/dev/null; then
        curl -fLO "$URL"
    elif command -v wget >/dev/null; then
        wget -q "$URL"
    else
        echo "Neither curl nor wget available; install one and retry" >&2
        exit 1
    fi
fi

if [[ ! -d "openssl-${OPENSSL_VERSION}" ]]; then
    echo "==> Extracting $TARBALL"
    tar -xzf "$TARBALL"
fi

cd "openssl-${OPENSSL_VERSION}"

echo "==> Configuring (no-shared, -fPIC, into $PREFIX)"
# - no-shared      : do not produce libcrypto.so / libssl.so, only .a
# - no-tests       : skip building the test suite (saves time)
# - no-docs        : skip docs (saves time)
# - no-engine      : we do not need OpenSSL engine support; smaller .a
# - no-deprecated  : avoid deprecated APIs that may pull extra code
# - -fPIC          : the .a must be position-independent so it can be
#                    relinked into a shared object (libmoonlight-common-c.so)
./Configure linux-x86_64 \
    no-shared no-tests no-docs no-engine no-deprecated \
    --prefix="$PREFIX" \
    --openssldir="$PREFIX/ssl" \
    -fPIC

echo "==> Building (this takes a few minutes)"
make -j"$(nproc)" build_libs

echo "==> Installing only the static libs + headers"
make install_dev

echo
echo "==> Done. Artifacts:"
ls -la "$PREFIX/lib"/libcrypto.a "$PREFIX/lib"/libssl.a 2>&1
echo "    Headers in $PREFIX/include/openssl/"
echo
echo "    Combined size: $(du -ch "$PREFIX/lib"/lib{ssl,crypto}.a | tail -1 | awk '{print $1}')"
