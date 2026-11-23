# cc — A Lightweight User-Space C Compiler for BRK

`bin/cc/` is a small C compiler that runs in BRK/RISC-V64 user space. It does not depend on the target machine's `as`/`ld`; instead, it performs lexing, preprocessing, parsing, code generation, linking, and ELF writing itself. The compiler itself still uses this repository's `libc.a`, but **the generated programs do not link against libc**—they depend only on kernel system calls.

## Quick Start

Build user programs (including `/bin/cc`):

```sh
make -C brk-user
```

Regenerate the rootfs (this installs `cc` to `/bin/cc`, `usr/include/cc.h` to `/usr/include/cc.h`, and copies `tests/` to `/home/tests`):

```sh
make -C brk-user rootfs CROSS_COMPILE=riscv64-unknown-elf-
make -C brk-user run CROSS_COMPILE=riscv64-unknown-elf-
```

In the BRK shell:

```sh
cc /home/tests/cc/hello.c -o /tmp/hello
/tmp/hello

cc /home/tests/cc/multi_a.c /home/tests/cc/multi_b.c -o /tmp/multi
/tmp/multi
```

Usage:

```text
cc [-c] [-E] [-S] [-o output] [-I dir] [-L dir] [-l name]
   [-D name[=value]] [-U name] [--cco2-object] input.c|input.o [...]
```

- If `-o` is not specified, the output is `a.out`
- Multiple `.c` files are compiled separately within the same invocation and then linked into a single executable
- `-S` compiles only and writes a human-readable RISC-V assembly listing
- `-c` compiles only and writes a standard ELF64 `ET_REL` object; the legacy private `CCO2` format remains available with `--cco2-object`
- `.o` inputs are passed to the linker together with `.c` inputs
- `-L dir` adds an archive search directory; `-l name` links `libname.a`
  from those directories (then `/usr/lib`, `/usr/local/lib`, `/lib`)
- Archive members are extracted on demand, so an unused member is never
  pulled into the executable
- The output file is automatically set to `0755`

## Preprocessor

Supported:

- Comments: `//`, `/* ... */`
- Line continuation: backslash + newline
- `#include "file"`: searches the source file's directory first, then `-I` paths
- `#include <file>`: searches `-I` paths and default paths
- Default include paths: `.`, `/usr/include`, `/usr/local/include`
- Object-like macros: `#define NAME value`, `#undef NAME`
- Function-like macros: `#define F(a,b) ...`; arguments are expanded before substitution
- Variadic macros: `#define F(...)`, `__VA_ARGS__`
- Conditional compilation: `#ifdef`, `#ifndef`, `#if`, `#elif`, `#else`, `#endif`
- `#if` supports integer constants, `defined(NAME)`, `!`, arithmetic/comparison/bitwise operations/`&&`/`||`
- `#error`, `#warning`
- `#` stringification, `##` token pasting
- Macro-named `#include`, macro invocations spanning physical lines
- `#pragma once`, `#line`
- Built-in `__FILE__`, `__LINE__`, `__STDC__`, `__STDC_VERSION__`, `__DATE__`, `__TIME__`
- `-D name[=value]`, `-U name`, `-E`

Not supported:

- Hexadecimal floating constants (`0x1p3`)
- Complex standard headers (the target machine provides only a minimal `usr/include/cc.h`)

## Supported C Subset

Types:

- `void`, `char`, `short`, `int`, `long`, `long long`
- `float`, `double` (`long double` uses the double representation)
- `signed` / `unsigned`
- Pointers, arrays (including multidimensional arrays), functions, function pointers
- `struct`, `union`, `enum`, `typedef`
- Bit-fields in structs/unions (named, unnamed and zero-width)
- `char` is **unsigned** by default (RISC-V psABI); `signed char` explicitly declares it signed

Declarations:

- Global variables, local variables, function definitions, function prototypes
- `extern` declarations at file scope and block scope
- `static` file-scope symbols and `static` local variables with persistent storage
- Multiple declarators in one declaration, e.g. `int a, *p, b[4];`
- Declarations in `for (int i = 0; ...)`
- Calls to functions defined later (the compiler creates implicit function symbols;
  when the later definition has a non-integer return type, a visible prototype is required)
- Function pointers, e.g. `int (*fp)(int, int);`
- Functions returning function pointers, e.g. `int (*pick(int))(int, int);`
- Arrays of function pointers, function pointer members in structs
- Aggregate parameters and return values by value
- Any number of parameters/arguments (slots beyond the first eight go on the stack)

Statements:

- Blocks, `if/else`, `while`, `do-while`, `for`
- `switch` / `case` / `default`, with fallthrough and nested switch support
- `goto` and labels
- `return`, `break`, `continue`
- Expression statements, empty statements

Expressions:

- Arithmetic: `+ - * / %` for integers; `+ - * /` for floating point
- Bitwise: `& | ^ << >> ~`
- Comparison: `== != < <= > >=` (integer and floating point)
- Logical: `&& || !` (floating-point operands use numeric truth)
- Assignment, compound assignment: `= += -= *= /= %= &= |= ^= <<= >>=`
- Increment/decrement: `++ --` (prefix/postfix, integers and floating point)
- Ternary, comma, casts, `sizeof`
- Address-of `&`, dereference `*`, array subscripting
- Member access `.` and `->` (including bit-fields and aggregate rvalues)
- Direct calls and indirect calls through function pointers (any argument count)
- Variadic functions: `...`, `va_list`, `va_start`, `va_arg`, `va_end`, `va_copy`

Initializers:

- Global scalars: integer and floating constant expressions
- Global `char[]`: string literals
- Global arrays: `{ ... }` constant lists (can infer `[]` length automatically)
- Global `struct` / `union`: `{ ... }` constant lists, with nesting support
- Global bit-field initializers
- Global multidimensional arrays, arrays of structs
- Global pointers: string literals, `0`, `&global`, array/function addresses
- Global function pointers: function addresses
- Local scalars: arbitrary expressions
- Local arrays/structs/unions:
  - `{ ... }` positional initialization
  - Nested `{ ... }`
  - `char[]` string initialization
  - Remaining parts not explicitly initialized are automatically zeroed
- Designated initializers: `.field = ...`, `[index] = ...` (single level)
- Brace elision: e.g. `int a[2][2] = {1,2,3,4};`
- Struct copy assignment: `a = b`

Not supported:

- Chained designators (e.g. `.a.b = ...`, `[i].x = ...`)
- All edge cases of mixing designated initializers with complex brace elision
- Global aggregate copy initialization (e.g. file-scope `struct P p = other;`)
- Flexible array members
- By-value `struct`/`union` arguments through `...` (`va_arg` of an aggregate)
- GCC-identical bit-field packing for unusual mixed base types; the private
  layout is documented in `docs/cc-internal-abi.md`

Built-in functions (if the user has not defined functions with the same names):

```c
int write(int fd, const void *buf, long n);
int read(int fd, void *buf, long n);
int open(const char *path, int flags, ...);
int close(int fd);
void exit(int status);
int putchar(int c);
int puts(const char *s);
```

`usr/include/cc.h` provides these prototypes as well as `NULL`, `EOF`, and `O_*` constants.

## Multi-File and Linking

Multiple `.c` files in the same invocation are compiled as independent translation units and then merged by the built-in simple linker:

```sh
# Compile and link in one go
cc a.c b.c -o prog

# Separate compilation
cc -c a.c -o a.o
cc -c b.c -o b.o
cc a.o b.o -o prog

# Mix source files and object files
cc a.c b.o -o prog
```

`cc -c` writes standard ELF64 `ET_REL` RISC-V objects with `.text`, `.data`,
`.bss`, `.rela.text`, `.rela.data`, `.symtab`, `.strtab` and
`.riscv.attributes`, using `R_RISCV_HI20`, `R_RISCV_LO12_I`, `R_RISCV_JAL`,
`R_RISCV_64` and related relocations.  GNU `readelf`, `objdump` and `ld -r`
accept them.  For compatibility, `--cco2-object` still writes the old private
CCO2 format, and the linker continues to read CCO2 and CCO1 objects.

The built-in linker also reads GCC-produced ELF objects and static archives.
It supports the relocation forms used by the in-tree libc and libgcc:
`R_RISCV_BRANCH`, `R_RISCV_CALL(_PLT)`, `R_RISCV_PCREL_HI20/LO12_I/LO12_S`,
`R_RISCV_LO12_S`, `R_RISCV_RVC_BRANCH`, `R_RISCV_RVC_JUMP`, 32/64-bit absolute
data, and ADD/SUB data relocations.  This makes `-lc -lgcc` work when the
archives are available.

Linking rules:

- All external symbols other than `main` are resolved by name
- `extern` declarations produce only undefined symbols, resolved by definitions in other units
- `static` functions/global variables are suffixed with the unit name to preserve file-scope linkage
- Each unit can have its own typedefs, tags, enumeration constants, and macros
- Runtime symbols `__cc_puts`, `__cc_memcpy`, and `__cc_memset` are injected uniformly by the linker
- String literals and `static` symbols are unit-local symbols and are not resolved by name across objects
- Duplicate external symbol definitions and unresolved references are reported as errors at link time
- The order of multiple input files does not affect the result

Not yet supported:

- Dynamic libraries/shared objects
- Linker relaxation (the `R_RISCV_RELAX`/`R_RISCV_ALIGN` markers are skipped
  without shrinking code)
- Linker scripts/custom sections; all read-only data is currently placed in
  the writable data segment

## Generated Artifact ABI

Generated programs are:

- `ELF64`, `ET_EXEC`, little-endian, `e_machine = EM_RISCV`
- Two `PT_LOAD` segments: read+execute headers/text, and read+write data+bss
- Fixed load base address `0x10000`, with code starting at file offset `0x1000`
- `e_flags` sets the LP64D float ABI and adds `EF_RISCV_RVC` when any linked
  input uses compressed instructions
- The entry point is the compiler-generated `__cc_start`
- `__cc_start` receives `a0=argc, a1=argv, a2=envp` per the kernel convention, calls `main`, and then exits via `SYS_exit`
- System call numbers use BRK's POSIX.1-2024 numbering: `read=0, write=1, close=2, open=13, exit=49`.  `open()` is lowered to the kernel's `openat(AT_FDCWD, path, flags, mode)` entry.

`puts`, struct copies, and aggregate initialization zeroing use `__cc_puts`, `__cc_memcpy`, and `__cc_memset`, respectively.

The generated scalar, floating-point, aggregate and variadic calling
convention follows the RISC-V LP64D ELF psABI: integer/pointer arguments use
`a0`-`a7`, named `float`/`double` arguments use `fa0`-`fa7`, overflow arguments
go on the caller stack at `0(sp)`, small aggregates are classified by up to two
eight-byte words (including mixed integer/FP fields), and large aggregates use
a hidden result pointer.  See `docs/cc-internal-abi.md` for the exact rules and
the remaining `long double`/bit-field gaps.

## Explicitly Unsupported Features

These limitations exist to keep the implementation sufficiently small:

- Flexible array members
- By-value `struct`/`union` arguments through `...`
- Chained designators, mixing with complex brace elision
- Global aggregate copy initialization
- Dynamic libraries/shared objects and linker relaxation
- 128-bit `long double` (currently represented as `double`)
- GCC-identical bit-field layout for unusual mixed base types
- Hosted libc startup/header integration: `-lc -lgcc` archive linking works,
  but the default startup path and libc headers are the next phase

## Implementation Structure

`bin/cc/` is divided by stage:

- `brkcc.h`: shared types, symbol tables, and function declarations between modules
- `util.c`: memory allocation, error handling, and byte buffers
- `lexer.c`: integer/character/string literals, identifiers, operators, comments, escapes
- `preprocess.c`: macro tables, include search, conditional compilation, `#if` expression evaluation
- `types.c`: `struct type`, `struct node`, global/local symbols, and scopes
- `emit.c`: instruction encoding, pseudo-instructions, labels, relocations and
  the structured instruction-record list
- `asm.c`: assembler finalization and the `-S` human-readable listing
- `parser.c`: declaration, initializer, statement, and expression parsing
- `codegen.c`: stack-based evaluation; expression results are placed in `a0`, and lvalue addresses are placed in `a0`
- `link.c`: merges text/data/bss from multiple units, resolves symbols, patches relocations and writes the executable
- `elf.c`: standard ELF64 `ET_REL` writer and general ELF input reader
- `archive.c`: static `ar` archive reader and on-demand member extraction
- `obj.c`: legacy CCO2 object serialization/deserialization (CCO1 reader compatible)
- `main.c`: command-line driver and ELF output entry point
- `include/cc.h`: the runtime header for compiled programs—a raw-syscall subset with no `libc.a` dependency; installed into the rootfs as `/usr/include/cc.h`, the only userland header the image ships

To avoid the RISC-V `B-type` branch's ±4KB range limitation, conditional jumps are uniformly generated as:

```asm
b<inverse> +8
jal x0, target
```

Function calls use `jal ra, target`; calls through function pointers use `jalr ra, 0(t2)`.
Global variable/string addresses use `lui + addi` HI20/LO12 relocations, patched uniformly at link time.

## Development and Testing

Build only the host version (for quickly inspecting generated ELF files):

```sh
make -C brk-user cc-host
./brk-user/build/cc-host -I brk-user/usr/include \
    -o /tmp/hello.elf brk-user/tests/cc/hello.c
riscv64-unknown-elf-readelf -h -l /tmp/hello.elf
```

`make cc-tests` boots BRK in QEMU and runs the on-target sources;
`make cc-link-tests` runs the host-side object/relocation/linker checks.
The linker fixture `tests/cc/link_tests.sh` covers ELF object structure,
standard relocations, CCO1/CCO2 compatibility, malformed objects,
duplicate/undefined symbols and executable ELF layout.

`tests/cc/` contains verified examples:

| File                             | Coverage                                                                        | Expected Output                      |
| -------------------------------- | ------------------------------------------------------------------------------- | ------------------------------------ |
| `hello.c`                        | Basic output                                                                    | `hello from cc`                      |
| `smoke.c`                        | Global arrays, loops, recursion, `putnum`                                       | `sum=` and `30`                      |
| `expr.c`                         | Pointers, compound assignment, `++/--`, `sizeof`, `argc`                        | Multiple lines of numbers            |
| `io.c`                           | `open/read/write/close`, conversions, bitwise operations                        | Multiple lines of numbers, `file-io` |
| `long.c`                         | 64-bit constants and `long/unsigned long`                                       | `1118`                               |
| `large.c`                        | Large stack frames, local offsets exceeding 12 bits                             | `999` and `512`                      |
| `pp.c`                           | Macros, conditional compilation, `#include`                                     | `pp-ok`, `43`, `1`, `45`             |
| `struct.c`                       | struct/union/enum/typedef, member access, struct copy                           | Multiple lines of numbers, `DCBA`    |
| `funcptr.c`                      | Function pointers, arrays of function pointers, returning function pointers     | Multiple lines of numbers            |
| `init.c`                         | Local array/struct initialization, nested initialization, string initialization | Multiple lines of numbers            |
| `ginit.c`                        | Global struct/array/pointer/function pointer initialization                     | Multiple lines of numbers            |
| `designated.c`                   | Designated initializers, brace elision, nested struct initialization            | Multiple lines of numbers            |
| `macros.c`                       | Function-like macros, variadic macros, nested expansion                         | Multiple lines of numbers            |
| `control.c`                      | `switch`/`case`/`default`/fallthrough/`goto`                                    | Multiple lines of numbers            |
| `varargs.c`                      | `va_list`/`va_start`/`va_arg`/`va_copy`, variadic function pointers             | Multiple lines of numbers            |
| `float.c`                        | Floating arithmetic, comparisons, casts, compound assignment, conditions        | `4 5 7 4 9`, etc.                    |
| `float_cast.c`                   | Toward-zero float casts and local double negation                               | `-2 1 -1`, `1 1`                     |
| `gaddr.c`                        | Constant address initializers (`&a[i]`, pointer + offset, `&s.field`)           | `20 30 30 6`                         |
| `blockfn.c`                      | Block-scope function declarations and function pointers                         | `5 6 7`                              |
| `byvalue.c`                      | Aggregate parameters/returns by value, large structs                            | `7`, `10`, `7 10`, etc.              |
| `bitfield.c`                     | Bit-field load/store/RMW, signed and wide fields                                | Multiple lines of numbers            |
| `abi.c`                         | LP64D FP/aggregate/stack argument and return classification                     | `3 1 2 3`, `12`, `66`, etc.          |
| `stdarg.c`                      | GCC-compatible `<stdarg.h>` int/long/double varargs                             | `10 14`                              |
| `args.c`                         | More than eight arguments, stack and float/aggregate slots, varargs             | `55`, `55`, `39`, `43`, `10`, `45`   |
| `agg_deep.c`                     | Aggregate return with hidden slots plus a ninth stack argument                  | `10 35`, `30 105`, `10 35`           |
| `pp_float.c`                     | Decimal floating literals and suffixes                                          | `pp_float ok`                        |
| `pp_line.c`                      | `#pragma once`, `#line`, predefined macros                                      | `pp_line ok`                         |
| `front_float.c`                  | Front-end floating conversions, globals, loops and truth                        | Multiple lines of numbers            |
| `front_aggregate.c`              | Aggregate parameters, returns, function pointers, unions                        | Multiple lines of numbers            |
| `front_bitfield.c`               | Bit-field layout, unions, zero-width and padding                                | Multiple lines of numbers            |
| `front_static.c`                 | Static locals, nested statics, extern locals                                    | Multiple lines of numbers            |
| `front_manyargs.c`               | Nine-plus parameters and mixed argument slots                                   | `55`, `45`, `78`, `550`, `155`, `24` |
| `link_multi_a.c` + `link_multi_b.c` | Cross-unit data relocations, bss, static name collisions                        | `link`, `42`, etc.                   |
| `multi_a.c` + `multi_b.c`        | Multi-file linking, `extern`, `static` internal linkage                         | `multi`, `5`, `101`, `1`, `2`, `7`   |
| `multi_a.c` + `multi_b.c` (`-c`) | Separate compilation, object files, local symbols                               | Same as above                        |

After modifying the compiler, it is recommended to run at least `tests/cc/expr.c`, `tests/cc/struct.c`, `tests/cc/funcptr.c`, `tests/cc/init.c`, and `tests/cc/multi_a.c tests/cc/multi_b.c`.
