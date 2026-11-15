# BRK Userland

BRK userland provides the user-space programs, C library, command helpers and
tests used by the [BRK kernel](https://github.com/zhongyuan3/brk).  The kernel
source tree is expected at `../brk` by default, but its location is
configurable with `KERNEL_ROOT`.

## Layout

```text
src/
├── cmd/                # user commands
│   ├── *.c             # single-file commands
│   ├── cc/             # on-target C compiler
│   ├── sh/             # shell
│   └── brktest/        # libc/system test harness
├── libbrkc/            # BRK C library (libc API)
├── libbrkcmd/          # command helper library (brk_cmd_* API)
├── include/            # public headers
├── include/brk/cmd.h   # command helper API
└── user.ld.S           # user program linker script
tests/
├── cc/                 # sources compiled by the on-target cc
└── libc/               # tests linked against libuser.a
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

# Show targets and configurable variables
make help
```

Useful variables:

| Variable              | Description                                      |
| --------------------- | ------------------------------------------------ |
| `CROSS_COMPILE`       | Cross-toolchain prefix                           |
| `BUILD_ROOT`          | Build directory, default `build`                 |
| `KERNEL_ROOT`         | Kernel tree providing UAPI headers, default `../brk` |
| `HOSTCC`              | Host C compiler for `cc-host`                    |
| `LIBGCC`              | Optional libgcc fallback                         |

The build copies the kernel UAPI headers from
`$(KERNEL_ROOT)/src/include/uapi` into the private build include directory.
Kernel implementation headers are not exposed to userland.

## Rootfs and QEMU

The rootfs is normally built through the kernel tree, which invokes
`scripts/mkrootfs.sh`:

```sh
make -C ../brk rootfs CROSS_COMPILE=riscv64-unknown-elf-
make -C ../brk run CROSS_COMPILE=riscv64-unknown-elf-
```

The rootfs script builds the user commands and libc tests, then installs:

- `init` under `/sbin`
- test and system tools such as `brktest`, `stat` and `httpd` under `/usr/bin`
- other commands under `/bin`
- `/etc` configuration files
- `tests/` under `/home/tests`
- libc test binaries under `/home/tests/libc`
- `src/include/cc.h` under `/usr/include/cc.h`

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
[`src/cmd/cc/README.md`](src/cmd/cc/README.md).

Libc tests are linked into `libuser` programs and installed as executables:

```sh
/home/tests/libc/alloc
```

The allocator test covers small and large allocations, `calloc`, `realloc`,
coalescing, invalid sizes and concurrent allocation.  See
[`tests/libc/README.md`](tests/libc/README.md) for details.

The libc and system test harness can also be run directly:

```sh
/usr/bin/brktest functional
/usr/bin/brktest all
```

See [`src/cmd/brktest/README.md`](src/cmd/brktest/README.md) for suite names,
options and stress-test usage.

## License

This project is licensed under the MIT License.  See [LICENSE](LICENSE) for
details.
