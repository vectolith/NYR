#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ISO="$ROOT/build/nyr-uefi.iso"
OVMF_CODE="${OVMF_CODE:-/usr/share/OVMF/OVMF_CODE_4M.fd}"
NYR_CPUS="${NYR_CPUS:-4}"
[[ "$NYR_CPUS" =~ ^[1-9][0-9]*$ ]] || { echo "NYR_CPUS must be a positive integer" >&2; exit 2; }
[[ -f "$ISO" ]] || { echo "Missing $ISO; run make iso" >&2; exit 1; }
if [[ ! -f "$OVMF_CODE" ]]; then
  OVMF_CODE="$(find /usr/share -type f -name 'OVMF_CODE*' -print -quit)"
fi
[[ -n "$OVMF_CODE" && -f "$OVMF_CODE" ]] || { echo "OVMF firmware not found" >&2; exit 1; }
LOG="$ROOT/build/qemu-boot-serial.log"
rm -f "$LOG"
set +e
timeout 15s qemu-system-x86_64 -machine q35 -accel tcg,thread=multi -smp "$NYR_CPUS" -m 512M -cpu qemu64 -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" -cdrom "$ISO" -boot d -display none -serial "file:$LOG" -monitor none -no-reboot
STATUS=$?
set -e
cat "$LOG" || true
for marker in "NYR: kernel entered" "NYR: SMP topology discovered CPUs $NYR_CPUS" "NYR: BSP GDT, TSS, IDT and breakpoint self-test passed" "NYR: framebuffer initialized" "NYR: PMM self-test passed" "NYR: virtual memory mapping, ownership, and TLB self-tests passed" "NYR: local APIC initialized; BSP NMI self-test passed" "NYR: SMP application processors online $NYR_CPUS" "NYR: NMI IPIs acknowledged by all APs" "NYR: SMP CPUs parked with interrupts disabled; scheduler not enabled" "NYR: boot milestone reached; idle halt loop"; do
  grep -Fq "$marker" "$LOG" || {
    echo "Missing boot marker: $marker (QEMU status $STATUS)" >&2
    exit 1
  }
done
echo "UEFI boot, CPU topology, PMM, and VMM smoke tests passed with $NYR_CPUS QEMU vCPUs"
