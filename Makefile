CROSS_COMPILE ?= riscv64-unknown-elf-
CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
AS := $(CROSS_COMPILE)as
AR := $(CROSS_COMPILE)ar
OBJCOPY := $(CROSS_COMPILE)objcopy
OBJDUMP := $(CROSS_COMPILE)objdump
RANLIB := $(CROSS_COMPILE)ranlib

BUILD_DIR := build
SRC_BUILD_DIR := $(BUILD_DIR)/src
LIB_BUILD_DIR := $(BUILD_DIR)/lib

CFLAGS := -O2 -ggdb -gdwarf-2 -Wall -Wextra -Werror
CFLAGS += -Wno-unused-parameter -Wno-unknown-attributes -Wno-main
CFLAGS += -march=rv64gc -mabi=lp64d -mcmodel=medlow
CFLAGS += -ffreestanding -nostdlib -fno-common
CFLAGS += -fno-omit-frame-pointer -fno-stack-protector
CFLAGS += -fno-pie -no-pie
CFLAGS += -MMD
CFLAGS += -I./include

USER_LD_SOURCE := src/user.ld.S
USER_LD := $(SRC_BUILD_DIR)/user.ld

USER_LIB_NAME := user
USER_LIB := $(LIB_BUILD_DIR)/lib$(USER_LIB_NAME).a

USER_SRC_FILES := $(wildcard src/*.c)
LIB_SRC_FILES := $(wildcard lib/*.c)

USER_OBJS := $(patsubst src/%.c,$(SRC_BUILD_DIR)/%.o,$(USER_SRC_FILES))
LIB_OBJS := $(patsubst lib/%.c,$(LIB_BUILD_DIR)/%.o,$(LIB_SRC_FILES))

USER_PROG := $(patsubst $(SRC_BUILD_DIR)/%.o,$(SRC_BUILD_DIR)/%,$(USER_OBJS))

USER_OBJS_DEPS := $(USER_OBJS:.o=.d)
LIB_OBJS_DEPS := $(LIB_OBJS:.o=.d)

.PHONY: all clean echo user_prog user_lib

.PRECIOUS: $(USER_OBJS) $(LIB_OBJS)

all: user_lib user_prog

clean:
	$(RM) -rf $(BUILD_DIR)

echo:
	@echo "USER_PROG=$(USER_PROG)"
	@echo "USER_LD=$(USER_LD)"
	@echo "USER_LIB=$(USER_LIB)"
	@echo "BUILD=$(BUILD)"
	@echo "OPT=$(OPT)"
	@echo "BUILD_DIR=$(BUILD_DIR)"

user_prog: $(USER_PROG)

user_lib: $(USER_LIB)

-include $(USER_OBJS_DEPS) $(LIB_OBJS_DEPS)

$(SRC_BUILD_DIR)/%: $(SRC_BUILD_DIR)/%.o $(USER_LIB) $(USER_LD)
	$(LD) -z max-page-size=4096 -T $(USER_LD) -static -o $@ $< -L$(LIB_BUILD_DIR) -l$(USER_LIB_NAME)

$(USER_LIB): $(LIB_OBJS)
	$(AR) rcs $@ $^
	$(RANLIB) $@

$(SRC_BUILD_DIR)/%.o: src/%.c
	@mkdir -p $(SRC_BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(LIB_BUILD_DIR)/%.o: lib/%.c
	@mkdir -p $(LIB_BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(USER_LD): $(USER_LD_SOURCE)
	@mkdir -p $(SRC_BUILD_DIR)
	$(CPP) $(CFLAGS) -o $@ $<
