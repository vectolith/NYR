#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
CC=${HOST_CC:-cc}
OUT="$ROOT/build/host-vmm-test"
mkdir -p "$ROOT/build"
"$CC" -DNYR_HOST_TEST -std=c11 -Wall -Wextra -Werror -O2 \
    "$ROOT/tests/host/vmm_test.c" "$ROOT/kernel/vmm.c" \
    "$ROOT/kernel/memory.c" "$ROOT/kernel/lock.c" -o "$OUT"
"$OUT"
