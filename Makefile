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

BUILD_DIR := build
SRC_BUILD_DIR := $(BUILD_DIR)/src
LIB_BUILD_DIR := $(BUILD_DIR)/lib

MKDIR_P := mkdir -p

BASEFLAGS := -O2 -ggdb -gdwarf-2 -Wall -Wextra -Werror
BASEFLAGS += -Wno-unused-parameter -Wno-unknown-attributes -Wno-main
BASEFLAGS += -march=rv64gc -mabi=lp64d -mcmodel=medlow
BASEFLAGS += -ffreestanding -nostdlib -fno-common
BASEFLAGS += -fno-omit-frame-pointer -fno-stack-protector
BASEFLAGS += -fno-pie -no-pie
BASEFLAGS += -I./include

CFLAGS := $(BASEFLAGS) -MMD -MP

USER_LDFLAGS := -z max-page-size=4096 -static

USER_LD_SOURCE := src/user.ld.S
USER_LD := $(SRC_BUILD_DIR)/user.ld

USER_LIB_NAME := user
USER_LIB := $(LIB_BUILD_DIR)/lib$(USER_LIB_NAME).a

USER_SRC_FILES := $(wildcard src/*.c)
LIB_SRC_FILES := $(wildcard lib/*/*.c)

USER_OBJS := $(patsubst src/%.c,$(SRC_BUILD_DIR)/%.o,$(USER_SRC_FILES))
LIB_OBJS := $(patsubst lib/%.c,$(LIB_BUILD_DIR)/%.o,$(LIB_SRC_FILES))

USER_PROG := $(patsubst $(SRC_BUILD_DIR)/%.o,$(SRC_BUILD_DIR)/%,$(USER_OBJS))

USER_OBJS_DEPS := $(USER_OBJS:.o=.d)
LIB_OBJS_DEPS := $(LIB_OBJS:.o=.d)

.PHONY: all clean user_prog user_lib

.DELETE_ON_ERROR:

.PRECIOUS: $(USER_OBJS) $(LIB_OBJS)

all: user_lib user_prog

clean:
	$(RM) -rf $(BUILD_DIR)

user_prog: $(USER_PROG)

user_lib: $(USER_LIB)

-include $(USER_OBJS_DEPS) $(LIB_OBJS_DEPS)

$(SRC_BUILD_DIR)/%: $(SRC_BUILD_DIR)/%.o $(USER_LIB) $(USER_LD)
	$(LD) $(USER_LDFLAGS) -T $(USER_LD) -o $@ $< -L$(LIB_BUILD_DIR) -l$(USER_LIB_NAME)

$(USER_LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^
	$(RANLIB) $@

$(SRC_BUILD_DIR)/%.o: src/%.c
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(LIB_BUILD_DIR)/%.o: lib/%.c
	@$(MKDIR_P) $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(USER_LD): $(USER_LD_SOURCE)
	@$(MKDIR_P) $(dir $@)
	$(CPP) -P $(BASEFLAGS) -o $@ $<
