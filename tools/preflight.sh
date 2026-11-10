#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
. "$ROOT/tools/env.sh"

printf 'NYR development environment preflight\n'
printf '%s\n' '===================================='

fail=0
for cmd in cc ld make; do
    if command -v "$cmd" >/dev/null 2>&1; then
        printf '[required: OK] %s\n' "$cmd"
    else
        printf '[required: MISSING] %s\n' "$cmd"
        fail=1
    fi
done

printf '\nOptional tools (not required for the current kernel build):\n'
for cmd in clang ld.lld nasm qemu-system-x86_64 qemu-img gdb mformat mcopy mkfs.fat xorriso rustc cargo python3 git cmake ninja pkg-config curl wget; do
    if command -v "$cmd" >/dev/null 2>&1; then
        printf '[available] %s\n' "$cmd"
    else
        printf '[optional/missing] %s\n' "$cmd"
    fi
done

if [ -n "${OVMF_CODE:-}" ] && [ -f "$OVMF_CODE" ]; then
    printf '\n[available] UEFI firmware: %s\n' "$OVMF_CODE"
else
    printf '\n[optional/missing] OVMF UEFI firmware (needed for future UEFI boot tests)\n'
fi

if [ "$fail" -eq 0 ]; then
    printf '\nPASS: required tools for the current kernel build are present.\n'
else
    printf '\nFAIL: install the missing required build tools.\n'
fi
exit "$fail"
