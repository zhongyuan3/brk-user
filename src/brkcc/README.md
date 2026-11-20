# cc — A Lightweight User-Space C Compiler for BRK

`src/brkcc/` is a small C compiler that runs in BRK/RISC-V64 user space. It does not depend on the target machine's `as`/`ld`; instead, it performs lexing, preprocessing, parsing, code generation, linking, and ELF writing itself. The compiler itself still uses this repository's `libuser.a`, but **the generated programs do not link against libuser**—they depend only on kernel system calls.

## Quick Start

Build user programs (including `/bin/cc`):

```sh
make -C brk-user
```

Regenerate the rootfs (this installs `cc` to `/bin/cc`, `src/brkcc/include/cc.h` to `/usr/include/cc.h`, and copies `tests/` to `/home/tests`):

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
cc [-c] [-E] [-o output] [-I dir] [-D name[=value]] [-U name] input.c|input.o [...]
```

- If `-o` is not specified, the output is `a.out`
- Multiple `.c` files are compiled separately within the same invocation and then linked into a single executable
- `-c` compiles only, without linking, using the built-in `CCO1` object format
- `.o` inputs are passed to the linker together with `.c` inputs
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
- `-D name[=value]`, `-U name`, `-E`

Not supported:

- `#` stringification, `##` token pasting
- `#include` followed by a macro name
- Macro invocations spanning physical lines (a macro invocation must be within the same logical line)
- Complex standard headers (the target machine provides only a minimal `src/brkcc/include/cc.h`)

## Supported C Subset

Types:

- `void`, `char`, `short`, `int`, `long`
- `signed` / `unsigned`
- Pointers, arrays (including multidimensional arrays), functions, function pointers
- `struct`, `union`, `enum`, `typedef`
- `char` is **unsigned** by default (RISC-V psABI); `signed char` explicitly declares it signed

Declarations:

- Global variables, local variables, function definitions, function prototypes
- `extern` global declarations and `static` file-scope symbols
- Multiple declarators in one declaration, e.g. `int a, *p, b[4];`
- Declarations in `for (int i = 0; ...)`
- Calls to functions defined later (the compiler creates implicit function symbols)
- Function pointers, e.g. `int (*fp)(int, int);`
- Functions returning function pointers, e.g. `int (*pick(int))(int, int);`
- Arrays of function pointers, function pointer members in structs

Statements:

- Blocks, `if/else`, `while`, `do-while`, `for`
- `switch` / `case` / `default`, with fallthrough and nested switch support
- `goto` and labels
- `return`, `break`, `continue`
- Expression statements, empty statements

Expressions:

- Arithmetic: `+ - * / %`
- Bitwise: `& | ^ << >> ~`
- Comparison: `== != < <= > >=`
- Logical: `&& || !`
- Assignment, compound assignment: `= += -= *= /= %= &= |= ^= <<= >>=`
- Increment/decrement: `++ --` (prefix/postfix)
- Ternary, comma, casts, `sizeof`
- Address-of `&`, dereference `*`, array subscripting
- Member access `.` and `->`
- Direct calls and indirect calls through function pointers (up to 8 arguments)
- Variadic functions: `...`, `va_list`, `va_start`, `va_arg`, `va_end`, `va_copy`

Initializers:

- Global scalars: constant expressions
- Global `char[]`: string literals
- Global arrays: `{ ... }` constant lists (can infer `[]` length automatically)
- Global `struct` / `union`: `{ ... }` constant lists, with nesting support
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
- Bit-fields, flexible array members

Built-in functions (if the user has not defined functions with the same names):

```c
int write(int fd, const void *buf, long n);
int read(int fd, void *buf, long n);
int open(const char *path, int flags, int mode);
int close(int fd);
void exit(int status);
int putchar(int c);
int puts(const char *s);
```

`src/brkcc/include/cc.h` provides these prototypes as well as `NULL`, `EOF`, and `O_*` constants.

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

Object files use the compiler's private `CCO1` format, not standard ELF `.o`; they are used only for this compiler's linking stage.

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

- Standard ELF `.o` / interoperability with the system linker
- Static libraries/dynamic libraries
- Linker scripts/custom sections

## Generated Artifact ABI

Generated programs are:

- `ELF64`, `ET_EXEC`, little-endian, `e_machine = EM_RISCV`
- A single `PT_LOAD` with permissions `R|W|X`
- Fixed load base address `0x10000`, with code starting at file offset `0x1000`
- The entry point is the compiler-generated `__cc_start`
- `__cc_start` receives `a0=argc, a1=argv, a2=envp` per the kernel convention, calls `main`, and then exits via `SYS_exit`
- System call numbers use BRK's POSIX.1-2024 numbering: `read=0, write=1, open=2, close=3, exit=49`

`puts`, struct copies, and aggregate initialization zeroing use `__cc_puts`, `__cc_memcpy`, and `__cc_memset`, respectively.

## Explicitly Unsupported Features

These limitations exist to keep the implementation sufficiently small:

- `float` / `double`
- Passing/returning `struct` / `union` by value
- Bit-fields, flexible array members
- `double` and by-value `struct` arguments in variadic functions
- Macro `#` stringification, `##` token pasting
- Chained designators, mixing with complex brace elision
- Global aggregate copy initialization
- `static` local variables (reported as an error)
- Standard ELF `.o`, static libraries/dynamic libraries

In addition, the kernel currently does not save floating-point context, so generated code uses only integer instructions.

## Implementation Structure

`src/brkcc/` is divided by stage:

- `brkcc.h`: shared types, symbol tables, and function declarations between modules
- `util.c`: memory allocation, error handling, and byte buffers
- `lexer.c`: integer/character/string literals, identifiers, operators, comments, escapes
- `preprocess.c`: macro tables, include search, conditional compilation, `#if` expression evaluation
- `types.c`: `struct type`, `struct node`, global/local symbols, and scopes
- `emit.c`: `enc_r/enc_i/enc_s/enc_b/enc_u/enc_j`, pseudo-instructions, labels, and relocations
- `parser.c`: declaration, initializer, statement, and expression parsing
- `codegen.c`: stack-based evaluation; expression results are placed in `a0`, and lvalue addresses are placed in `a0`
- `link.c`: merges text/data/bss from multiple units, resolves symbols, and patches relocations
- `main.c`: command-line driver and ELF output entry point

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
./brk-user/build/cc-host -I brk-user/src/include \
    -o /tmp/hello.elf brk-user/tests/cc/hello.c
riscv64-unknown-elf-readelf -h -l /tmp/hello.elf
```

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
| `multi_a.c` + `multi_b.c`        | Multi-file linking, `extern`, `static` internal linkage                         | `multi`, `5`, `101`, `1`, `2`, `7`   |
| `multi_a.c` + `multi_b.c` (`-c`) | Separate compilation, object files, local symbols                               | Same as above                        |

After modifying the compiler, it is recommended to run at least `tests/cc/expr.c`, `tests/cc/struct.c`, `tests/cc/funcptr.c`, `tests/cc/init.c`, and `tests/cc/multi_a.c tests/cc/multi_b.c`.
