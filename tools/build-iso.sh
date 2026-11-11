#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="${LIMINE_VERSION:-12.9.6}"
ARCHIVE="limine-binary.tar.gz"
WORK="$ROOT/build/limine"
ISO_ROOT="$ROOT/build/iso-root"
mkdir -p "$WORK" "$ISO_ROOT/boot" "$ISO_ROOT/EFI/BOOT"
if [[ ! -f "$WORK/$ARCHIVE" ]]; then
  curl -fL "https://github.com/Limine-Bootloader/Limine/releases/download/v${VERSION}/limine-binary.tar.gz" -o "$WORK/$ARCHIVE"
fi
echo "b38aabb042b0c0f9f562b7741be3c1204838d7bbacfa78bb411c674bd5b984ee  $WORK/$ARCHIVE" | sha256sum -c -
mkdir -p "$WORK/unpacked"
tar -xzf "$WORK/$ARCHIVE" -C "$WORK/unpacked"
find_file() { find "$WORK/unpacked" -type f -name "$1" -print -quit; }
BIOS_SYS="$(find_file limine-bios.sys)"
BIOS_CD="$(find_file limine-bios-cd.bin)"
UEFI_CD="$(find_file limine-uefi-cd.bin)"
EFI_APP="$(find_file BOOTX64.EFI)"
HOST_TOOL="$(find "$WORK/unpacked" -type f -name limine -perm -u+x -print -quit)"
for item in "$BIOS_SYS" "$BIOS_CD" "$UEFI_CD" "$EFI_APP" "$HOST_TOOL"; do
  [[ -n "$item" && -f "$item" ]] || { echo "Missing Limine release component: $item" >&2; exit 1; }
done
cp "$ROOT/build/nyr-kernel.elf" "$ISO_ROOT/boot/nyr-kernel.elf"
cp "$ROOT/limine.conf" "$ISO_ROOT/limine.conf"
cp "$BIOS_SYS" "$ISO_ROOT/limine-bios.sys"
cp "$BIOS_CD" "$ISO_ROOT/limine-bios-cd.bin"
cp "$UEFI_CD" "$ISO_ROOT/limine-uefi-cd.bin"
cp "$EFI_APP" "$ISO_ROOT/EFI/BOOT/BOOTX64.EFI"
xorriso -as mkisofs -R -r -J -b limine-bios-cd.bin \
  -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
  --efi-boot limine-uefi-cd.bin -efi-boot-part --efi-boot-image \
  --protective-msdos-label "$ISO_ROOT" -o "$ROOT/build/nyr-uefi.iso"
"$HOST_TOOL" bios-install "$ROOT/build/nyr-uefi.iso"
echo "Created $ROOT/build/nyr-uefi.iso"
