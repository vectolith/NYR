#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO="${NYR_ISO_IMAGE:-$ROOT/build/nyr-uefi.iso}"
[[ -f "$ISO" ]] || { echo "ISO not found; run make iso first" >&2; exit 2; }
CODE="${OVMF_CODE:-}"
if [[ -z "$CODE" ]]; then
  for f in /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/ovmf/OVMF_CODE.fd; do [[ -f "$f" ]] && CODE="$f" && break; done
fi
[[ -n "$CODE" && -f "$CODE" ]] || { echo "OVMF_CODE firmware not found" >&2; exit 1; }
echo "Starting NYR UEFI ISO in headless QEMU; serial output follows."
exec qemu-system-x86_64 -machine q35 -m 512M -cpu qemu64 -bios "$CODE" -cdrom "$ISO" -boot d -display none -monitor none -serial stdio -no-reboot
