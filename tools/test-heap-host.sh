#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
CC=${HOST_CC:-cc}
OUT="$ROOT/build/host-heap-test"
mkdir -p "$ROOT/build"
"$CC" -DNYR_HOST_TEST -std=c11 -Wall -Wextra -Werror -O2 \
    "$ROOT/tests/host/heap_test.c" "$ROOT/kernel/heap.c" "$ROOT/kernel/lock.c" -o "$OUT"
"$OUT"
