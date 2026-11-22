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
# The userland build type selects the variant subdirectory the same way
# the Makefile does, so debug and release binaries never mix in one
# image.
BUILD=${BUILD:-debug}
VARIANT_ROOT="$BUILD_ROOT/$BUILD"
BIN_DIR="$VARIANT_ROOT/bin"
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
	BUILD="$BUILD" \
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

# The command tree mirrors the root filesystem: every single source
# file or directory under bin/, usr/bin/ and sbin/ is a command, and
# its location is its install path.  There is no packaging whitelist.
install_cmds()
{
	local entry name

	for entry in "$USER_ROOT/$1"/*; do
		name=$(basename "$entry")
		if [[ $name == *.c ]]; then
			name=${name%.c}
		elif [[ ! -d $entry ]]; then
			continue
		fi
		"$BRKFS" cp --parents "$BIN_DIR/$name" "$2/$name" "$IMG"
	done
}

install_cmds bin /bin
install_cmds sbin /sbin
install_cmds usr/bin /usr/bin

# The Lua interpreter is vendored under vendor/lua-5.5.1, organized by
# origin rather than by install location; it installs under /bin like the
# other user commands.
"$BRKFS" cp --parents "$BIN_DIR/lua" "/bin/lua" "$IMG"

# `[` is a second name for the test command, as on classic Unix; ship it
# as a copy of the test binary instead of a dedicated wrapper source.
"$BRKFS" cp --parents "$BIN_DIR/test" "/bin/[" "$IMG"

if [[ -d $USER_ROOT/etc ]]; then
	# Stage /etc through the build tree so packaging never mutates a
	# tracked source file: shadow holds password hashes and must be
	# root-only in the image, but the checkout keeps its own mode.
	rm -rf "$ROOTFS_TREE/etc"
	cp -r "$USER_ROOT/etc" "$ROOTFS_TREE/etc"
	[[ -f $ROOTFS_TREE/etc/shadow ]] && chmod 0600 "$ROOTFS_TREE/etc/shadow"
	"$BRKFS" cp -r --parents "$ROOTFS_TREE/etc" "/etc" "$IMG"
fi

if [[ -d $USER_ROOT/tests ]]; then
	"$BRKFS" cp -r "$USER_ROOT/tests" "/home/tests" "$IMG"
fi

# libc tests are built as normal library-linked executables; install
# those alongside their sources so they can be run directly inside BRK.
if [[ -d $VARIANT_ROOT/tests/libc ]]; then
	for test_bin in "$VARIANT_ROOT"/tests/libc/*; do
		[[ -f $test_bin ]] || continue
		"$BRKFS" cp "$test_bin" \
			"/home/tests/libc/${test_bin##*/}" "$IMG"
	done
fi

# Dynamic-linking artifacts: /lib/ld-brk.so is the PT_INTERP target
# every PIE executable names, /lib/libhello.so backs the in-system
# loader smoke test (dynhello rides along in /home/tests/libc).
if [[ -f $VARIANT_ROOT/ld-brk.so ]]; then
	"$BRKFS" cp --parents "$VARIANT_ROOT/ld-brk.so" "/lib/ld-brk.so" "$IMG"
fi
if [[ -f $VARIANT_ROOT/libhello.so ]]; then
	"$BRKFS" cp --parents "$VARIANT_ROOT/libhello.so" "/lib/libhello.so" "$IMG"
fi
if [[ -f $VARIANT_ROOT/libplug.so ]]; then
	"$BRKFS" cp --parents "$VARIANT_ROOT/libplug.so" "/lib/libplug.so" "$IMG"
fi
if [[ -f $VARIANT_ROOT/libc.so ]]; then
	"$BRKFS" cp --parents "$VARIANT_ROOT/libc.so" "/lib/libc.so" "$IMG"
fi
if [[ -f $VARIANT_ROOT/libdyn.so ]]; then
	"$BRKFS" cp --parents "$VARIANT_ROOT/libdyn.so" "/lib/libdyn.so" "$IMG"
fi

# usr/include/cc.h is the runtime header for programs compiled by the
# on-target cc: a raw-syscall subset with no libc dependency.  It is
# the only header installed into the image so far; a future on-target
# toolchain would install all of usr/include the same way.
if [[ -f $USER_ROOT/usr/include/cc.h ]]; then
	"$BRKFS" cp "$USER_ROOT/usr/include/cc.h" "/usr/include/cc.h" "$IMG"
fi

for doc in README.md README.cn.md LICENSE; do
	if [[ -f $KERNEL_ROOT/$doc ]]; then
		"$BRKFS" cp --parents "$KERNEL_ROOT/$doc" \
			"/usr/share/doc/brk/$doc" "$IMG"
	fi
done
