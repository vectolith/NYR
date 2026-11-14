#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
# shellcheck disable=SC1091
source "$ROOT/toolchain.lock"

WORK="$ROOT/build/limine"
UNPACKED="$WORK/unpacked"
LIMINE_DIR="$UNPACKED/limine-binary"
ISO_ROOT="$ROOT/build/iso-root"
ARCHIVE="limine-binary.tar.gz"

mkdir -p "$WORK"
if [[ ! -f "$WORK/$ARCHIVE" ]]; then
    curl --fail --location --retry 3 --retry-delay 2 \
        "https://github.com/Limine-Bootloader/Limine/releases/download/v${LIMINE_VERSION}/$ARCHIVE" \
        --output "$WORK/$ARCHIVE"
fi

echo "$LIMINE_BINARY_SHA256  $WORK/$ARCHIVE" | sha256sum --check --status || {
    echo "Limine v${LIMINE_VERSION} binary archive SHA-256 mismatch" >&2
    exit 1
}

rm -rf "$UNPACKED"
mkdir -p "$UNPACKED"
tar --extract --gzip --file "$WORK/$ARCHIVE" --directory "$UNPACKED"

for component in limine.c limine-bios.sys limine-bios-cd.bin limine-uefi-cd.bin BOOTX64.EFI Makefile; do
    if [[ ! -f "$LIMINE_DIR/$component" ]]; then
        echo "Expected Limine v${LIMINE_VERSION} component missing: $LIMINE_DIR/$component" >&2
        echo "Archive entries:" >&2
        tar --list --gzip --file "$WORK/$ARCHIVE" | sed -n '1,100p' >&2
        exit 1
    fi
done

make --directory "$LIMINE_DIR" CC="${HOST_CC:-cc}" CFLAGS="${HOST_CFLAGS:--O2 -g}"
[[ -x "$LIMINE_DIR/limine" ]] || {
    echo "Limine host utility was not built" >&2
    exit 1
}

rm -rf "$ISO_ROOT"
mkdir -p "$ISO_ROOT/boot/limine" "$ISO_ROOT/EFI/BOOT"
install -m 0644 "$ROOT/build/nyr-kernel.elf" "$ISO_ROOT/boot/nyr-kernel.elf"
install -m 0644 "$ROOT/limine.conf" "$ISO_ROOT/limine.conf"
install -m 0644 "$LIMINE_DIR/limine-bios.sys" "$ISO_ROOT/boot/limine/limine-bios.sys"
install -m 0644 "$LIMINE_DIR/limine-bios-cd.bin" "$ISO_ROOT/boot/limine/limine-bios-cd.bin"
install -m 0644 "$LIMINE_DIR/limine-uefi-cd.bin" "$ISO_ROOT/boot/limine/limine-uefi-cd.bin"
install -m 0644 "$LIMINE_DIR/BOOTX64.EFI" "$ISO_ROOT/EFI/BOOT/BOOTX64.EFI"

xorriso -as mkisofs -R -r -J \
    -b boot/limine/limine-bios-cd.bin \
    -no-emul-boot -boot-load-size 4 -boot-info-table \
    -hfsplus -apm-block-size 2048 \
    --efi-boot boot/limine/limine-uefi-cd.bin \
    -efi-boot-part --efi-boot-image --protective-msdos-label \
    "$ISO_ROOT" -o "$ROOT/build/nyr-uefi.iso"

"$LIMINE_DIR/limine" bios-install "$ROOT/build/nyr-uefi.iso"
echo "Created $ROOT/build/nyr-uefi.iso"
