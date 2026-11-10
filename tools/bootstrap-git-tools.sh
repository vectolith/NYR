#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
PREFIX=${NYR_TOOLCHAIN_DIR:-"$ROOT/.toolchain"}
SRC="$PREFIX/src"
mkdir -p "$SRC" "$PREFIX/bin"

clone_or_update() {
  name=$1; url=$2
  dir="$SRC/$name"
  if [ -d "$dir/.git" ]; then
    echo "[update] $name"
    git -C "$dir" fetch --tags --prune
    git -C "$dir" pull --ff-only || true
  else
    echo "[clone]  $name"
    git clone --filter=blob:none "$url" "$dir"
  fi
}

# Source repositories for NYR's missing development stack.
clone_or_update llvm-project https://github.com/llvm/llvm-project.git
clone_or_update nasm https://github.com/netwide-assembler/nasm.git
clone_or_update qemu https://github.com/qemu/qemu.git
clone_or_update ekd2 https://github.com/tianocore/edk2.git
clone_or_update binutils-gdb https://sourceware.org/git/binutils-gdb.git
mclone_or_update mtools https://github.com/Distrotech/mtools.git
clone_or_update dosfstools https://github.com/dosfstools/dosfstools.git
clone_or_update libisoburn https://dev.lovelyhq.com/libburnia/libisoburn.git
clone_or_update cmake https://github.com/Kitware/CMake.git
clone_or_update ninja https://github.com/ninja-build/ninja.git
clone_or_update rust https://github.com/rust-lang/rust.git

echo "Sources cloned/updated under $SRC"V6†ò%'VâÖ¶R&VfÆ–v‡BFò6VRv†–6‚W†V7WF&ÆRFööÇ2&R7GVÆÇ’W6&ÆRâ 