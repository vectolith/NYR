#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
CC=${HOST_CC:-cc}
OUT="$ROOT/build/host-lock-test"
mkdir -p "$ROOT/build"
"$CC" -DNYR_HOST_TEST -std=c11 -Wall -Wextra -Werror -O2 -pthread \
    "$ROOT/tests/host/lock_test.c" "$ROOT/kernel/lock.c" -o "$OUT"
"$OUT"
