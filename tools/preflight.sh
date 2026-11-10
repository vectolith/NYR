#!/bin/sh
set -u
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
. "$ROOT/tools/env.sh"
printf 'Tool mount: %s\n' "$NYR_TOOLS_MOUNT"
if [ -d "$NYR_TOOLS_MOUNT" ]; then printf '[OK]      mounted tool root    %s\n' "$NYR_TOOLS_MOUNT"; else printf '[MISSING] mounted tool root    %s\n' "$NYR_TOOLS_MOUNT"; fi

# NYR complete host-tool preflight. Missing tools are reported together.
TOOLS='cc|C compiler
gcc|GNU C compiler
g++|GNU C++ compiler
clang|LLVM C compiler
ld|GNU Linker
ld.lld|LLVM linker
make|GNU Make
cmake|CMake
ninja|Ninja
nasm|NASM assembler
llvm-objcopy|LLVM objcopy
llvm-readelf|LLVM readelf
objcopy|GNU objcopy
readelf|GNU readelf
objdump|GNU objdump
nm|GNU nm
addr2line|GNU addr2line
qemu-system-x86_64|QEMU x86-64 emulator
qemu-img|QEMU image utility
gdb|GDB debugger
lldb|LLDB debugger
mformat|Mtools FAT formatter
mcopy|Mtools file copier
mkfs.fat|FAT filesystem formatter
xorriso|ISO image tool
rustc|Rust compiler
cargo|Rust package/build tool
git|Git
python3|Python 3
pkg-config|pkg-config
curl|curl
wget|wget

fail=0
count=0
missing=0
printf '%s\n' 'NYR toolchain preflight'
printf '%s\n' '========================'
printf '%s\n' "$TOOLS" | while IFS='|' read -r cmd label; do
    count=$((count + 1))
    if path=$(command -v "$cmd" 2>/dev/null); then
        version=$($cmd --version 2>&1 | sed -n '1p')
        [ -n "$version" ] || version='version flag not supported'
        printf '[OK]      %-20s %-28s %s\n' "$cmd" "$label" "$version"
    else
        printf '[MISSING] %-20s %s\n' "$cmd" "$label"
    fi
done

# Re-evaluate solely for reliable exit status (pipeline loops may run in subshells).
for cmd in $(printf '%s\n' "$TOOLS" | cut -d'|' -f1); do
    if ! command -v "$cmd" >/dev/null 2>&1; then
        fail=1
        missing=$((missing + 1))
    fi
done

printf '\nFirmware/data checks\n'
printf '%s\n' '--------------------'
ovmf=$(find /usr/share /usr/lib -maxdepth 5 -type f \( -iname 'OVMF*.fd' -o -iname '*OVMF*.bin' \) 2>/dev/null | sed -n '1p')
if [ -n "$ovmf" ]; then
    printf '[OK]      %-20s %s\n' 'OVMF/EDK II' "$ovmf"
else
    printf '[MISSING] %-20s %s\n' 'OVMF/EDK II' 'UEFI firmware image not found'
    fail=1; missing=$((missing + 1))
fi

printf '\nHost capability checks\n'
printf '%s\n' '----------------------'
if command -v tdnf >/dev/null 2>&1; then printf '[OK]      %-20s %s\n' tdnf "$(tdnf --version 2>/dev/null | sed -n '1p')"; else printf '[MISSING] %-20s\n' tdnf; fail=1; missing=$((missing+1)); fi
if sudo -n true >/dev/null 2>&1; then printf '[OK]      %-20s\n' 'root package access'; else printf '[BLOCKED] %-20s %s\n' 'root package access' 'sudo cannot elevate in this sandbox'; fail=1; missing=$((missing+1)); fi

printf '\nResult: '
if [ "$fail" -eq 0 ]; then
    printf 'PASS - NYR development environment is ready.\n'
else
    printf 'FAIL - %s required/planned capability checks are unavailable.\n' "$missing"
fi
exit "$fail"
