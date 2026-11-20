# BRK Userland

BRK userland provides the user-space programs, C library, command helpers and
tests used by the [BRK kernel](https://github.com/zhongyuan3/brk).  The kernel
source tree is expected at `../brk` by default, but its location is
configurable with `KERNEL_ROOT`.

## Layout

```text
src/
├── brksh/              # the shell (installed as sh)
├── brkcc/              # on-target C compiler (installed as cc)
├── brktest/            # libc/system test harness
├── cmd/                # single-file user commands
├── libbrkc/            # BRK C library (libc API)
├── libbrkc/math/       # double-precision libm (vendored from musl)
├── libbrkcmd/          # command helper library (brk_cmd_* API)
├── include/            # public headers
├── include/brk/cmd.h   # command helper API
└── user.ld.S           # user program linker script
vendor/
└── lua/                # vendored Lua 5.5.1 (installed as lua)
tests/
├── cc/                 # sources compiled by the on-target cc
├── libc/               # tests linked against libuser.a
└── lua/                # smoke test run by the vendored interpreter
```

A command is either a single source file (`src/cmd/foo.c`) or a directory of
sources (`src/cmd/foo/**/*.c`).  The build system links all sources in a
command directory into a single executable named after that directory.

## Requirements

- A RISC-V 64 cross toolchain, for example `riscv64-unknown-elf-`
- QEMU system mode for running BRK
- The BRK kernel source tree, by default at `../brk`
- `brk-tools` for rootfs image creation, by default at `../brk-tools`
- A host C compiler for `cc-host`
- GDB for the GDB targets
- `dtc` for the device-tree source target

## Build

```sh
# Build all user commands
make

# Build libuser.a only
make lib

# Build the host-side version of cc
make cc-host

# Build the libc tests under tests/libc
make tests

# List all discovered commands
make list-progs

# Build the kernel (debug by default)
make kernel CROSS_COMPILE=riscv64-unknown-elf-

# Show targets and configurable variables
make help
```

Useful variables:

| Variable              | Description                                      |
| --------------------- | ------------------------------------------------ |
| `CROSS_COMPILE`       | Cross-toolchain prefix                           |
| `BUILD_ROOT`          | Userland/rootfs build directory, default `build` |
| `KERNEL_ROOT`         | Kernel tree, default `../brk`                    |
| `KERNEL_ARCH`         | Kernel architecture, default `riscv`             |
| `KERNEL_BUILD`        | Kernel build type, default `debug`               |
| `KERNEL_BUILD_ROOT`   | Kernel build directory, derived from the above   |
| `KERNEL_MAKE_ARGS`    | Extra arguments passed to the kernel make        |
| `ROOTFS_IMG`          | Rootfs image path, default `build/rootfs.img`    |
| `M` / `SMP`           | QEMU RAM size / CPU count, defaults `128M` / `3` |
| `QEMU` / `GDB`        | Tool paths used by the boot/debug targets        |
| `EXTRA_QEMU_OPTS`     | Extra QEMU arguments                             |
| `HOSTCC`              | Host C compiler for `cc-host`                    |
| `LIBGCC`              | Optional libgcc fallback                         |

The build copies the kernel UAPI headers from
`$(KERNEL_ROOT)/include/uapi` into the private build include directory.
Kernel implementation headers are not exposed to userland.

## Rootfs and QEMU

The rootfs, QEMU boot and GDB workflows live in this repository:

```sh
# Build or force-regenerate the rootfs image
make rootfs CROSS_COMPILE=riscv64-unknown-elf-

# Build kernel + rootfs and boot in QEMU
make run CROSS_COMPILE=riscv64-unknown-elf-

# Additional boot/debug helpers
make debug CROSS_COMPILE=riscv64-unknown-elf-
make gdb-server CROSS_COMPILE=riscv64-unknown-elf-
make gdb-client CROSS_COMPILE=riscv64-unknown-elf-
make dtb
make dts
```

`run`, `debug` and `gdb-server` build the kernel through `KERNEL_ROOT` before
starting QEMU. Use `KERNEL_BUILD=release` to boot a release kernel. The rootfs
image is written to `build/rootfs.img` by default.

The rootfs script builds the user commands and libc tests, then installs:

- `init` under `/sbin`
- test and system tools such as `brktest`, `stat` and `httpd` under `/usr/bin`
- other commands under `/bin`
- `/etc` configuration files
- `tests/` under `/home/tests`
- libc test binaries under `/home/tests/libc`
- `src/brkcc/include/cc.h` under `/usr/include/cc.h`

`KERNEL_ROOT` and `TOOLS_ROOT` can be overridden when the kernel or
`brk-tools` repositories are checked out elsewhere.

## Tests

The `cc` tests are ordinary source files copied into the rootfs.  Compile and
run them inside BRK with the on-target compiler:

```sh
cc /home/tests/cc/hello.c -o /tmp/hello
/tmp/hello

cc /home/tests/cc/multi_a.c /home/tests/cc/multi_b.c -o /tmp/multi
/tmp/multi
```

See [`tests/cc/README.md`](tests/cc/README.md) for the full list and expected
outputs.  The compiler implementation is documented in
[`src/brkcc/README.md`](src/brkcc/README.md).

Libc tests are linked into `libuser` programs and installed as executables:

```sh
/home/tests/libc/alloc
```
The allocator test covers small and large allocations, `calloc`, `realloc`,
coalescing, invalid sizes and concurrent allocation.  See
[`tests/libc/README.md`](tests/libc/README.md) for details.

### Lua

`src/cmd/lua` vendors Lua 5.5.1 unmodified and links it statically against
`libuser.a`; the build installs it as `/bin/lua`.  It exercises the whole
floating-point stack: the vendored musl-derived libm, correctly rounded
`strtod` and printf conversions, `setjmp`-based error handling and the
kernel's FP context switching.  The in-system smoke test:

```sh
lua /home/tests/lua/smoke.lua
```

The math, strtod and printf-float engines are pure arithmetic and are
validated on the host against glibc (2 ULP bound) by:

```sh
scripts/math_host_test.sh
```

The libc and system test harness can also be run directly:

```sh
/usr/bin/brktest functional
/usr/bin/brktest all
```

See [`src/brktest/README.md`](src/brktest/README.md) for suite names,
options and stress-test usage.

## License

This project is licensed under the MIT License.  See [LICENSE](LICENSE) for
details.
