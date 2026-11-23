# cc thread contract

This document is the coordination contract for the four component threads that
extend `bin/cc`.  It is intentionally stricter than the user-facing README:
the README describes what the compiler does, this file describes how the
implementation is allowed to change.

## 1. Goal

Complete the self-contained C toolchain in `bin/cc`:

- preprocessor: `lexer.c`, `preprocess.c`
- front end: `types.c`, `parser.c`
- assembler/back end: `codegen.c`, `emit.c`, `asm.c`
- linker: `link.c`, `obj.c`

No optimization passes, no register allocator, no debug information.  The
generated programs must keep depending only on kernel system calls and the
runtime helpers emitted by `cc` itself.

## 2. Pipeline

```text
.c / .h text
    |
    |  preprocess + lex
    v
token stream (struct token)
    |
    |  parse + type check
    v
AST (struct node) + symbols (struct obj / struct sym)
    |
    |  instruction selection
    v
structured instruction records (asm.c)
    |
    |  assembler: layout, labels, relocations
    v
relocatable unit (struct unit)
    |
    |  object serialization (ELF ET_REL)
    v
.o file
    |
    |  link: resolve, relocate, lay out
    v
ELF64 executable
```

`-E` stops after preprocessing.  `-S` stops after the assembler and writes a
human-readable assembly listing.  `-c` stops after object serialization.

## 3. File ownership

A thread may only modify the files listed for it.  A thread that finds a bug
in another component reports it to the coordinator instead of editing that
component.

| Thread | Owned files | May add tests under |
| --- | --- | --- |
| `pp` | `bin/cc/lexer.c`, `bin/cc/preprocess.c` | `tests/cc/pp_*.c` |
| `front` | `bin/cc/types.c`, `bin/cc/parser.c` | `tests/cc/front_*.c` |
| `back` | `bin/cc/codegen.c`, `bin/cc/emit.c`, `bin/cc/asm.c` | `tests/cc/back_*.c` |
| `link` | `bin/cc/link.c`, `bin/cc/obj.c` | `tests/cc/link_*.c` |
| `coord` | `bin/cc/brkcc.h`, `bin/cc/main.c`, `Makefile`, `bin/cc/README.md`, `docs/**`, `tests/cc/README.md`, shared test fixtures | everything |

`bin/cc/brkcc.h` is the shared ABI.  Only the coordinator edits it.  If a
thread needs a new field or helper, it must:

1. state the exact declaration and why it is needed in its handoff report;
2. avoid adding it locally to the header;
3. keep the branch buildable with the frozen header, or clearly mark the
   branch as blocked on that interface request.

New `.c` files in `bin/cc/` are picked up automatically by the Makefile
wildcard.  Do not edit `main.c` from a component thread; the coordinator
wires the CLI.

## 4. Shared ABI

### 4.1 Data model

- `char` is unsigned by default; `signed char` is explicit.
- `short` 2, `int` 4, `long` 8, pointers 8, `float` 4, `double` 8.
- `long long` is accepted as an alias of `long`.
- Aggregates use their natural alignment; bit-fields are laid out LSB-first
  inside the storage unit of their declared type.
- Function arguments and return values use the private `cc` ABI documented in
  `docs/cc-internal-abi.md`; it is self-consistent, not the RISC-V psABI.

### 4.2 Calling convention summary

Core scalar/FP/aggregate/variadic rules follow the RISC-V LP64D ELF psABI:

- integer/pointer arguments use `a0`-`a7`; named `float`/`double` arguments
  use `fa0`-`fa7`;
- arguments that do not fit in their register sequence go on the caller stack
  at `0(sp)`, `8(sp)`, ...; the stack is 16-byte aligned;
- variadic floating-point arguments use the integer sequence;
- small aggregates are classified by up to two eight-byte words and may use
  integer and FP registers; large aggregates are passed by reference;
- small aggregates return in `a0/a1`, `fa0/fa1`, or a mixture; large
  aggregates use a hidden result pointer in `a0`;
- `s0`/`s1` are preserved; generated functions do not use `s2`-`s11` or
  `fs0`-`fs11`;
- `va_list` points at the saved argument area, as described in
  `docs/cc-internal-abi.md`.

`long double` is currently the compiler's 8-byte double representation rather
than the psABI's 128-bit type; this remains a follow-up item.

### 4.3 Object format

`cc -c` now emits standard ELF64 little-endian RISC-V `ET_REL` objects with
`.text`, `.data`, `.bss`, `.rela.text`, `.rela.data`, `.symtab`, `.strtab`
and `.shstrtab`, using `R_RISCV_HI20`, `R_RISCV_LO12_I`, `R_RISCV_JAL` and
`R_RISCV_64` relocations.  The built-in linker consumes them directly.

The legacy private CCO2 format is still readable (and writable with
`--cco2-object` for compatibility tests).  It carries:

- magic `CCO2`, version 2;
- `.text` bytes, `.data` bytes, `.bss` size;
- symbol table: name, section, offset, size, alignment, defined/static flags;
- text relocations: offset, kind, symbol index, addend;
- data relocations: owner symbol index, offset, target symbol index, addend.

The assembler produces an in-memory `struct unit`; the linker owns
serialization/deserialization.  `CCO1` objects remain readable for
compatibility.

## 5. Work protocol

- Each component thread works in its own git worktree/branch named
  `cc/<component>`.
- Base all work on the freeze commit that contains this contract and the
  extended `brkcc.h`.
- Do not rebase or force-push shared branches.  The coordinator merges.
- Keep commits focused: one logical change per commit, message
  `feat(cc/<component>): ...` or `fix(cc/<component>): ...`.
- Before handoff:
  - `make cc-host HOSTCC=gcc` passes with `-Werror`;
  - `make -j4` passes;
  - all currently runnable `tests/cc` still build and run;
  - the new tests owned by the thread are present and documented;
  - the handoff report lists changed files, tests run, known gaps and any
    header request.
- No unrelated formatting, no dead debug prints, no `TODO` stubs in merged
  code.

## 6. Integration order and gates

1. coordinator freezes `brkcc.h`, `types.c` base types and docs;
2. `pp` merge;
3. `front` merge;
4. `back` merge;
5. `link` merge;
6. coordinator runs the full in-system suite and updates the README.

After every merge the coordinator runs:

```sh
make cc-host HOSTCC=gcc
make -j4
for f in tests/cc/*.c; do build/cc-host -Iusr/include -Itests/cc "$f" -o /tmp/cc-test; done
```

and the QEMU/BRK suite for the tests that need a real kernel.

## 7. Definition of done

- `-E`, `-S`, `-c` and link-only modes work.
- `float`/`double`, aggregate parameter/return by value, bit-fields,
  `static` locals and more than 8 parameters work.
- Existing tests still pass in BRK.
- New tests cover the added features and at least one error path per
  component.
- `bin/cc/README.md` matches reality; no stale "not supported" entries for
  features that now work.
- The four components can be identified by file and interface without
  reaching into another component's internals.

## 8. Non-goals

- optimization, register allocation, instruction scheduling;
- debug info, LTO, PGO;
- standard ELF relocatable objects, static/dynamic libraries, linker scripts;
- C11 threads, TLS, VLAs, `_Complex`, `_Atomic`, wide characters;
- ABI compatibility with the system RISC-V toolchain.
