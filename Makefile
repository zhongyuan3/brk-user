BUILD_ROOT ?= build
OBJDIR := $(BUILD_ROOT)/obj
BINDIR := $(BUILD_ROOT)/bin

ifeq ($(V),1)
Q :=
else
Q := @
endif

# The hooks target only touches Git configuration and must not require a
# cross compiler.
ifneq ($(MAKECMDGOALS),hooks)
ifndef CROSS_COMPILE
# Prefer the bare-metal toolchain.  Some riscv64-linux-gnu libgcc builds use
# Zcb instructions (e.g. c.lbu), which the QEMU rv64 CPU does not implement.
CROSS_COMPILE := $(shell \
if riscv64-unknown-elf-objdump -i 2>&1 | grep 'elf64-big' > /dev/null 2>&1; \
then echo 'riscv64-unknown-elf-'; \
elif riscv64-elf-objdump -i 2>&1 | grep 'elf64-big' > /dev/null 2>&1; \
then echo 'riscv64-elf-'; \
elif riscv64-none-elf-objdump -i 2>&1 | grep 'elf64-big' > /dev/null 2>&1; \
then echo 'riscv64-none-elf-'; \
elif riscv64-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' > /dev/null 2>&1; \
then echo 'riscv64-linux-gnu-'; \
elif riscv64-unknown-linux-gnu-objdump -i 2>&1 | grep 'elf64-big' > /dev/null 2>&1; \
then echo 'riscv64-unknown-linux-gnu-'; \
else echo "***" 1>&2; \
echo "*** Error: Cross compiler not found" 1>&2; \
echo "***" 1>&2; \
exit 1; \
fi)
endif
endif

KERNEL_ROOT ?= ../brk
KERNEL_ARCH ?= riscv
KERNEL_BUILD ?= debug
KERNEL_BUILD_ROOT ?= $(KERNEL_ROOT)/build/$(KERNEL_ARCH)/$(KERNEL_BUILD)
KERNEL_ELF := $(KERNEL_BUILD_ROOT)/brk
KERNEL_MAKE_ARGS ?=

UAPI_SRC := $(KERNEL_ROOT)/include/uapi
UAPI_DIR := $(BUILD_ROOT)/include/uapi
UAPI_STAMP := $(UAPI_DIR)/.stamp
UAPI_HEADERS := $(wildcard $(UAPI_SRC)/*.h)

ROOTFS_IMG ?= $(BUILD_ROOT)/rootfs.img
MKROOTFS := scripts/mkrootfs.sh
M ?= 128M
SMP ?= 3
QEMU ?= qemu-system-riscv64
GDB ?= $(shell command -v gdb-multiarch >/dev/null 2>&1 && echo gdb-multiarch || echo $(CROSS_COMPILE)gdb)
EXTRA_QEMU_OPTS ?=

# Inputs that determine the rootfs contents.  Rebuild the image when the
# packaging script, userland sources, headers or binaries change.
ROOTFS_DEPS := $(MKROOTFS) Makefile $(UAPI_HEADERS) \
	$(wildcard $(BINDIR)/*) \
	$(shell find bin sbin usr etc tests vendor user.ld.S -type f \
		2>/dev/null)

QEMU_OPTS += -machine virt -nographic
QEMU_OPTS += -m $(M)
QEMU_OPTS += -smp $(SMP)
QEMU_OPTS += -bios default
QEMU_OPTS += -drive file=$(ROOTFS_IMG),if=none,format=raw,id=x0
QEMU_OPTS += -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
QEMU_OPTS += -global virtio-mmio.force-legacy=false
QEMU_OPTS += -netdev user,id=net0
QEMU_OPTS += -device virtio-net-device,netdev=net0,bus=virtio-mmio-bus.1
QEMU_OPTS += $(EXTRA_QEMU_OPTS)

DTB := $(BUILD_ROOT)/qemu_virt.dtb
DTS := $(BUILD_ROOT)/qemu_virt.dts

CC := $(CROSS_COMPILE)gcc
CPP := $(CC) -E
AR := $(CROSS_COMPILE)ar
RANLIB := $(CROSS_COMPILE)ranlib
HOSTCC ?= cc

CFLAGS := -std=gnu11
CFLAGS += -O2
CFLAGS += -Wall -Wextra -Werror
CFLAGS += -march=rv64gc
CFLAGS += -ffreestanding -nostdinc
CFLAGS += -fno-stack-protector
CFLAGS += -fno-pie
CFLAGS += -I./usr/include -I$(BUILD_ROOT)/include
CFLAGS += -MMD -MP

LDFLAGS := -nostdlib -no-pie -z max-page-size=4096 -static
# The printf engine renders %Lf by narrowing a long double, which the
# compiler implements with the __trunctfdf2 helper in libgcc.  The
# archive linker only pulls that one member on demand, so defaulting to
# -lgcc adds nothing to programs that never print long doubles.
LIBGCC ?= -lgcc
LDLIBS := $(LIBGCC)

HOST_CFLAGS ?= -std=gnu11 -O2 -Wall -Wextra -Werror
HOST_CFLAGS += -I$(BUILD_ROOT)/include

USER_LD_SOURCE := user.ld.S
USER_LD := $(BUILD_ROOT)/user.ld

# The two userland libraries are archived separately.  libcmdfw.a holds
# the command helpers, libc.a the C library; libcmdfw depends on libc, so
# programs link libcmdfw first and libc after it.  A future on-target
# toolchain would ship both archives from usr/lib into the image.
LIBC_DIR := usr/lib/libc
LIBCMD_DIR := usr/lib/libcmdfw
LIBC_SRCS := $(sort $(shell find $(LIBC_DIR) -name '*.c' -print))
LIBCMD_SRCS := $(sort $(shell find $(LIBCMD_DIR) -name '*.c' -print))
LIBC_OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(LIBC_SRCS))
LIBCMD_OBJS := $(patsubst %.c,$(OBJDIR)/%.o,$(LIBCMD_SRCS))
LIBC_A := $(BUILD_ROOT)/libc.a
LIBCMD_A := $(BUILD_ROOT)/libcmdfw.a
LIBS := $(LIBCMD_A) $(LIBC_A)

# The command tree mirrors the root filesystem: a command under bin/ is
# installed to /bin, one under usr/bin/ to /usr/bin, sbin/ to /sbin.
# A command is either a single source file (bin/foo.c) or a directory of
# sources (bin/foo/**/*.c), which keeps multi-file commands easy to
# navigate.  The binary name is the basename, so command names must be
# unique across the three roots.  The shell, the on-target compiler, the
# test harness and init are ordinary directory commands under bin/sh,
# bin/cc, usr/bin/brktest and sbin/init.
CMD_DIRS := bin usr/bin sbin
USER_SINGLE_SRCS := $(sort $(foreach d,$(CMD_DIRS),$(wildcard $(d)/*.c)))
USER_SINGLE_PROGS := $(basename $(notdir $(USER_SINGLE_SRCS)))
USER_DIRS := $(sort $(foreach d,$(CMD_DIRS),\
	$(patsubst %/,%,$(wildcard $(d)/*/))))
USER_DIR_PROGS := $(notdir $(USER_DIRS))
USER_DIR_SRCS := $(sort $(foreach d,$(USER_DIRS),\
	$(shell find $(d) -name '*.c' -print)))

# Vendored third-party sources live under vendor/ and are compiled with
# warnings visible but without -Werror, mirroring the kernel's vendor/
# policy. Lua 5.5.1 builds every file except luac.c, the standalone
# bytecode-compiler frontend with its own main().
LUA_DIR := vendor/lua
LUA_CFLAGS := $(filter-out -Werror,$(CFLAGS))
LUA_SRCS := $(sort $(filter-out $(LUA_DIR)/luac.c,\
	$(wildcard $(LUA_DIR)/*.c)))
LUA_OBJS := $(patsubst $(LUA_DIR)/%.c,$(OBJDIR)/vendor/lua/%.o,$(LUA_SRCS))

USER_PROGS := $(sort $(USER_SINGLE_PROGS) $(USER_DIR_PROGS)) lua
USER_BINS := $(addprefix $(BINDIR)/,$(USER_PROGS))
USER_OBJS := $(patsubst %.c,$(OBJDIR)/%.o,\
	$(USER_SINGLE_SRCS) $(USER_DIR_SRCS))
DEPS := $(LIBC_OBJS:.o=.d) $(LIBCMD_OBJS:.o=.d) $(USER_OBJS:.o=.d) \
	$(LUA_OBJS:.o=.d)

# libc tests are normal library-linked programs under tests/libc.
TEST_SRCS := $(sort $(wildcard tests/libc/*.c))
TEST_OBJS := $(patsubst tests/%.c,$(OBJDIR)/tests/%.o,$(TEST_SRCS))
TEST_BINS := $(patsubst tests/%.c,$(BUILD_ROOT)/tests/%,$(TEST_SRCS))
DEPS += $(TEST_OBJS:.o=.d)

# Keep object files around for debugging and incremental rebuilds.
.SECONDARY: $(LIBC_OBJS) $(LIBCMD_OBJS) $(USER_OBJS) $(TEST_OBJS) $(LUA_OBJS)

CC_HOST := $(BUILD_ROOT)/cc-host
CC_HOST_OBJDIR := $(BUILD_ROOT)/cc-host-obj
CC_HOST_SRCS := $(sort $(wildcard bin/cc/*.c))
CC_HOST_OBJS := $(patsubst %.c,$(CC_HOST_OBJDIR)/%.o,$(CC_HOST_SRCS))
DEPS += $(CC_HOST_OBJS:.o=.d)

.PHONY: all lib cc-host tests check check-in-system host-tests clean \
	help hooks list-progs kernel rootfs run debug gdb-server gdb-client \
	dtb dts

all: $(USER_BINS)

lib: $(LIBC_A) $(LIBCMD_A)

cc-host: $(CC_HOST)

tests: $(TEST_BINS)

# One entry point for validation.  check covers everything that runs
# without a kernel boot: the libc tests are built (they execute inside
# BRK) and the float engines are compared against glibc on the host.
check: tests host-tests

# Boots BRK in QEMU through scripts/run-brktest.sh, runs the brktest
# suites inside the guest and reports pass/fail.  Rebuilds the kernel
# and rootfs as needed.
check-in-system:
	$(Q)KERNEL_ROOT="$(abspath $(KERNEL_ROOT))" \
		CROSS_COMPILE="$(CROSS_COMPILE)" scripts/run-brktest.sh all

# Host-side validation of the libc floating-point stack: the engines
# are pure arithmetic, so host results transfer to the riscv64 build.
host-tests: $(UAPI_STAMP)
	$(Q)BUILD_ROOT="$(abspath $(BUILD_ROOT))" HOSTCC="$(HOSTCC)" \
		tests/host/run.sh

list-progs:
	@printf '%s\n' $(USER_PROGS)

kernel:
	$(Q)$(MAKE) -C $(KERNEL_ROOT) ARCH=$(KERNEL_ARCH) BUILD=$(KERNEL_BUILD) \
		CROSS_COMPILE=$(CROSS_COMPILE) $(KERNEL_MAKE_ARGS)

rootfs:
	$(Q)rm -f $(ROOTFS_IMG)
	$(Q)KERNEL_ROOT="$(abspath $(KERNEL_ROOT))" \
		BUILD_ROOT="$(abspath $(BUILD_ROOT))" \
		$(MKROOTFS) "$(ROOTFS_IMG)"

$(ROOTFS_IMG): $(ROOTFS_DEPS)
	$(Q)KERNEL_ROOT="$(abspath $(KERNEL_ROOT))" \
		BUILD_ROOT="$(abspath $(BUILD_ROOT))" \
		$(MKROOTFS) "$@"

run: $(ROOTFS_IMG) kernel
	$(Q)$(QEMU) $(QEMU_OPTS) -kernel $(KERNEL_ELF)

debug: $(ROOTFS_IMG) kernel
	$(Q)$(QEMU) $(QEMU_OPTS) -kernel $(KERNEL_ELF) -d int

gdb-server: $(ROOTFS_IMG) kernel
	$(Q)$(QEMU) $(QEMU_OPTS) -S -s -kernel $(KERNEL_ELF)

gdb-client: kernel
	$(Q)$(GDB) --tui -quiet -ex "target remote :1234" $(KERNEL_ELF)

dtb: $(DTB)

$(DTB):
	$(Q)mkdir -p $(dir $@)
	$(Q)$(QEMU) -M virt,dumpdtb=$@

dts: $(DTS)

$(DTS): $(DTB)
	$(Q)command -v dtc >/dev/null 2>&1 || \
		{ echo "error: dtc not found in PATH" >&2; exit 1; }
	$(Q)dtc -I dtb -O dts $< -o $@

hooks:
	@git config core.hooksPath .githooks
	@echo "  HOOK    core.hooksPath=.githooks"

help:
	@echo "Targets:"
	@echo "  all          Build all user programs (default)"
	@echo "  lib          Build libc.a and libcmdfw.a"
	@echo "  cc-host      Build the host-side cc helper"
	@echo "  tests        Build libc tests under tests/libc"
	@echo "  check        Build libc tests and run the host float validation"
	@echo "  check-in-system   Boot BRK in QEMU and run the brktest suites"
	@echo "  host-tests   Run the host float validation under tests/host"
	@echo "  list-progs   Print the names of all user programs"
	@echo "  kernel       Build the kernel (KERNEL_BUILD=debug|release)"
	@echo "  rootfs       Build the root filesystem image"
	@echo "  run          Build kernel + rootfs and boot in QEMU"
	@echo "  debug        Boot with QEMU interrupt tracing"
	@echo "  gdb-server   Start QEMU with a GDB stub"
	@echo "  gdb-client   Connect GDB to localhost:1234"
	@echo "  dtb / dts    Dump the QEMU virt device tree"
	@echo "  hooks        Enable the Conventional Commit hook"
	@echo "  clean        Remove $(BUILD_ROOT)"
	@echo "  help         Show this help"
	@echo ""
	@echo "Layout (mirrors the rootfs install paths):"
	@echo "  bin/                commands installed under /bin (incl. sh/, cc/)"
	@echo "  sbin/               commands installed under /sbin (init/)"
	@echo "  usr/bin/            commands installed under /usr/bin (brktest/)"
	@echo "  usr/include/        public headers"
	@echo "  usr/lib/libc/       BRK C library"
	@echo "  usr/lib/libcmdfw/     BRK command helpers"
	@echo "  vendor/lua/         vendored Lua 5.5.1 (installed as lua)"
	@echo ""
	@echo "Configurable variables:"
	@echo "  CROSS_COMPILE=<prefix>"
	@echo "  BUILD_ROOT=<path>       default: build"
	@echo "  KERNEL_ROOT=<path>      default: ../brk"
	@echo "  KERNEL_ARCH=<arch>      default: riscv"
	@echo "  KERNEL_BUILD=<type>     default: debug (debug|release)"
	@echo "  KERNEL_BUILD_ROOT=<path>  default: KERNEL_ROOT/build/ARCH/BUILD"
	@echo "  KERNEL_MAKE_ARGS=...    extra arguments passed to the kernel make"
	@echo "  ROOTFS_IMG=<path>       default: BUILD_ROOT/rootfs.img"
	@echo "  M=<size>                default: 128M"
	@echo "  SMP=<n>                 default: 3"
	@echo "  QEMU=<path>             default: qemu-system-riscv64"
	@echo "  GDB=<path>"
	@echo "  HOSTCC=<cc>             default: cc"

$(BUILD_ROOT):
	mkdir -p $@

$(BINDIR): | $(BUILD_ROOT)
	mkdir -p $@

# Copy only the kernel UAPI headers into a private include root: the
# kernel's implementation headers (everything outside include/uapi) are
# never exposed to userland builds.
$(UAPI_STAMP): $(UAPI_SRC) $(UAPI_HEADERS) | $(BUILD_ROOT)
	$(RM) -rf $(UAPI_DIR)
	mkdir -p $(UAPI_DIR)
	cp $(UAPI_HEADERS) $(UAPI_DIR)/
	touch $@

$(USER_LD): $(USER_LD_SOURCE) | $(BUILD_ROOT)
	$(CPP) $(CFLAGS) -P -o $@ $<

$(OBJDIR)/%.o: %.c | $(UAPI_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/tests/%.o: tests/%.c | $(UAPI_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(CC_HOST_OBJDIR)/%.o: %.c $(UAPI_STAMP) | $(UAPI_STAMP)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_CFLAGS) -MMD -MP -c -o $@ $<

$(LIBC_A): $(LIBC_OBJS)
	$(RM) $@
	$(AR) rcs $@ $^
	$(RANLIB) $@

$(LIBCMD_A): $(LIBCMD_OBJS)
	$(RM) $@
	$(AR) rcs $@ $^
	$(RANLIB) $@

# $(1) command name, $(2) source root (bin, usr/bin, sbin)
define ADD_SINGLE_PROGRAM
$(BINDIR)/$(1): $(OBJDIR)/$(2)/$(1).o $(LIBS) $(USER_LD) | $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -T $(USER_LD) -o $$@ $$< $(LIBS) $(LDLIBS)
endef

# $(1) command name, $(2) source root
define ADD_DIR_PROGRAM
$(BINDIR)/$(1): $(patsubst %.c,$(OBJDIR)/%.o,$(filter $(2)/$(1)/%,$(USER_DIR_SRCS))) $(LIBS) $(USER_LD) | $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -T $(USER_LD) -o $$@ $$(filter $(OBJDIR)/%.o,$$^) $(LIBS) $(LDLIBS)
endef

$(foreach d,$(CMD_DIRS),\
	$(foreach p,$(basename $(notdir $(wildcard $(d)/*.c))),\
		$(eval $(call ADD_SINGLE_PROGRAM,$(p),$(d)))))
$(foreach d,$(USER_DIRS),\
	$(eval $(call ADD_DIR_PROGRAM,$(notdir $(d)),$(patsubst %/,%,$(dir $(d))))))

# Vendored Lua interpreter: linked from vendor/lua (see its README).
$(OBJDIR)/vendor/lua/%.o: $(LUA_DIR)/%.c | $(UAPI_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(LUA_CFLAGS) -c -o $@ $<

$(BINDIR)/lua: $(LUA_OBJS) $(LIBS) $(USER_LD) | $(BINDIR)
	$(CC) $(LUA_CFLAGS) $(LDFLAGS) -T $(USER_LD) -o $@ $(LUA_OBJS) \
		$(LIBS) $(LDLIBS)

$(BUILD_ROOT)/tests/%: $(OBJDIR)/tests/%.o $(LIBS) $(USER_LD)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(LDFLAGS) -T $(USER_LD) -o $@ $< $(LIBS) $(LDLIBS)

$(CC_HOST): $(CC_HOST_OBJS)
	$(HOSTCC) $(HOST_CFLAGS) -o $@ $^

clean:
	$(RM) -rf $(BUILD_ROOT)

-include $(DEPS)
