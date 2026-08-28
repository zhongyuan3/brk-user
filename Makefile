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
XLEN ?= 64
ifeq ($(XLEN),32)
CFLAGS += -march=rv32gc -mabi=ilp32 -mcmodel=medany
else ifeq ($(XLEN),64)
CFLAGS += -march=rv64gc -mabi=lp64d -mcmodel=medlow
else
$(error invalid XLEN '$(XLEN)' (expected 32 or 64))
endif

BUILD_DIR := build/$(XLEN)
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

SH_SRCS := $(sort $(wildcard sh/*.c))
SH_OBJS := $(patsubst sh/%.c,$(BUILD_DIR)/sh/%.o,$(SH_SRCS))

USER_PROGS := $(sort $(USER_SINGLE_PROGS) sh)
USER_BINS := $(addprefix $(BINDIR)/,$(USER_PROGS))
USER_SINGLE_OBJS := $(addprefix $(BUILD_DIR)/,$(addsuffix .o,$(USER_SINGLE_PROGS)))

.SECONDARY: $(USER_SINGLE_OBJS) $(SH_OBJS)

.PHONY: all clean

all: $(USER_BINS)

$(BUILD_DIR):
	mkdir -p $@

$(BINDIR): | $(BUILD_DIR)
	mkdir -p $@

$(BUILD_DIR)/sh: | $(BUILD_DIR)
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

$(BUILD_DIR)/sh/%.o: sh/%.c | $(BUILD_DIR)/sh
	$(CC) $(CFLAGS) -c -o $@ $<

$(BINDIR)/sh: $(SH_OBJS) $(LIBUSER_A) $(USER_LD) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $(SH_OBJS) $(LIBUSER_A) $(USER_LDFLAGS) -lgcc

$(BINDIR)/%: $(BUILD_DIR)/%.o $(LIBUSER_A) $(USER_LD) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $< $(LIBUSER_A) $(USER_LDFLAGS) -lgcc

clean:
	$(RM) -rf $(BUILD_DIR)

-include $(LIB_OBJS:.o=.d)
-include $(addprefix $(BUILD_DIR)/,$(addsuffix .d,$(USER_SINGLE_PROGS)))
-include $(SH_OBJS:.o=.d)
