BUILD_ROOT ?= build
BINDIR := $(BUILD_ROOT)/bin

KERNEL_ROOT ?= ../brk
UAPI_SRC := $(KERNEL_ROOT)/src/include/uapi
UAPI_DIR := $(BUILD_ROOT)/include/uapi
UAPI_STAMP := $(UAPI_DIR)/.stamp
UAPI_HEADERS := $(wildcard $(UAPI_SRC)/*.h)

CROSS_COMPILE ?=

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
CFLAGS += -I./include -I$(BUILD_ROOT)/include
CFLAGS += -MMD -MP

LDFLAGS := -nostdlib -no-pie -z max-page-size=4096 -static
LDLIBS := -lgcc

HOST_CFLAGS ?= -std=gnu11 -O2 -Wall -Wextra -Werror

USER_LD_SOURCE := src/user.ld.S
USER_LD := $(BUILD_ROOT)/user.ld

LIB_SRCS := $(sort $(wildcard lib/*/*.c))
LIB_OBJS := $(patsubst lib/%.c,$(BUILD_ROOT)/lib/%.o,$(LIB_SRCS))
LIBUSER_A := $(BUILD_ROOT)/libuser.a

USER_SRCS := $(wildcard src/*.c)
USER_PROGS := $(sort $(basename $(notdir $(USER_SRCS))))
USER_BINS := $(addprefix $(BINDIR)/,$(USER_PROGS))
USER_OBJS := $(addprefix $(BUILD_ROOT)/,$(addsuffix .o,$(USER_PROGS)))
DEPS := $(LIB_OBJS:.o=.d) $(USER_OBJS:.o=.d)

# Keep object files around for debugging and incremental rebuilds.
.SECONDARY: $(LIB_OBJS) $(USER_OBJS)

CC_HOST := $(BUILD_ROOT)/cc-host

.PHONY: all lib cc-host clean help

all: $(USER_BINS)

lib: $(LIBUSER_A)

cc-host: $(CC_HOST)

help:
	@echo "Targets:"
	@echo "  all          Build all user programs (default)"
	@echo "  lib          Build libuser.a only"
	@echo "  cc-host      Build the host-side cc helper"
	@echo "  clean        Remove $(BUILD_ROOT)"
	@echo "  help         Show this help"
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

$(BUILD_ROOT)/lib/%.o: lib/%.c | $(UAPI_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_ROOT)/%.o: src/%.c | $(UAPI_STAMP)
	$(CC) $(CFLAGS) -c -o $@ $<

$(LIBUSER_A): $(LIB_OBJS)
	$(RM) $@
	$(AR) rcs $@ $^
	$(RANLIB) $@

$(BINDIR)/%: $(BUILD_ROOT)/%.o $(LIBUSER_A) $(USER_LD) | $(BINDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -T $(USER_LD) -o $@ $< $(LIBUSER_A) $(LDLIBS)

$(CC_HOST): src/cc.c | $(BUILD_ROOT)
	$(HOSTCC) $(HOST_CFLAGS) -o $@ $<

clean:
	$(RM) -rf $(BUILD_ROOT)

-include $(DEPS)
