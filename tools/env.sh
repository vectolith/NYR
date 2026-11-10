#!/bin/sh
# Source this file to activate host-mounted NYR tools.
NYR_ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
NYR_TOOLS_MOUNT=${NYR_TOOLS_MOUNT:-/mnt/tools}

paths=''
add_bin() { if [ -d "$1" ]; then paths="${paths:+$paths:}$1"; fi; return 0; }
add_bin "$NYR_TOOLS_MOUNT/bin"
for d in "$NYR_TOOLS_MOUNT"/*/bin; do [ -d "$d" ] && add_bin "$d"; done
add_bin "$NYR_ROOT/.toolchain/bin"

if [ -n "$paths" ]; then PATH="$paths:$PATH"; fi
export PATH NYR_TOOLS_MOUNT
export OVMF_CODE=${OVMF_CODE:-$(find "$NYR_TOOLS_MOUNT" -type f -iname 'OVMF_CODE*.fd' 2>/dev/null | sed -n '1p' || true)}
export OVMF_VARS_TEMPLATE=${OVMF_VARS_TEMPLATE:-$(find "$NYR_TOOLS_MOUNT" -type f -iname 'OVMF_VARS*.fd' 2>/dev/null | sed -n '1p' || true)}
