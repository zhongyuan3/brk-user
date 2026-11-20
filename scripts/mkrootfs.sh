#!/bin/bash

set -euo pipefail

usage()
{
	echo "usage: ${0##*/} <output-rootfs.img>" >&2
	exit 1
}

[[ $# -eq 1 ]] || usage
IMG=$1
case $IMG in
/*) ;;
*) IMG="$(pwd)/$IMG" ;;
esac

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
USER_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
BUILD_ROOT=${BUILD_ROOT:-"$USER_ROOT/build"}
case $BUILD_ROOT in
/*) ;;
*) BUILD_ROOT="$USER_ROOT/$BUILD_ROOT" ;;
esac
BIN_DIR="$BUILD_ROOT/bin"
ROOTFS_TREE="$BUILD_ROOT/rootfs-tree"

KERNEL_ROOT=${KERNEL_ROOT:-"$USER_ROOT/../brk"}
TOOLS_ROOT=${TOOLS_ROOT:-"$USER_ROOT/../brk-tools"}
case $KERNEL_ROOT in
/*) ;;
*) KERNEL_ROOT="$USER_ROOT/$KERNEL_ROOT" ;;
esac
case $TOOLS_ROOT in
/*) ;;
*) TOOLS_ROOT="$USER_ROOT/$TOOLS_ROOT" ;;
esac

if [[ ! -d $TOOLS_ROOT ]]; then
	echo "${0##*/}: missing directory: $TOOLS_ROOT" >&2
	echo "Set TOOLS_ROOT if your layout differs." >&2
	exit 1
fi

BRKFS=$TOOLS_ROOT/brkfs
if [[ ! -x $BRKFS ]]; then
	make -C "$TOOLS_ROOT"
fi
[[ -x $BRKFS ]] || {
	echo "${0##*/}: expected host tool: $BRKFS" >&2
	exit 1
}

make -C "$USER_ROOT" KERNEL_ROOT="$KERNEL_ROOT" BUILD_ROOT="$BUILD_ROOT" \
	${CROSS_COMPILE:+CROSS_COMPILE="$CROSS_COMPILE"} \
	${LIBGCC:+LIBGCC="$LIBGCC"} all tests

if [[ ! -x $BIN_DIR/init ]]; then
	echo "${0##*/}: userland binaries not found under $BIN_DIR" >&2
	echo "Build them first: make -C $USER_ROOT all" >&2
	exit 1
fi

BLOCK_SIZE=4096
BLOCK_COUNT=8192
INODE_COUNT=1024

mkdir -p "$(dirname "$IMG")"
rm -f "$IMG"
dd if=/dev/zero of="$IMG" bs=$BLOCK_SIZE count=$BLOCK_COUNT status=none
"$BRKFS" mkfs -bs $BLOCK_SIZE -I $INODE_COUNT "$IMG"

# Build a small FHS-style directory skeleton.  Git cannot track empty
# directories, so create them here and copy them into the image.
rm -rf "$ROOTFS_TREE"
mkdir -p "$ROOTFS_TREE"/{bin,sbin,etc,home,root,tmp,boot,dev,proc,sys}
mkdir -p "$ROOTFS_TREE"/run/lock
mkdir -p "$ROOTFS_TREE"/{mnt,media,opt,srv,lib,lib64}
mkdir -p "$ROOTFS_TREE"/usr/{bin,sbin,lib,include,share}
mkdir -p "$ROOTFS_TREE"/usr/local/{bin,sbin,lib,include,share}
mkdir -p "$ROOTFS_TREE"/usr/share/doc/brk
mkdir -p "$ROOTFS_TREE"/var/{log,tmp,spool,cache,lib}

# FHS: world-writable scratch directories with the sticky bit.
chmod 1777 "$ROOTFS_TREE/tmp" "$ROOTFS_TREE/var/tmp"

for d in bin sbin etc home root tmp boot dev proc sys run mnt media opt srv \
	 lib lib64 usr var; do
	"$BRKFS" cp -r --parents "$ROOTFS_TREE/$d" "/$d" "$IMG"
done

# Programs installed outside /bin.  init is PID 1 and belongs under
# /sbin; the diagnostic and admin tools live under /usr/bin.  Their
# sources live outside src/cmd as well (src/init, src/brktest, ...).
SYSTEM_BINS=(init)
USR_BINS=(brktest clear df edit fallocate httpd httpget nc netstat stat
	  tput)

for name in "${SYSTEM_BINS[@]}"; do
	"$BRKFS" cp --parents "$BIN_DIR/$name" "/sbin/$name" "$IMG"
done

for name in "${USR_BINS[@]}"; do
	"$BRKFS" cp --parents "$BIN_DIR/$name" "/usr/bin/$name" "$IMG"
done

mapfile -t bins < <(
	make -C "$USER_ROOT" -s list-progs KERNEL_ROOT="$KERNEL_ROOT" \
		BUILD_ROOT="$BUILD_ROOT" \
		${CROSS_COMPILE:+CROSS_COMPILE="$CROSS_COMPILE"}
)
for name in "${bins[@]}"; do
	case " ${SYSTEM_BINS[*]} ${USR_BINS[*]} " in
	*" $name "*)
		continue
		;;
	esac
	"$BRKFS" cp --parents "$BIN_DIR/$name" "/bin/$name" "$IMG"
done

if [[ -d $USER_ROOT/etc ]]; then
	# The shadow file holds password hashes; it must stay root-only even
	# on the host side so the mode survives the copy into the image.
	[[ -f $USER_ROOT/etc/shadow ]] && chmod 0600 "$USER_ROOT/etc/shadow"
	"$BRKFS" cp -r --parents "$USER_ROOT/etc" "/etc" "$IMG"
fi

if [[ -d $USER_ROOT/tests ]]; then
	"$BRKFS" cp -r "$USER_ROOT/tests" "/home/tests" "$IMG"
fi

# libc tests are built as normal libuser-linked executables; install those
# alongside their sources so they can be run directly inside BRK.
if [[ -d $BUILD_ROOT/tests/libc ]]; then
	for test_bin in "$BUILD_ROOT"/tests/libc/*; do
		[[ -f $test_bin ]] || continue
		"$BRKFS" cp "$test_bin" \
			"/home/tests/libc/${test_bin##*/}" "$IMG"
	done
fi

if [[ -f $USER_ROOT/src/brkcc/include/cc.h ]]; then
	"$BRKFS" cp "$USER_ROOT/src/brkcc/include/cc.h" "/usr/include/cc.h" "$IMG"
fi

for doc in README.md README.cn.md LICENSE; do
	if [[ -f $KERNEL_ROOT/$doc ]]; then
		"$BRKFS" cp --parents "$KERNEL_ROOT/$doc" \
			"/usr/share/doc/brk/$doc" "$IMG"
	fi
done
