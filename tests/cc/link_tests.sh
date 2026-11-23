#!/bin/sh
#
# Host-side linker and object-format tests for bin/cc.
#
# Run after building the host compiler:
#   make cc-host HOSTCC=gcc
#   tests/cc/link_tests.sh
#
# The generated RISC-V executables are not run here; the test checks that
# CCO2 serialization round-trips, that CCO1 objects are still readable, that
# source and object links produce identical images, and that malformed
# objects and bad link inputs are rejected.

set -u

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
CC=${CC:-"$root/build/cc-host"}
src_dir=${SRC_DIR:-"$script_dir"}

if [ ! -x "$CC" ]; then
	echo "link_tests: $CC not found; run 'make cc-host HOSTCC=gcc'" >&2
	exit 1
fi

TMP=${TMPDIR:-/tmp}/cc-link-tests.$$
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
mkdir -p "$TMP" || exit 1

failures=0

note() {
	printf 'ok: %s\n' "$1"
}

fail() {
	printf 'not ok: %s\n' "$1" >&2
	failures=$((failures + 1))
}

run_cc() {
	"$CC" -I"$root/usr/include" -I"$src_dir" "$@"
}

expect_fail() {
	label=$1
	pattern=$2
	shift 2
	if "$@" >"$TMP/out" 2>"$TMP/err"; then
		fail "$label (command unexpectedly succeeded)"
		return
	fi
	if ! grep -F -- "$pattern" "$TMP/err" >/dev/null; then
		printf '  stderr:\n' >&2
		sed 's/^/    /' "$TMP/err" >&2
		fail "$label (missing '$pattern')"
		return
	fi
	note "$label"
}

u32() {
	od -An -tu4 -j "$1" -N4 "$2" | tr -d ' \n'
}

patch() {
	printf "$1" | dd of="$3" bs=1 seek="$2" conv=notrunc 2>/dev/null
}

# End of the symbol table and the number of data relocations.
sym_end() {
	f=$1
	tl=$(u32 8 "$f")
	dl=$(u32 12 "$f")
	ns=$(u32 20 "$f")
	nd=$(u32 24 "$f")
	o=$((32 + tl + dl))
	i=0
	while [ "$i" -lt "$ns" ]; do
		n=$(u32 "$o" "$f")
		o=$((o + 4 + n + 16 + 2))
		i=$((i + 1))
	done
	echo "$o $nd"
}

# ------------------------------------------------------------------
# Multi-unit source vs. object linking, and CCO2 header.
# ------------------------------------------------------------------

if run_cc "$src_dir/link_multi_a.c" "$src_dir/link_multi_b.c" \
	-o "$TMP/direct"; then
	note "multi-unit source link"
else
	fail "multi-unit source link"
fi

if run_cc -c "$src_dir/link_multi_a.c" -o "$TMP/a.o" &&
	run_cc -c "$src_dir/link_multi_b.c" -o "$TMP/b.o"; then
	note "multi-unit -c compile"
else
	fail "multi-unit -c compile"
fi

magic=$(dd if="$TMP/a.o" bs=1 count=4 2>/dev/null)
version=$(od -An -tx1 -j4 -N4 "$TMP/a.o" | tr -d ' \n')
if [ "$magic" = "CCO2" ] && [ "$version" = "02000000" ]; then
	note "CCO2 magic and version 2"
else
	fail "CCO2 magic and version 2 (magic='$magic' version='$version')"
fi

if run_cc "$TMP/a.o" "$TMP/b.o" -o "$TMP/obj"; then
	note "multi-unit object link"
else
	fail "multi-unit object link"
fi

if cmp -s "$TMP/direct" "$TMP/obj"; then
	note "source and object links are identical"
else
	fail "source and object links are identical"
fi

# ------------------------------------------------------------------
# Assembly listing mode and CLI mode exclusivity.
# ------------------------------------------------------------------

if run_cc -S "$src_dir/link_multi_b.c" -o "$TMP/unit.s"; then
	if grep -q '^[[:space:]]*\.text' "$TMP/unit.s" &&
		grep -q 'addi' "$TMP/unit.s"; then
		note "-S writes a readable assembly listing"
	else
		fail "-S writes a readable assembly listing"
	fi
else
	fail "-S writes a readable assembly listing"
fi

if "$CC" -S -c "$src_dir/link_multi_b.c" -o "$TMP/bad.s" \
	>"$TMP/out" 2>"$TMP/err"; then
	fail "-S and -c are mutually exclusive"
else
	note "-S and -c are mutually exclusive"
fi

# ------------------------------------------------------------------
# CCO1 compatibility: the body is identical, only magic/version differ.
# ------------------------------------------------------------------

cp "$TMP/a.o" "$TMP/a1.o"
cp "$TMP/b.o" "$TMP/b1.o"
patch 'CCO1\001\000\000\000' 0 "$TMP/a1.o"
patch 'CCO1\001\000\000\000' 0 "$TMP/b1.o"
if run_cc "$TMP/a1.o" "$TMP/b1.o" -o "$TMP/obj1" &&
	cmp -s "$TMP/direct" "$TMP/obj1"; then
	note "CCO1 reader compatibility"
else
	fail "CCO1 reader compatibility"
fi

# ------------------------------------------------------------------
# ELF layout: one RWX PT_LOAD, fixed base, bss outside the file.
# ------------------------------------------------------------------

if command -v readelf >/dev/null 2>&1; then
	loads=$(readelf -lW "$TMP/direct" |
		awk '$1 == "LOAD" { n++ } END { print n + 0 }')
	load=$(readelf -lW "$TMP/direct" | awk '$1 == "LOAD" { print; exit }')
	set -- $load
	if [ "$loads" -eq 1 ] && [ "$1" = "LOAD" ] &&
		[ "$2" = "0x000000" ] &&
		[ "$3" = "0x0000000000010000" ] &&
		[ "$7" = "RWE" ] && [ $(( $5 )) -lt $(( $6 )) ]; then
		note "single RWX PT_LOAD at 0x10000 with bss outside file"
	else
		fail "single RWX PT_LOAD at 0x10000 with bss outside file"
	fi
	entry=$(readelf -h "$TMP/direct" |
		awk -F: '/Entry point address/ { gsub(/ /, "", $2); print $2 }')
	if [ -n "$entry" ] && [ "$entry" != "0x0" ]; then
		note "non-zero entry point"
	else
		fail "non-zero entry point"
	fi
else
	note "readelf not found; skipping ELF layout checks"
fi

# ------------------------------------------------------------------
# Link errors.
# ------------------------------------------------------------------

cat >"$TMP/dup_a.c" <<'EOF'
int dup = 1;
int main(void)
{
	return dup;
}
EOF
cat >"$TMP/dup_b.c" <<'EOF'
int dup = 2;
EOF
cat >"$TMP/main_a.c" <<'EOF'
int main(void)
{
	return 0;
}
EOF
cat >"$TMP/main_b.c" <<'EOF'
int main(void)
{
	return 1;
}
EOF
cat >"$TMP/undef.c" <<'EOF'
int missing(void);
int main(void)
{
	return missing();
}
EOF
cat >"$TMP/empty.c" <<'EOF'
int not_main;
EOF
cat >"$TMP/data_a.c" <<'EOF'
extern int ext;
int *p = &ext;
int main(void)
{
	return *p;
}
EOF
cat >"$TMP/data_b.c" <<'EOF'
int ext = 3;
EOF
cat >"$TMP/static_undef.c" <<'EOF'
static int hidden(void);
int call_hidden(void)
{
	return hidden();
}
int main(void)
{
	return call_hidden();
}
EOF
cat >"$TMP/static_other.c" <<'EOF'
static int hidden(void)
{
	return 7;
}
EOF

run_cc -c "$TMP/dup_a.c" -o "$TMP/dup_a.o" || fail "compile duplicate fixture"
run_cc -c "$TMP/dup_b.c" -o "$TMP/dup_b.o" || fail "compile duplicate fixture"
expect_fail "duplicate external symbol" "duplicate symbol 'dup'" \
	run_cc "$TMP/dup_a.o" "$TMP/dup_b.o" -o "$TMP/out.bin"

run_cc -c "$TMP/main_a.c" -o "$TMP/main_a.o" || fail "compile main fixture"
run_cc -c "$TMP/main_b.c" -o "$TMP/main_b.o" || fail "compile main fixture"
expect_fail "duplicate main" "multiple definitions of 'main'" \
	run_cc "$TMP/main_a.o" "$TMP/main_b.o" -o "$TMP/out.bin"

run_cc -c "$TMP/undef.c" -o "$TMP/undef.o" || fail "compile undefined fixture"
expect_fail "unresolved external symbol" "undefined symbol 'missing'" \
	run_cc "$TMP/undef.o" -o "$TMP/out.bin"

run_cc -c "$TMP/empty.c" -o "$TMP/empty.o" || fail "compile empty fixture"
expect_fail "program without main" "no definition of 'main'" \
	run_cc "$TMP/empty.o" -o "$TMP/out.bin"

run_cc -c "$TMP/static_undef.c" -o "$TMP/static_undef.o" ||
	fail "compile static fixture"
run_cc -c "$TMP/static_other.c" -o "$TMP/static_other.o" ||
	fail "compile static fixture"
expect_fail "undefined static symbol stays unit-local" \
	"undefined static symbol" \
	run_cc "$TMP/static_undef.o" "$TMP/static_other.o" -o "$TMP/out.bin"

run_cc -c "$TMP/data_a.c" -o "$TMP/data_a.o" || fail "compile relocation fixture"
run_cc -c "$TMP/data_b.c" -o "$TMP/data_b.o" || fail "compile relocation fixture"
set -- $(sym_end "$TMP/data_a.o")
sym_off=$1
ndrels=$2
rel_off=$((sym_off + ndrels * 20))

cp "$TMP/data_a.o" "$TMP/trailing.o"
printf X >>"$TMP/trailing.o"
expect_fail "trailing object data" "trailing data in object file" \
	run_cc "$TMP/trailing.o" -o "$TMP/out.bin"

cp "$TMP/data_a.o" "$TMP/magic.o"
patch 'XXXX' 0 "$TMP/magic.o"
expect_fail "bad object magic" "is not a cc object file" \
	run_cc "$TMP/magic.o" -o "$TMP/out.bin"

cp "$TMP/data_a.o" "$TMP/version.o"
patch '\001\000\000\000' 4 "$TMP/version.o"
expect_fail "object version mismatch" "unsupported object file version" \
	run_cc "$TMP/version.o" -o "$TMP/out.bin"

cp "$TMP/data_a.o" "$TMP/short.o"
size=$(wc -c <"$TMP/short.o")
dd if="$TMP/short.o" of="$TMP/short2.o" bs=1 count=$((size - 3)) \
	2>/dev/null
expect_fail "truncated object" "truncated object file" \
	run_cc "$TMP/short2.o" -o "$TMP/out.bin"

cp "$TMP/data_a.o" "$TMP/badkind.o"
patch '\143\000\000\000' $((rel_off + 4)) "$TMP/badkind.o"
expect_fail "bad text relocation kind" "bad relocation kind" \
	run_cc "$TMP/badkind.o" -o "$TMP/out.bin"

cp "$TMP/data_a.o" "$TMP/badpos.o"
patch '\377\377\377\177' "$rel_off" "$TMP/badpos.o"
expect_fail "text relocation out of range" "text relocation out of range" \
	run_cc "$TMP/badpos.o" -o "$TMP/out.bin"

cp "$TMP/data_a.o" "$TMP/badindex.o"
patch '\377\377\377\177' $((sym_off + 8)) "$TMP/badindex.o"
expect_fail "bad relocation symbol index" "bad symbol index" \
	run_cc "$TMP/badindex.o" -o "$TMP/out.bin"

cp "$TMP/data_a.o" "$TMP/baddata.o"
patch '\377\377\377\177' $((sym_off + 4)) "$TMP/baddata.o"
expect_fail "data relocation out of range" "data relocation out of range" \
	run_cc "$TMP/baddata.o" "$TMP/data_b.o" -o "$TMP/out.bin"

if [ "$failures" -ne 0 ]; then
	printf '%d linker test(s) failed\n' "$failures" >&2
	exit 1
fi
printf 'all linker tests passed\n'
