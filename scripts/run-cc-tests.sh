#!/bin/sh
# Boot BRK in QEMU and exercise the on-target cc test sources.
#
# Usage:
#   scripts/run-cc-tests.sh [source.c ...]
#
# With no arguments, every tests/cc/*.c that looks like a standalone test is
# used.  The host queues the commands, the guest reports completion with a
# marker, and only then does the host terminate QEMU.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
USER_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
KERNEL_ROOT=${KERNEL_ROOT:-"$USER_ROOT/../brk"}
CROSS_COMPILE=${CROSS_COMPILE:-riscv64-unknown-elf-}
BOOT_DELAY=${BOOT_DELAY:-8}
RUN_TIME=${RUN_TIME:-600}

cd "$USER_ROOT"

if [ "$#" -gt 0 ]; then
	SOURCES=$*
else
	SOURCES=
	for f in "$USER_ROOT"/tests/cc/*.c; do
		base=${f##*/}
		case "$base" in
		multi_*.c|link_multi_*.c|hosted*.c) continue ;;
		esac
		grep -qE '(^|[^A-Za-z0-9_])main[[:space:]]*\(' "$f" || continue
		SOURCES="$SOURCES tests/cc/$base"
	done
fi

# Build the image before opening the serial pipe.  Otherwise the producer can
# queue console input while the rootfs is still being generated.
make -s -j4 KERNEL_ROOT="$KERNEL_ROOT" CROSS_COMPILE="$CROSS_COMPILE" \
	rootfs kernel >/dev/null

LOG=$(mktemp "${TMPDIR:-/tmp}/cc-tests.XXXXXX")
FIFO=$(mktemp -u "${TMPDIR:-/tmp}/cc-tests-fifo.XXXXXX")
trap 'rm -f "$LOG" "$FIFO"' EXIT INT TERM
mkfifo "$FIFO"

# Start QEMU with the FIFO as stdin.  Keep the output in LOG and echo it as it
# arrives so the caller sees compiler diagnostics immediately.
(make run CROSS_COMPILE="$CROSS_COMPILE" KERNEL_ROOT="$KERNEL_ROOT" \
	<"$FIFO" 2>&1 | tee "$LOG") &
RUN_PID=$!

exec 3>"$FIFO"
sleep "$BOOT_DELAY"
printf '\n' >&3
sleep 1
printf 'echo READY\n' >&3
sleep 1

# A short pause between console lines keeps the guest line editor from
# dropping characters when a whole suite is queued at once.
send()
{
	printf '%s\n' "$1" >&3
	sleep 0.25
}

for src in $SOURCES; do
	base=${src##*/}
	name=${base%.c}
	send "echo === $name"
	send "cc /home/tests/cc/$name.c -o /tmp/$name"
	case "$name" in
	io) send "/tmp/$name hello" ;;
	*) send "/tmp/$name" ;;
	esac
done
send 'echo === multi'
send 'cc /home/tests/cc/multi_a.c /home/tests/cc/multi_b.c -o /tmp/multi'
send '/tmp/multi'
send 'cc -c /home/tests/cc/multi_a.c -o /tmp/a.o'
send 'cc -c /home/tests/cc/multi_b.c -o /tmp/b.o'
send 'cc /tmp/a.o /tmp/b.o -o /tmp/multi_obj'
send '/tmp/multi_obj'
send 'echo === link_multi'
send 'cc /home/tests/cc/link_multi_a.c /home/tests/cc/link_multi_b.c -o /tmp/link_multi'
send '/tmp/link_multi'
send 'echo === modes'
send 'cc -S /home/tests/cc/hello.c -o /tmp/hello.s'
send 'cc -c /home/tests/cc/hello.c -o /tmp/hello.o'
send 'echo === hosted'
send 'cc /home/tests/cc/hosted.c -lc -o /tmp/hosted'
send '/tmp/hosted'
send 'cc /home/tests/cc/hosted_libc.c -lc -o /tmp/hosted_libc'
send '/tmp/hosted_libc'
send 'echo __CC_SUITE_DONE__'

DEADLINE=$(( $(date +%s) + RUN_TIME ))
while ! grep -q '^__CC_SUITE_DONE__' "$LOG" 2>/dev/null; do
	if ! kill -0 "$RUN_PID" 2>/dev/null; then
		break
	fi
	if [ "$(date +%s)" -ge "$DEADLINE" ]; then
		echo "cc-tests: timed out waiting for the guest" >&2
		break
	fi
	sleep 1
done

# Ctrl-A x is the nographic monitor escape that terminates QEMU once the
# guest has actually finished the queued work.
printf '\001x' >&3 2>/dev/null || true
exec 3>&-
wait "$RUN_PID" 2>/dev/null || true

if ! grep -q '^__CC_SUITE_DONE__' "$LOG"; then
	echo "cc-tests: guest did not report completion" >&2
	tail -20 "$LOG" >&2 || true
	exit 1
fi
if grep -q '^cc: ' "$LOG"; then
	echo "cc-tests: compiler diagnostic detected" >&2
	exit 1
fi
if grep -qE 'Segmentation fault|Illegal instruction|Kernel panic|panic:' "$LOG"; then
	echo "cc-tests: guest failure detected" >&2
	exit 1
fi
exit 0
