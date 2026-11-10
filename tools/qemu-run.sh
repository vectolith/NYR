#!/bin/sh
# Boot a UEFI disk image in QEMU with phone-browser-friendly serial output.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
. "$ROOT/tools/env.sh"
QEMU=${QEMU:-qemu-system-x86_64}
IMAGE=${NYR_DISK_IMAGE:-"$ROOT/build/nyr-uefi.img"}
[ -f "$IMAGE" ] || {
    echo "No bootable NYR disk image found: $IMAGE" >&2
    echo "A UEFI loader and disk-image target must be added before a real kernel boot is possible." >&2
    echo "When available, run: NYR_DISK_IMAGE=build/nyr-uefi.img make qemu-run" >&2
    exit 2
}
command -v "$QEMU" >/dev/null 2>&1 || { echo "QEMU not found: $QEMU" >&2; exit 1; }

CODE=${OVMF_CODE:-}
VARS_TEMPLATE=${OVMF_VARS_TEMPLATE:-}
for candidate in /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/ovmf/OVMF_CODE.fd /usr/share/qemu/OVMF_CODE.fd; do
    if [ -z "$CODE" ] && [ -f "$candidate" ]; then CODE=$candidate; fi
done
for candidate in /usr/share/OVMF/OVMF_VARS_4M.fd /usr/share/OVMF/OVMF_VARS.fd /usr/share/ovmf/OVMF_VARS.fd /usr/share/qemu/OVMF_VARS.fd; do
    if [ -z "$VARS_TEMPLATE" ] && [ -f "$candidate" ]; then VARS_TEMPLATE=$candidate; fi
done
[ -n "$CODE" ] && [ -f "$CODE" ] || { echo "OVMF_CODE firmware image not found." >&2; exit 1; }
[ -n "$VARS_TEMPLATE" ] && [ -f "$VARS_TEMPLATE" ] || { echo "OVMF_VARS template not found." >&2; exit 1; }

VARS="$ROOT/build/OVMF_VARS.fd"
cp "$VARS_TEMPLATE" "$VARS"
SERIAL_MODE=${NYR_QEMU_SERIAL:-stdio}
case "$SERIAL_MODE" in
  stdio)
    echo "Starting NYR in headless QEMU. Serial console is attached to this terminal."
    exec "$QEMU" -machine q35 -m 512M -cpu qemu64 -drive "if=pflash,format=raw,readonly=on,file=$CODE" -drive "if=pflash,format=raw,file=$VARS" -drive "format=raw,file=$IMAGE" -display none -monitor none -serial stdio -no-reboot
    ;;
  file)
    LOG="$ROOT/build/qemu-serial.log"
    echo "Starting headless QEMU; serial output: $LOG"
    exec "$QEMU" -machine q35 -m 512M -cpu qemu64 -drive "if=pflash,format=raw,readonly=on,file=$CODE" -drive "if=pflash,format=raw,file=$VARS" -drive "format=raw,file=$IMAGE" -display none -monitor none -serial "file:$LOG" -no-reboot
    ;;
  *)
    echo "NYR_QEMU_SERIAL must be 'stdio' or 'file'." >&2
    exit 2
    ;;
esac
