#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO="$ROOT/build/nyr-uefi.iso"
OVMF_CODE="${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}"
[[ -f "$ISO" ]] || { echo "Missing $ISO; run make iso" >&2; exit 1; }
if [[ ! -f "$OVMF_CODE" ]]; then
  OVMF_CODE="$(find /usr/share -type f \( -name 'OVMF_CODE.fd' -o -name 'OVMF_CODE_4M.fd' \) -print -quit)"
fi
[[ -n "$OVMF_CODE" && -f "$OVMF_CODE" ]] || { echo "OVMF firmware not found" >&2; exit 1; }
LOG="$ROOT/build/qemu-boot-serial.log"
rm -f "$LOG"
set +e
timeout 15s qemu-system-x86_64 -machine q35 -m 512M -cpu qemu64 \
  -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
  -cdrom "$ISO" -boot d -display none -serial "file:$LOG" -monitor none -no-reboot
STATUS=$?
set -e
cat "$LOG" || true
grep -q "NYR: kernel entered" "$LOG" || { echo "Kernel entry marker missing (QEMU status $STATUS)" >&2; exit 1; }
grep -q "NYR: framebuffer initialized" "$LOG" || { echo "Framebuffer marker missing" >&2; exit 1; }
echo "UEFI boot smoke test passed"
