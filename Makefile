ifndef CROSS_COMPILE
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

CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
AR := $(CROSS_COMPILE)ar
RANLIB := $(CROSS_COMPILE)ranlib
CPP := $(CC) -E




CFLAGS := -O2 -ggdb -gdwarf-2 -Wall -Wextra -Werror
CFLAGS += -march=rv64gc -mabi=lp64d -mcmodel=medlow

BUILD_DIR := build/64
BINDIR := $(BUILD_DIR)/bin



CFLAGS += -Wno-unused-parameter -Wno-unknown-attributes -Wno-main

CFLAGS += -ffreestanding -nostdlib -fno-common
CFLAGS += -fno-omit-frame-pointer -fno-stack-protector
CFLAGS += -fno-pie -no-pie
CFLAGS += -I./include
CFLAGS += -MMD -MP

USER_LD_SOURCE := src/user.ld.S
USER_LD := $(BUILD_DIR)/user.ld

USER_LDFLAGS := -z max-page-size=4096 -static -T $(USER_LD)

LIB_SRCS := $(sort $(wildcard lib/*/*.c))
LIB_OBJS := $(patsubst lib/%.c,$(BUILD_DIR)/lib/%.o,$(LIB_SRCS))
LIBUSER_A := $(BUILD_DIR)/libuser.a

USER_SINGLE_SRCS := $(wildcard src/*.c)
USER_SINGLE_PROGS := $(basename $(notdir $(USER_SINGLE_SRCS)))

USER_PROGS := $(sort $(USER_SINGLE_PROGS))
USER_BINS := $(addprefix $(BINDIR)/,$(USER_PROGS))
USER_SINGLE_OBJS := $(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(USER_SINGLE_PROGS)))

.SECONDARY: $(USER_SINGLE_OBJS)

HOSTCC ?= cc
CC_HOST := $(BUILD_DIR)/cc-host

.PHONY: all clean cc-host

all: $(USER_BINS)

# Build the compiler for the host as well.  This is only a development aid:
# it lets you generate and inspect RISC-V ELF files without booting BRK.
cc-host: $(CC_HOST)

$(CC_HOST): src/cc.c | $(BUILD_DIR)
	$(HOSTCC) -std=gnu11 -O2 -Wall -Wextra -Werror -o $@ $<

$(BUILD_DIR):
	mkdir -p $@

$(BINDIR): | $(BUILD_DIR)
	mkdir -p $@

$(USER_LD): $(USER_LD_SOURCE) | $(BUILD_DIR)
	$(CPP) $(CFLAGS) -P -o $@ $<

$(BUILD_DIR)/lib/%.o: lib/%.c | $(BUILD_DIR)
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(LIBUSER_A): $(LIB_OBJS)
	$(RM) $@
	$(AR) rcs $@ $^
	$(RANLIB) $@

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BINDIR)/%: $(BUILD_DIR)/%.o $(LIBUSER_A) $(USER_LD) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBUSER_A) $(USER_LDFLAGS) -lgcc

clean:
	$(RM) -rf $(BUILD_DIR)

-include $(LIB_OBJS:.o=.d)
-include $(addprefix $(BUILD_DIR)/,$(addsuffix .d,$(USER_SINGLE_PROGS)))
