#!/bin/sh
#
# Host-side linker and object-format tests for bin/cc.
#
# Run after building the host compiler:
#   make cc-host HOSTCC=gcc
#   tests/cc/link_tests.sh
#
# The generated RISC-V executables are not run here; the test checks ELF
# object emission, standard relocations, archive extraction, links against
# the built libc.a/libgcc.a, source/object image identity, legacy CCO1/CCO2
# compatibility, and rejection of malformed objects and bad link inputs.

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
	"$CC" -I"$root/build/include" -I"$root/usr/include" -I"$src_dir" "$@"
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

ident=$(od -An -tx1 -N4 "$TMP/a.o" | tr -d ' \n')
etype=$(od -An -tu2 -j16 -N2 "$TMP/a.o" | tr -d ' \n')
machine=$(od -An -tu2 -j18 -N2 "$TMP/a.o" | tr -d ' \n')
flags=$(od -An -tu4 -j48 -N4 "$TMP/a.o" | tr -d ' \n')
if [ "$ident" = "7f454c46" ] && [ "$etype" = "1" ] &&
	[ "$machine" = "243" ] && [ "$flags" = "4" ]; then
	note "ELF64 ET_REL RISC-V object with double-float flags"
else
	fail "ELF64 ET_REL RISC-V object with double-float flags (ident='$ident' type='$etype' machine='$machine' flags='$flags')"
fi

if command -v readelf >/dev/null 2>&1; then
	sections=$(readelf -SW "$TMP/a.o")
	if printf '%s\n' "$sections" | grep -q '\.text .*PROGBITS' &&
		printf '%s\n' "$sections" | grep -q '\.data .*PROGBITS' &&
		printf '%s\n' "$sections" | grep -q '\.bss .*NOBITS' &&
		printf '%s\n' "$sections" | grep -q '\.rela\.text .*RELA' &&
		printf '%s\n' "$sections" | grep -q '\.rela\.data .*RELA' &&
		printf '%s\n' "$sections" | grep -q '\.symtab .*SYMTAB'; then
		note "ELF object carries text/data/bss/symtab/rela sections"
	else
		fail "ELF object carries text/data/bss/symtab/rela sections"
	fi
	relocs=$(readelf -rW "$TMP/a.o")
	if printf '%s\n' "$relocs" | grep -q 'R_RISCV_HI20' &&
		printf '%s\n' "$relocs" | grep -q 'R_RISCV_LO12_I' &&
		printf '%s\n' "$relocs" | grep -q 'R_RISCV_JAL' &&
		printf '%s\n' "$relocs" | grep -q 'R_RISCV_64'; then
		note "ELF object uses standard RISC-V relocations"
	else
		fail "ELF object uses standard RISC-V relocations"
	fi
else
	note "readelf not found; skipping ELF section checks"
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
# CCO2/CCO1 compatibility: --cco2-object still writes the legacy format,
# and the body is identical to the old CCO2 layout.
# ------------------------------------------------------------------

if run_cc --cco2-object -c "$src_dir/link_multi_a.c" -o "$TMP/a.cco2.o" &&
	run_cc --cco2-object -c "$src_dir/link_multi_b.c" -o "$TMP/b.cco2.o"; then
	note "legacy CCO2 -c compile"
else
	fail "legacy CCO2 -c compile"
fi

magic=$(dd if="$TMP/a.cco2.o" bs=1 count=4 2>/dev/null)
version=$(od -An -tx1 -j4 -N4 "$TMP/a.cco2.o" | tr -d ' \n')
if [ "$magic" = "CCO2" ] && [ "$version" = "02000000" ]; then
	note "CCO2 magic and version 2"
else
	fail "CCO2 magic and version 2 (magic='$magic' version='$version')"
fi

cp "$TMP/a.cco2.o" "$TMP/a1.o"
cp "$TMP/b.cco2.o" "$TMP/b1.o"
patch 'CCO1\001\000\000\000' 0 "$TMP/a1.o"
patch 'CCO1\001\000\000\000' 0 "$TMP/b1.o"
if run_cc "$TMP/a1.o" "$TMP/b1.o" -o "$TMP/obj1" &&
	cmp -s "$TMP/direct" "$TMP/obj1"; then
	note "CCO1 reader compatibility"
else
	fail "CCO1 reader compatibility"
fi

# ------------------------------------------------------------------
# Static archives and -L/-l resolution.
# ------------------------------------------------------------------

AR=${AR:-}
if [ -z "$AR" ]; then
	for candidate in riscv64-unknown-elf-ar riscv64-elf-ar ar; do
		if command -v "$candidate" >/dev/null 2>&1; then
			AR=$candidate
			break
		fi
	done
fi

if [ -n "$AR" ]; then
	cat >"$TMP/ar_use.c" <<'AR_EOF'
int ar_double(int x);
int ar_sum(void);
int main(void)
{
	return ar_double(3) + ar_sum();
}
AR_EOF
	cat >"$TMP/ar_x.c" <<'AR_EOF'
int ar_value = 7;
int ar_double(int x)
{
	return x * 2;
}
AR_EOF
	cat >"$TMP/ar_y.c" <<'AR_EOF'
extern int ar_value;
int ar_sum(void)
{
	return ar_value + 3;
}
AR_EOF
	cat >"$TMP/ar_unused.c" <<'AR_EOF'
int missing_from_archive(void);
int ar_unused(void)
{
	return missing_from_archive();
}
AR_EOF

	if run_cc -c "$TMP/ar_x.c" -o "$TMP/ar_x.o" &&
		run_cc -c "$TMP/ar_y.c" -o "$TMP/ar_y.o" &&
		run_cc -c "$TMP/ar_unused.c" -o "$TMP/ar_unused.o" &&
		"$AR" rc "$TMP/libarchive.a" "$TMP/ar_x.o" "$TMP/ar_y.o" 			"$TMP/ar_unused.o"; then
		if run_cc "$TMP/ar_use.c" -L "$TMP" -larchive 			-o "$TMP/ar_prog"; then
			note "archive member extraction and -L/-l"
		else
			fail "archive member extraction and -L/-l"
		fi
	else
		fail "create archive fixture"
	fi

	expect_fail "missing library" "cannot find -lno_such_archive" 		run_cc "$TMP/ar_use.c" -L "$TMP" -lno_such_archive 			-o "$TMP/out.bin"
else
	note "ar not found; skipping archive tests"
fi

# ------------------------------------------------------------------
# Real-world inputs: link against the built libc.a and the cross libgcc.a.
# ------------------------------------------------------------------

libc_dir=
for d in "$root/build/debug" "$root/build/release"; do
	if [ -f "$d/libc.a" ]; then
		libc_dir=$d
		break
	fi
done
cross_gcc=$(command -v riscv64-unknown-elf-gcc ||
	command -v riscv64-elf-gcc || true)
if [ -n "$libc_dir" ] && [ -n "$cross_gcc" ]; then
	libgcc=$("$cross_gcc" -march=rv64gc -mabi=lp64d 		-print-file-name=libgcc.a 2>/dev/null || true)
	if [ -f "$libgcc" ]; then
		cat >"$TMP/libc_use.c" <<'LIBC_EOF'
unsigned long strlen(const char *s);
int snprintf(char *buf, unsigned long n, const char *fmt, ...);
int main(void)
{
	char b[16];

	snprintf(b, sizeof(b), "%d", 7);
	return (int)strlen(b);
}
LIBC_EOF
		if run_cc "$TMP/libc_use.c" -L "$libc_dir" -lc 			-L "$(dirname "$libgcc")" -lgcc 			-o "$TMP/libc_prog"; then
			note "libc.a + libgcc.a archive link"
		else
			fail "libc.a + libgcc.a archive link"
		fi
	else
		note "libgcc.a not found; skipping libc archive link"
	fi
else
	note "built libc.a or cross gcc not found; skipping libc archive link"
fi

# ------------------------------------------------------------------
# ELF layout: separate RX and RW PT_LOAD segments, fixed base, bss
# outside the file.
# ------------------------------------------------------------------

if command -v readelf >/dev/null 2>&1; then
	layout=$(readelf -lW "$TMP/direct" | awk '
		$1 == "LOAD" {
			n++;
			if (n == 1) {
				off1 = $2; va1 = $3; fs1 = $5; ms1 = $6;
				rx = ($7 == "R" && $8 == "E");
			} else if (n == 2) {
				off2 = $2; va2 = $3; fs2 = $5; ms2 = $6;
				rw = ($7 == "RW");
			}
		}
		END {
			if (n == 2 && off1 == "0x000000" &&
			    va1 == "0x0000000000010000" && rx && rw &&
			    off2 != "0x000000" && va2 != va1 &&
			    fs2 != "0x000000" && ms2 != fs2)
				print "ok";
		}')
	if [ "$layout" = "ok" ]; then
		note "separate RX and RW PT_LOAD segments with bss outside file"
	else
		fail "separate RX and RW PT_LOAD segments with bss outside file"
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

run_cc --cco2-object -c "$TMP/data_a.c" -o "$TMP/data_a.o" ||
	fail "compile relocation fixture"
run_cc --cco2-object -c "$TMP/data_b.c" -o "$TMP/data_b.o" ||
	fail "compile relocation fixture"
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

# ------------------------------------------------------------------
# Malformed standard ELF objects.
# ------------------------------------------------------------------

run_cc -c "$TMP/data_a.c" -o "$TMP/data_a.elf.o" ||
	fail "compile ELF relocation fixture"

cp "$TMP/data_a.elf.o" "$TMP/badmachine.o"
patch '\001\000' 18 "$TMP/badmachine.o"
expect_fail "non-RISC-V ELF object" "is not a RISC-V ELF object" \
	run_cc "$TMP/badmachine.o" -o "$TMP/out.bin"

cp "$TMP/data_a.elf.o" "$TMP/badshoff.o"
patch '\377\377\377\177\000\000\000\000' 40 "$TMP/badshoff.o"
expect_fail "bad ELF section header offset" "truncated section header table" \
	run_cc "$TMP/badshoff.o" -o "$TMP/out.bin"

cp "$TMP/data_a.elf.o" "$TMP/truncated.o"
esize=$(wc -c <"$TMP/truncated.o")
dd if="$TMP/truncated.o" of="$TMP/truncated2.o" bs=1 count=$((esize - 3)) \
	2>/dev/null
expect_fail "truncated ELF object" "truncated" \
	run_cc "$TMP/truncated2.o" -o "$TMP/out.bin"

cp "$TMP/data_a.elf.o" "$TMP/badelfmagic.o"
patch 'XXXX' 0 "$TMP/badelfmagic.o"
expect_fail "bad ELF magic" "is not a cc object file" \
	run_cc "$TMP/badelfmagic.o" -o "$TMP/out.bin"

if [ "$failures" -ne 0 ]; then
	printf '%d linker test(s) failed\n' "$failures" >&2
	exit 1
fi
printf 'all linker tests passed\n'
