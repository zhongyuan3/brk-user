BUILD_ROOT ?= build
OBJDIR := $(BUILD_ROOT)/obj
BINDIR := $(BUILD_ROOT)/bin

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

KERNEL_ROOT ?= ../brk
UAPI_SRC := $(KERNEL_ROOT)/src/include/uapi
UAPI_DIR := $(BUILD_ROOT)/include/uapi
UAPI_STAMP := $(UAPI_DIR)/.stamp
UAPI_HEADERS := $(wildcard $(UAPI_SRC)/*.h)

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
CFLAGS += -I./src/include -I$(BUILD_ROOT)/include
CFLAGS += -MMD -MP

LDFLAGS := -nostdlib -no-pie -z max-page-size=4096 -static
# Userland provides its own compiler helpers; override with -lgcc only as a
# fallback if a future toolchain emits an unimplemented helper.
LIBGCC ?=
LDLIBS := $(LIBGCC)

HOST_CFLAGS ?= -std=gnu11 -O2 -Wall -Wextra -Werror
HOST_CFLAGS += -I$(BUILD_ROOT)/include

USER_LD_SOURCE := src/user.ld.S
USER_LD := $(BUILD_ROOT)/user.ld

# libbrkc and libbrkcmd sources form libuser.a.  Command sources do not.
LIB_SRCS := $(sort $(foreach d,src/libbrkc src/libbrkcmd,\
	$(shell find $(d) -name '*.c' -print)))
LIB_OBJS := $(patsubst src/%.c,$(OBJDIR)/%.o,$(LIB_SRCS))
LIBUSER_A := $(BUILD_ROOT)/libuser.a

# User commands live under src/cmd.  A command is either a single source file
# (src/cmd/foo.c) or a directory of sources (src/cmd/foo/**/*.c), which keeps
# multi-file commands easy to navigate.
CMD_DIR := src/cmd
USER_SINGLE_SRCS := $(sort $(wildcard $(CMD_DIR)/*.c))
USER_SINGLE_PROGS := $(basename $(notdir $(USER_SINGLE_SRCS)))
SRC_DIRS := $(patsubst %/,%,$(wildcard $(CMD_DIR)/*/))
USER_DIRS := $(SRC_DIRS)
USER_DIR_PROGS := $(notdir $(USER_DIRS))
USER_DIR_SRCS := $(sort $(foreach d,$(USER_DIRS),\
	$(shell find $(d) -name '*.c' -print)))

USER_PROGS := $(sort $(USER_SINGLE_PROGS) $(USER_DIR_PROGS))
USER_BINS := $(addprefix $(BINDIR)/,$(USER_PROGS))
USER_OBJS := $(patsubst src/%.c,$(OBJDIR)/%.o,\
	$(USER_SINGLE_SRCS) $(USER_DIR_SRCS))
DEPS := $(LIB_OBJS:.o=.d) $(USER_OBJS:.o=.d)

# Keep object files around for debugging and incremental rebuilds.
.SECONDARY: $(LIB_OBJS) $(USER_OBJS)

CC_HOST := $(BUILD_ROOT)/cc-host
CC_HOST_OBJDIR := $(BUILD_ROOT)/cc-host-obj
CC_HOST_SRCS := $(sort $(wildcard $(CMD_DIR)/cc/*.c))
CC_HOST_OBJS := $(patsubst src/%.c,$(CC_HOST_OBJDIR)/%.o,$(CC_HOST_SRCS))
DEPS += $(CC_HOST_OBJS:.o=.d)

.PHONY: all lib cc-host clean help list-progs

all: $(USER_BINS)

lib: $(LIBUSER_A)

cc-host: $(CC_HOST)

list-progs:
	@printf '%s\n' $(USER_PROGS)

help:
	@echo "Targets:"
	@echo "  all          Build all user programs (default)"
	@echo "  lib          Build libuser.a only"
	@echo "  cc-host      Build the host-side cc helper"
	@echo "  list-progs   Print the names of all user programs"
	@echo "  clean        Remove $(BUILD_ROOT)"
	@echo "  help         Show this help"
	@echo ""
	@echo "Layout:"
	@echo "  src/cmd/*.c         single-file commands"
	@echo "  src/cmd/<command>/  multi-file commands"
	@echo "  src/libbrkc/         BRK C library"
	@echo "  src/libbrkcmd/       BRK command helpers"
	@echo "  src/include/         public headers"
	@echo ""
	@echo "Configurable variables:"
	@echo "  CROSS_COMPILE=<prefix>"
	@echo "  BUILD_ROOT=<path>       default: build"
	@echo "  KERNEL_ROOT=<path>      default: ../brk"
	@echo "  HOSTCC=<cc>             default: cc"

$(BUILD_ROOT):
	mkdir -p $@

$(BINDIR): | $(BUILD_ROOT)
	mkdir -p $@

# Copy only the kernel UAPI headers into a private include root.  Kernel
# implementation headers under src/include/brk/ stay invisible to userland.
$(UAPI_STAMP): $(UAPI_SRC) $(UAPI_HEADERS) | $(BUILD_ROOT)
	$(RM) -rf $(UAPI_DIR)
	mkdir -p $(UAPI_DIR)
	cp $(UAPI_HEADERS) $(UAPI_DIR)/
	touch $@

$(USER_LD): $(USER_LD_SOURCE) | $(BUILD_ROOT)
	$(CPP) $(CFLAGS) -P -o $@ $<

$(OBJDIR)/%.o: src/%.c | $(UAPI_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(CC_HOST_OBJDIR)/%.o: src/%.c $(UAPI_STAMP) | $(UAPI_STAMP)
	@mkdir -p $(dir $@)
	$(HOSTCC) $(HOST_CFLAGS) -MMD -MP -c -o $@ $<

$(LIBUSER_A): $(LIB_OBJS)
	$(RM) $@
	$(AR) rcs $@ $^
	$(RANLIB) $@

define ADD_SINGLE_PROGRAM
$(BINDIR)/$(1): $(OBJDIR)/cmd/$(1).o $(LIBUSER_A) $(USER_LD) | $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -T $(USER_LD) -o $$@ $$< $(LIBUSER_A) $(LDLIBS)
endef

define ADD_DIR_PROGRAM
$(BINDIR)/$(1): $(patsubst src/%.c,$(OBJDIR)/%.o,$(filter $(CMD_DIR)/$(1)/%,$(USER_DIR_SRCS))) $(LIBUSER_A) $(USER_LD) | $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -T $(USER_LD) -o $$@ $$(filter $(OBJDIR)/%.o,$$^) $(LIBUSER_A) $(LDLIBS)
endef

$(foreach p,$(USER_SINGLE_PROGS),$(eval $(call ADD_SINGLE_PROGRAM,$(p))))
$(foreach p,$(USER_DIR_PROGS),$(eval $(call ADD_DIR_PROGRAM,$(p))))

$(CC_HOST): $(CC_HOST_OBJS)
	$(HOSTCC) $(HOST_CFLAGS) -o $@ $^

clean:
	$(RM) -rf $(BUILD_ROOT)

-include $(DEPS)
