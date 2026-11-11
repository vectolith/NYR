#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
CC=${HOST_CC:-cc}
OUT="$ROOT/build/host-smp-test"
mkdir -p "$ROOT/build"
"$CC" -std=c11 -Wall -Wextra -Werror -O2 \
    "$ROOT/tests/host/smp_test.c" "$ROOT/kernel/smp.c" -o "$OUT"
"$OUT"
