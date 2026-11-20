# BRK Userland

BRK userland provides the user-space programs, C library, command helpers and
tests used by the [BRK kernel](https://github.com/zhongyuan3/brk).  The kernel
source tree is expected at `../brk` by default, but its location is
configurable with `KERNEL_ROOT`.

## Layout

```text
bin/                    # commands installed under /bin (sh/, cc/, *.c)
sbin/                   # commands installed under /sbin (init/)
usr/
├── bin/                # commands installed under /usr/bin (brktest/, *.c)
├── include/            # public headers (brk/cmd.h, cc.h, POSIX headers)
└── lib/
    ├── libc/           # BRK C library (libc API; math/ vendored from musl)
    └── libcmd/         # command helper library (brk_cmd_* API)
vendor/
└── lua/                # vendored Lua 5.5.1 (installed as lua)
tests/                  # host float validation + cc/libc/lua suites
etc/                    # copied into the rootfs as /etc
scripts/                # rootfs packaging and host-side utilities
user.ld.S               # user program linker script
```

The command tree mirrors the root filesystem: a command under `bin/` is
installed to `/bin`, one under `usr/bin/` to `/usr/bin`, `sbin/` to
`/sbin`.  There is no packaging whitelist—the location *is* the rule.
`vendor/`, `tests/` and `scripts/` are host-side assets organized by
role instead of install location.

The `libc` and `libcmd` sources are archived into separate `libc.a` and
`libcmd.a` archives; every user program links both.  `make lib` builds
them.  A future on-target toolchain would install `usr/include` and
the archives under `/usr` the same way.

A command is either a single source file (`bin/foo.c`) or a directory
of sources (`bin/foo/**/*.c`).  The build system links all sources in a
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

# Build libc.a and libcmd.a only
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

- commands at their mirrored paths: `bin/` under `/bin`, `sbin/` under
  `/sbin`, `usr/bin/` under `/usr/bin`
- `test` a second time as `/bin/[`, and vendored `lua` under `/bin/lua`
- `/etc` configuration files
- `tests/` under `/home/tests`
- libc test binaries under `/home/tests/libc`
- `usr/include/cc.h` under `/usr/include/cc.h`

`KERNEL_ROOT` and `TOOLS_ROOT` can be overridden when the kernel or
`brk-tools` repositories are checked out elsewhere.

### Accounts and password hashes

`etc/passwd`, `etc/group` and `etc/shadow` seed the rootfs `/etc`.  The
`shadow` hashes use the BRK `$sha256$` scheme, `hex(SHA256(salt + password))`;
`scripts/pwutil.py` generates, verifies and updates them:

```sh
scripts/pwutil.py list etc/shadow
scripts/pwutil.py passwd user   # prompts, then updates etc/shadow
```

Since `etc/shadow` is committed with seeded hashes for the preconfigured
accounts, treat them as public test credentials, not secrets.

## Tests

The suites live in different places because they run differently:

| Suite                           | Sources       | Runs        | Entry point |
| ------------------------------- | ------------- | ----------- | ----------- |
| libc/system functional + stress | `usr/bin/brktest/` | inside BRK | `/usr/bin/brktest all` |
| libc unit tests                 | `tests/libc`  | inside BRK  | `/home/tests/libc/alloc` |
| cc tests                        | `tests/cc`    | inside BRK  | compile with the on-target cc |
| Lua smoke test                  | `tests/lua`   | inside BRK  | `lua /home/tests/lua/smoke.lua` |
| float engine validation         | `tests/host`  | on the host | `make host-tests` |

Two targets wrap these entry points: `make check` builds the libc tests
and runs the host float validation without booting anything, and
`make check-in-system` boots BRK in QEMU and runs the brktest suites end
to end (it rebuilds the kernel and rootfs as needed).

### brktest

The libc and system test harness is installed under `/usr/bin` and runs
TAP-style suites inside BRK:

```sh
/usr/bin/brktest functional
/usr/bin/brktest all
```

See [`usr/bin/brktest/README.md`](usr/bin/brktest/README.md) for suite names,
options and stress-test usage.  `scripts/run-brktest.sh` boots BRK in QEMU,
runs a target inside the guest and reports whether it passed:

```sh
scripts/run-brktest.sh all
```

### cc tests

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
[`bin/cc/README.md`](bin/cc/README.md).

### libc tests

Libc tests are ordinary programs linked against `libc.a` and installed as
executables:

```sh
/home/tests/libc/alloc
```

The allocator test covers small and large allocations, `calloc`, `realloc`,
coalescing, invalid sizes and concurrent allocation.  See
[`tests/libc/README.md`](tests/libc/README.md) for details.

### Lua

`vendor/lua` hosts Lua 5.5.1 unmodified; the build links it statically
against `libc.a` and `libcmd.a` and installs it as `/bin/lua`.  It exercises
the whole
floating-point stack: the vendored musl-derived libm, correctly rounded
`strtod` and printf conversions, `setjmp`-based error handling and the
kernel's FP context switching.  The in-system smoke test:

```sh
lua /home/tests/lua/smoke.lua
```

### Host float validation

The math, strtod and printf-float engines are pure arithmetic with no OS
dependencies, so their host behavior transfers to the riscv64 build
unchanged.  `tests/host/run.sh` compiles them straight from the libc
sources and compares against glibc: math within 2 ULP, strtod for value,
endptr and errno agreement, printf byte-for-byte with `%a` checked by
round-trip.  No kernel boot is needed:

```sh
make host-tests
```

## License

This project is licensed under the MIT License.  See [LICENSE](LICENSE) for
details.
