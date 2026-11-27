# AGENTS.md

BRK userland: C library, command helpers, user commands, an on-target C
compiler, and rootfs/test tooling. The kernel is a sibling checkout at
`../brk` (`KERNEL_ROOT`); rootfs packaging uses `../brk-tools`
(`TOOLS_ROOT`). `README.md` is the authoritative overview; `make help`
lists targets.

## Build and verify

- `make` builds every command; `make lib` builds only the archives;
  `make list-progs` lists discovered commands.
- `make check` is host-only: builds `tests/libc`, runs float validation
  (`tests/host`) and cc linker tests (`tests/cc/link_tests.sh`). It does
  not boot BRK or execute the libc tests.
- `make check-in-system` boots QEMU and runs `/usr/bin/brktest all`;
  `make cc-tests` boots and compiles/executes `tests/cc`.
- Focused runs: `scripts/run-brktest.sh file.basic` (one guest case),
  `scripts/run-cc-tests.sh tests/cc/expr.c` (one cc fixture),
  `make host-tests`, `make cc-link-tests`.
- Requires `riscv64-unknown-elf-*` (auto-detected; override
  `CROSS_COMPILE=`) and `qemu-system-riscv64`. Rootfs builds also need
  `brkfs` from `../brk-tools`, built automatically if missing.
- Every build, including `make check`, stages UAPI headers from
  `$(KERNEL_ROOT)/include/uapi`; the kernel checkout is required.
- Builds live in `build/<BUILD>` (`debug` default, `release`).
  `build/cc-host` is the host compiler and is not per-variant. Userland
  `BUILD` and kernel `KERNEL_BUILD` are independent; a release boot needs
  both.
- Objects depend on `build/<BUILD>/.build-flags`; changing any flag or
  compiler rebuilds the whole variant.

## Layout and wiring

- Install path is source path: `bin/` -> `/bin`, `sbin/` -> `/sbin`,
  `usr/bin/` -> `/usr/bin`. A command is `bin/foo.c` or `bin/foo/**/*.c`;
  the directory basename is the binary name, which must be unique across
  the three roots. Do not put non-command directories there.
- `usr/lib/libc` becomes `libc.a` (math included); `usr/lib/libcmdfw`
  becomes `libcmdfw.a`. Programs link `libcmdfw.a` before `libc.a`.
- `usr/include` is the public header tree; POSIX wrappers compose
  `syscall(SYS_*)` from kernel UAPI. Kernel UAPI headers are staged into
  `build/include/uapi`; include them as `<uapi/...>`. Kernel
  implementation headers are never available.
- `tests/libc/*.c` are auto-built and installed as `/home/tests/libc/`;
  `tests/cc/*.c` are copied to `/home/tests/cc` and compiled in the guest;
  `tests/host` is host-only.
- `dyn/` builds `ld-brk.so`, `libc.so` and loader fixtures with
  `riscv64-linux-gnu-*` (the Newlib toolchain cannot emit shared objects).
  Keep it separate from the static command path; `make rootfs` depends on
  these artifacts.
- `vendor/lua-5.5.1` is a byte-for-byte upstream copy installed as
  `/bin/lua`; never edit it. The build only skips `luac.c` and adds
  `CFLAGS_lua := -Wno-error`.

## Conventions

- Linux kernel C style: 8-column tabs, 80 columns, function braces on
  their own line. `.clang-format` is authoritative; warnings are errors
  (`-Werror`).
- Per-program flags use `CFLAGS_<prog>` in the Makefile; add any new
  entry to `FLAGS_SIGNATURE` so flag changes invalidate the build.
- Commit messages follow Conventional Commits, enforced by
  `.githooks/commit-msg`; enable with `make hooks`. Subject at most 72
  characters, no trailing period.
- `etc/shadow` hashes are committed test credentials, not secrets; do not
  add real passwords.
- `bin/cc` is a limited on-target C compiler; keep `tests/cc` fixtures
  inside the subset documented in `bin/cc/README.md`, update its
  capability/limitation lists when behavior changes, and keep
  `tests/cc/README.md` outputs current. Hosted links are the default;
  `-ffreestanding` is only for raw-syscall fixtures. Treat
  `docs/cc-thread-contract.md` as partly historical and verify against
  the Makefile and current sources.
