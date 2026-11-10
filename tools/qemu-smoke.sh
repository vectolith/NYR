#!/bin/sh
# Verify that QEMU can start headlessly and create a serial log.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
mkdir -p "$ROOT/build"
LOG="$ROOT/build/qemu-smoke-serial.log"
rm -f "$LOG"
QEMU=${QEMU:-qemu-system-x86_64}
command -v "$QEMU" >/dev/null 2>&1 || { echo "QEMU not found: $QEMU" >&2; exit 1; }
set +e
timeout 3 "$QEMU" -machine none -nodefaults -display none -monitor none -serial "file:$LOG" -S
status=$?
set -e
if [ "$status" -ne 124 ] && [ "$status" -ne 137 ]; then
    echo "QEMU did not remain alive for the smoke-test window (exit $status)." >&2
    exit 1
fi
[ -f "$LOG" ] || { echo "QEMU did not create the serial log." >&2; exit 1; }
echo "QEMU headless startup passed. Serial log: $LOG"
