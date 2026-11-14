#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
. "$ROOT/tools/env.sh"

printf 'NYR development environment preflight\n'
printf '%s\n' '===================================='

fail=0
for cmd in cc ld make; do
    if command -v "$cmd" >/dev/null 2>&1; then
        printf '[required: OK] %s -> %s\n' "$cmd" "$(command -v "$cmd")"
    else
        printf '[required: MISSING] %s\n' "$cmd"
        fail=1
    fi
done

printf '\nDevelopment and emulator tools:\n'
for cmd in clang ld.lld nasm qemu-system-x86_64 qemu-img gdb mformat mcopy mkfs.fat xorriso rustc cargo python3 git cmake ninja pkg-config curl wget timeout readelf objdump; do
    if command -v "$cmd" >/dev/null 2>&1; then
        printf '[available] %s\n' "$cmd"
    else
        printf '[optional/missing] %s\n' "$cmd"
    fi
done

for candidate in /usr/share/OVMF/OVMF_CODE_4M.fd /usr/share/OVMF/OVMF_CODE.fd /usr/share/ovmf/OVMF_CODE.fd /usr/share/qemu/OVMF_CODE.fd; do
    if [ -f "$candidate" ]; then OVMF_CODE=$candidate; break; fi
done
if [ -n "${OVMF_CODE:-}" ] && [ -f "$OVMF_CODE" ]; then
    printf '\n[available] OVMF firmware: %s\n' "$OVMF_CODE"
else
    printf '\n[optional/missing] OVMF firmware (required for UEFI QEMU runs)\n'
fi

if [ "$fail" -eq 0 ]; then
    printf '\nPASS: current kernel build requirements are available.\n'
else
    printf '\nFAIL: one or more required build tools are missing.\n'
fi
exit "$fail"
