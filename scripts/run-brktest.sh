#!/bin/sh
# Boot BRK in QEMU, run brktest, and report whether the run passed.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
USER_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
KERNEL_ROOT=${KERNEL_ROOT:-"$USER_ROOT/../brk"}
CROSS_COMPILE=${CROSS_COMPILE:-riscv64-unknown-elf-}
BOOT_DELAY=${BOOT_DELAY:-6}
RUN_TIME=${RUN_TIME:-120}
TARGET=${1:-all}

if [ "$#" -gt 0 ]; then
	shift
fi

LOG=$(mktemp "${TMPDIR:-/tmp}/brktest.XXXXXX")
trap 'rm -f "$LOG"' EXIT INT TERM

cd "$KERNEL_ROOT"
set +e
{
	sleep "$BOOT_DELAY"
	printf '/usr/bin/brktest %s' "$TARGET"
	for arg in "$@"; do
		printf ' %s' "$arg"
	done
	printf '\n'
	sleep "$RUN_TIME"
	printf 'exit\n'
	sleep 5
} | timeout $((BOOT_DELAY + RUN_TIME + 20)) \
	make run CROSS_COMPILE="$CROSS_COMPILE" 2>&1 | tee "$LOG"
set -e

if grep -q '^not ok ' "$LOG"; then
	echo "brktest: TAP failure detected" >&2
	exit 1
fi
if grep -qE '^[0-9]+ suite\(s\), 0 failed' "$LOG"; then
	exit 0
fi
echo "brktest: no successful result found in QEMU output" >&2
exit 1
