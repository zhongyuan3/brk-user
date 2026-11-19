#!/bin/bash
# Host-side validation of the vendored musl math code under
# src/libbrkc/math.  The sources are pure IEEE-754 arithmetic with no OS
# dependencies, so their behavior on the host matches the riscv64 build.
# Each function is renamed brk_<name> so it can be linked against glibc
# libm and compared directly.
#
# usage: scripts/math_host_test.sh

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
MATH_DIR="$SCRIPT_DIR/../src/libbrkc/math"
TEST_SRC="$SCRIPT_DIR/math_host_test.c"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

RENAME=""
for f in exp log log2 log10 pow sin cos tan asin acos atan atan2 fmod \
	floor ceil fabs frexp ldexp scalbn modf sqrt copysign \
	__fpclassify __signbit __sin __cos __tan __rem_pio2 \
	__rem_pio2_large __math_xflow __math_uflow __math_oflow \
	__math_divzero __math_invalid; do
	RENAME="$RENAME -D$f=brk_$f"
done

OBJS=()
for src in "$MATH_DIR"/*.c; do
	base=$(basename "$src" .c)
	cc -O2 -Wall -nostdinc -I"$SCRIPT_DIR/../src/include" -I"$MATH_DIR" \
		$RENAME -c "$src" -o "$OUT/$base.o"
	OBJS+=("$OUT/$base.o")
done

cc -O2 -Wall -c "$TEST_SRC" -o "$OUT/host_test.o"
cc -o "$OUT/math_host_test" "$OUT/host_test.o" "${OBJS[@]}" -lm

"$OUT/math_host_test"
