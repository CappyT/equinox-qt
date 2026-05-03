#!/usr/bin/env bash
#
# Repro the dlmopen + dual-mlc crash under gdb and capture every artefact
# Claude needs to read offline. Output lands in /tmp/equinox-debug/.
#
# Prerequisites you have to satisfy yourself before running this:
#   1. gdb installed              (sudo dnf install gdb)
#   2. sunshine running in your GUI session
#   3. Moonlight client paired with the local sunshine (already done)
#
# Usage:
#   ./scripts/debug-equinox-crash.sh
#
# The script will print a prompt then wait for Enter; once you confirm,
# moonlight launches under gdb. Click Desktop on the host tile to trigger
# the streaming start path -> the crash. gdb dumps full state, then exits.

set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR=/tmp/equinox-debug
mkdir -p "$LOG_DIR"

GDB_LOG="$LOG_DIR/gdb.log"
STDOUT_LOG="$LOG_DIR/moonlight-stdout.log"
STDERR_LOG="$LOG_DIR/moonlight-stderr.log"
INFO_LOG="$LOG_DIR/env-info.log"
CORE_FILE="$LOG_DIR/core"

rm -f "$GDB_LOG" "$STDOUT_LOG" "$STDERR_LOG" "$INFO_LOG" "$CORE_FILE"

if ! command -v gdb >/dev/null; then
    echo "gdb is not installed. Run:  sudo dnf install gdb" >&2
    exit 1
fi

echo "==> Rebuilding tests/mlc-wrapper/libmoonlight-common-c.so with -g (debug symbols)"
make -C "$REPO/tests/mlc-wrapper" clean >/dev/null
make -C "$REPO/tests/mlc-wrapper" libmoonlight-common-c.so 2>&1 | tail -3

echo "==> Capturing environment snapshot to $INFO_LOG"
{
    echo "===== Environment ====="
    date
    uname -a
    echo "DISPLAY=${DISPLAY:-}"
    echo "WAYLAND_DISPLAY=${WAYLAND_DISPLAY:-}"
    echo "XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-}"
    echo "PWD=$PWD"
    echo
    echo "===== Moonlight binary ====="
    file "$REPO/app/moonlight"
    ls -la "$REPO/app/moonlight"
    echo "Has debug info: $(readelf -S "$REPO/app/moonlight" 2>/dev/null | grep -c '\.debug_info') section(s)"
    echo
    echo "===== libmoonlight-common-c.so (dlmopen target) ====="
    file "$REPO/tests/mlc-wrapper/libmoonlight-common-c.so"
    ls -la "$REPO/tests/mlc-wrapper/libmoonlight-common-c.so"
    echo "Has debug info: $(readelf -S "$REPO/tests/mlc-wrapper/libmoonlight-common-c.so" 2>/dev/null | grep -c '\.debug_info') section(s)"
    echo
    echo "===== gdb ====="
    gdb --version | head -1
    echo
    echo "===== Sunshine running? ====="
    pgrep -af sunshine | grep -v "$0" | head -3 || echo "(none)"
    echo
    echo "===== Static-linked libmoonlight-common-c.a (still in the binary too) ====="
    ls -la "$REPO/moonlight-common-c/libmoonlight-common-c.a" 2>/dev/null || echo "(not present, that is unusual)"
} >> "$INFO_LOG"

# Allow core dumps just in case (gdb usually catches before core is written
# but having the safety net is cheap).
ulimit -c unlimited

cd "$REPO"

cat <<EOF

==> Ready to launch.
   - Sunshine should already be running in your GUI session.
   - When the moonlight window opens, click on the localhost host tile,
     then click Desktop. Wait for the crash.
   - The script will return automatically; you do NOT have to press
     anything inside gdb.
   - All output goes to $LOG_DIR/.

Press Enter to launch...
EOF
read -r

LD_LIBRARY_PATH="$REPO/tests/mlc-wrapper" \
    gdb -batch \
        -x "$REPO/scripts/debug-equinox-crash.gdb" \
        --args "$REPO/app/moonlight" \
        > "$STDOUT_LOG" 2> "$STDERR_LOG" || true

GDB_EXIT=$?

echo
echo "==> Done. Files in $LOG_DIR:"
ls -la "$LOG_DIR"
echo
echo "==> gdb stdout (last 5 lines):"
tail -5 "$STDOUT_LOG"
echo
echo "==> gdb log size: $(wc -l < "$GDB_LOG" 2>/dev/null || echo 0) lines"
echo "==> Exit code: $GDB_EXIT"
echo
echo "Send Claude these paths so he can read them:"
echo "  $GDB_LOG"
echo "  $STDOUT_LOG"
echo "  $STDERR_LOG"
echo "  $INFO_LOG"
