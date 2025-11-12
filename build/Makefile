# BCM defines the target SoC board
# Possible values are : 
# - 2835 -> Raspberry Pi A, B, Zero...
# - 2836 -> Raspberry Pi 2
# - 2837 -> Raspberry Pi 3
BCM ?= 2835

USE_MINI_UART ?= 0

ARMGNU ?= arm-none-eabi

ARCH = aarch32

ifeq ($(BCM),2836)
	# If the target is BCM2836, use Cortex-A7 as CPU
	CFLAGS += -march=armv7-a -mtune=cortex-a7
else ifeq ($(BCM),2837)
	# If the target is BCM2837, the used toolchain should be made 
	# for 64bits architecture
	# Try to find an aarch64 cross-compiler dynamically
	ARMGNU := $(shell if command -v aarch64-linux-gnu-gcc > /dev/null 2>&1; then echo aarch64-linux-gnu; \
		elif command -v aarch64-unknown-linux-gnu-gcc > /dev/null 2>&1; then echo aarch64-unknown-linux-gnu; \
		elif command -v aarch64-none-elf-gcc > /dev/null 2>&1; then echo aarch64-none-elf; \
		else echo "aarch64-linux-gnu"; fi)
	ARCH = aarch64
	CFLAGS += -march=armv8-a -mtune=cortex-a53
else
	# BCM2835
	CFLAGS += -march=armv6 -mtune=arm1176jzf-s
endif

CC = $(ARMGNU)-gcc
AS = $(ARMGNU)-as
LD = $(ARMGNU)-ld
OC = $(ARMGNU)-objcopy

# Dynamically find the GCC version and library path
ARMGNU_VERSION := $(shell $(CC) -dumpversion 2>/dev/null || echo "13.2.1")
ARMGNU_PATH := $(shell dirname $(shell dirname $(shell which $(CC) 2>/dev/null || echo "/usr/bin/$(CC)")) 2>/dev/null || echo "/usr")

# Bare metal compilation flags
CFLAGS += -O2 -Wall -Wextra -nostdlib -nostartfiles -ffreestanding

# Disable specific warnings
CFLAGS += -Wno-int-to-pointer-cast

# Add definitions for pre-processing
CFLAGS += -DBCM$(BCM) -D__$(ARCH)__ -DUSE_MINI_UART=$(USE_MINI_UART)
LDFLAGS += --defsym=__$(ARCH)__=1 -nostdlib

# The bootloader on Raspberry Pi uses different kernel names:
# kernel.img: 32-bit ARMv6 kernel for Pi 1 and Zero (BCM2835)
# kernel7.img: 32-bit ARMv7 kernel for Pi 2 and 3 (BCM2836, BCM2837 in 32-bit)
# kernel8.img: 64-bit kernel for any 64-bit capable Pi (BCM2837 in 64-bit)
KERNEL = kernel7

QEMU = qemu-system-arm
QEMU_MACHINE = raspi2b
QEMU_MEM = 1024

ifeq ($(BCM),2835)
	KERNEL = kernel
	QEMU_MACHINE = raspi0
	QEMU_MEM = 512
else ifeq ($(BCM),2836)
	KERNEL = kernel7
	QEMU_MACHINE = raspi2b
	QEMU_MEM = 1024
else ifeq ($(BCM),2837)
	KERNEL = kernel8
	QEMU = qemu-system-aarch64
	QEMU_MACHINE = raspi3b
	QEMU_MEM = 1024
endif

# QEMU flags: use -display none with explicit -serial stdio for better UART emulation
QEMU_FLAGS = -m $(QEMU_MEM) -M $(QEMU_MACHINE) -display none -serial stdio

ifeq ($(USE_MINI_UART),1)
	# For mini UART (UART1), use two serial ports
	QEMU_FLAGS = -m $(QEMU_MEM) -M $(QEMU_MACHINE) -display none -serial null -serial stdio
endif

##################
# FILES AND DIRS #
##################

rwildcard=$(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) $(filter $(subst *,%,$2),$d))

BUILD_DIR = .
OBJ_DIR = $(BUILD_DIR)/obj

SRC_DIR = ../src
INCLUDE_DIR = $(BUILD_DIR)/../include
K_LIBC_DIR = $(SRC_DIR)/kernel/k_libc

KERNEL_C_FILES = $(call rwildcard, $(SRC_DIR)/kernel, *.c)
LIBC_C_FILES   = $(wildcard $(SRC_DIR)/libc/*.c)
KERNEL_S_FILES = $(wildcard $(SRC_DIR)/$(ARCH)/*.S)

OBJ_FILES  = $(patsubst $(SRC_DIR)/kernel/%.c, $(OBJ_DIR)/kernel/%_c.o, $(KERNEL_C_FILES))
OBJ_FILES += $(patsubst $(SRC_DIR)/libc/%.c, $(OBJ_DIR)/libc/%_c.o, $(LIBC_C_FILES))
OBJ_FILES += $(patsubst $(SRC_DIR)/$(ARCH)/%.S, $(OBJ_DIR)/$(ARCH)/%_S.o, $(KERNEL_S_FILES))

# Dynamically find libgcc path
LIBGCC_PATH := $(shell $(CC) -print-libgcc-file-name 2>/dev/null)
ifneq ($(LIBGCC_PATH),)
	LIBPATH = -lgcc -L$(dir $(LIBGCC_PATH))
else
	# Fallback to default paths
	LIBPATH = -lgcc -L$(ARMGNU_PATH)/lib/gcc/$(ARMGNU)/$(ARMGNU_VERSION)
endif

############
# COMMANDS #
############

.PHONY : all run clean

all: $(KERNEL).img

run: $(KERNEL).img
	$(QEMU) $(QEMU_FLAGS) -kernel $(KERNEL).img

clean:
	@rm -rf $(OBJ_DIR) 2> /dev/null || true
	@rm -f $(BUILD_DIR)/kernel*.img 2> /dev/null || true
	@rm -f $(BUILD_DIR)/kernel*.elf 2> /dev/null || true

# Compile C files
$(OBJ_DIR)/kernel/%_c.o: $(SRC_DIR)/kernel/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -I$(K_LIBC_DIR) -c $< -o $@

$(OBJ_DIR)/libc/%_c.o: $(SRC_DIR)/libc/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -I$(K_LIBC_DIR) -c $< -o $@

# Compile S files
$(OBJ_DIR)/$(ARCH)/%_S.o: $(SRC_DIR)/$(ARCH)/%.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $< -o $@

# Build the kernel img file by linking object files
$(KERNEL).img: $(OBJ_FILES)
	@echo $(OBJ_FILES)
	$(LD) $(LDFLAGS) -T $(BUILD_DIR)/linker.ld -o $(KERNEL).elf $(OBJ_FILES) $(LIBPATH)
	$(OC) -O binary $(KERNEL).elf $(KERNEL).img
