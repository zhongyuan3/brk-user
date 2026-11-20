#!/bin/bash
# Host-side validation of the libc floating-point stack: the vendored
# musl math code, the strtod scanner and the printf float engine.
#
# The three engines are pure IEEE-754 arithmetic with no OS dependencies,
# so their behavior on the host matches the riscv64 build.  Each engine is
# compiled straight from the target sources, renamed where needed to keep
# the glibc symbols callable, and compared against glibc: math within
# 2 ULP, strtod for value, endptr and errno agreement, fmt_fp
# byte-for-byte (with %a validated by round-trip through strtod).
#
# usage: tests/host/run.sh
#
# Requires the kernel UAPI headers under BUILD_ROOT/include; the
# host-tests make target stages them first.

set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
USER_ROOT=$(cd "$SCRIPT_DIR/../.." && pwd)
SRC_INCLUDE="$USER_ROOT/usr/include"
BUILD_ROOT=${BUILD_ROOT:-"$USER_ROOT/build"}
UAPI_INCLUDE="$BUILD_ROOT/include"
MATH_DIR="$USER_ROOT/usr/lib/libc/math"
STDIO_DIR="$USER_ROOT/usr/lib/libc/stdio"
CC=${HOSTCC:-cc}

if [[ ! -d $UAPI_INCLUDE/uapi ]]; then
	echo "${0##*/}: missing $UAPI_INCLUDE/uapi" >&2
	echo "Run 'make host-tests' (or 'make all') to stage them." >&2
	exit 1
fi

OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

echo "== math: vendored musl libm vs glibc, 2 ULP =="

# Each function is renamed brk_<name> so it can be linked against glibc
# libm and compared directly.
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
	$CC -O2 -Wall -nostdinc -I"$SRC_INCLUDE" -I"$UAPI_INCLUDE" \
		-I"$MATH_DIR" $RENAME -c "$src" -o "$OUT/math-$base.o"
	OBJS+=("$OUT/math-$base.o")
done

$CC -O2 -Wall -c "$SCRIPT_DIR/math_host_test.c" -o "$OUT/math_host_test.o"
$CC -o "$OUT/math_host_test" "$OUT/math_host_test.o" "${OBJS[@]}" -lm
"$OUT/math_host_test"

echo "== strtod: vendored scanner vs glibc =="

# Rename strtod to brk_strtod so both implementations can coexist.
$CC -O2 -Wall -nostdinc -I"$SRC_INCLUDE" -I"$UAPI_INCLUDE" \
	-Dstrtod=brk_strtod -c "$USER_ROOT/usr/lib/libc/stdlib/strtod.c" \
	-o "$OUT/strtod-engine.o"
$CC -O2 -Wall -c "$SCRIPT_DIR/strtod_host_test.c" -o "$OUT/strtod_host_test.o"
$CC -o "$OUT/strtod_host_test" "$OUT/strtod_host_test.o" \
	"$OUT/strtod-engine.o" -lm
"$OUT/strtod_host_test"

echo "== fmt_fp: printf %e/%f/%g/%a vs glibc =="

# The engine calls back into __printf_out/__printf_pad, which the test
# replaces with a memory sink, so no stdio plumbing is needed.
$CC -O2 -Wall -nostdinc -I"$SRC_INCLUDE" -I"$UAPI_INCLUDE" \
	-I"$STDIO_DIR" -c "$STDIO_DIR/fmt_fp.c" -o "$OUT/fmt_fp-engine.o"
$CC -O2 -Wall -c "$SCRIPT_DIR/fmt_fp_host_test.c" -o "$OUT/fmt_fp_host_test.o"
$CC -o "$OUT/fmt_fp_host_test" "$OUT/fmt_fp_host_test.o" \
	"$OUT/fmt_fp-engine.o" -lm
"$OUT/fmt_fp_host_test"
