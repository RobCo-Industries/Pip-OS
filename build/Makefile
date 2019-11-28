# BCM defines the target SoC board
# Possible values are : 
# - 2835 -> Raspberry Pi A, B, Zero...
# - 2836 -> Raspberry Pi 2
# - 2837 -> Raspberry Pi 3
BCM ?= 2835

USE_MINI_UART ?= 0

ARMGNU = arm-none-eabi
ARCH = aarch32

ifeq ($(BCM),2836)
	# If the target is BCM2836, use Cortex-A7 as CPU
	CFLAGS += -mcpu=cortex-a7 -march=armv7-a -mtune=cortex-a7
else ifeq ($(BCM),2837)
	# If the target is BCM2837, the used toolchain should be made 
	# for 64bits architecture
	ARMGNU = aarch64-unknown-linux-gnu
	ARCH = aarch64
	CFLAGS += -mcpu=cortex-a53 -march=armv8-a -mtune=cortex-a53
else
	# BCM2835
	CFLAGS += -mcpu=arm1176jzf-s -march=armv6 -mtune=arm1176jzf-s
endif

CC = $(ARMGNU)-gcc
AS = $(ARMGNU)-as
LD = $(ARMGNU)-ld
OC = $(ARMGNU)-objcopy

# Bare metal compilation flags
CFLAGS += -O2 -Wall -Wextra -nostdlib -nostartfiles -ffreestanding

# Disable specific warnings
CFLAGS += -Wno-int-to-pointer-cast

# Add definitions for pre-processing
CFLAGS += -DBCM$(BCM) -D__$(ARCH)__ -DUSE_MINI_UART=$(USE_MINI_UART)
LDFLAGS += --defsym=__$(ARCH)__=1 -nostdlib -nostartfiles

# The bootloader on Raspberry Pi will use kernel7.img for 
# 32bits arch and kernel8.img for 64bits
KERNEL = kernel7

QEMU = qemu-system-arm
QEMU_MACHINE = raspi2

ifeq ($(ARCH),aarch64)
	KERNEL = kernel8
	QEMU = qemu-system-aarch64
	QEMU_MACHINE = raspi3
endif

QEMU_FLAGS = -m 256 -M $(QEMU_MACHINE)

ifeq ($(USE_MINI_UART),1)
	QEMU_FLAGS += -serial null -serial stdio
else 
	QEMU_FLAGS += -serial stdio
endif

##################
# FILES AND DIRS #
##################

BUILD_DIR = .
OBJ_DIR = $(BUILD_DIR)/obj

SRC_DIR = ../src
INCLUDE_DIR = $(BUILD_DIR)/../include

KERNEL_C_FILES = $(wildcard $(SRC_DIR)/kernel/*.c)
COMMON_C_FILES = $(wildcard $(SRC_DIR)/common/*.c)
KERNEL_S_FILES = $(wildcard $(SRC_DIR)/$(ARCH)/*.S)

OBJ_FILES = $(patsubst $(SRC_DIR)/kernel/%.c, $(OBJ_DIR)/kernel/%_c.o, $(KERNEL_C_FILES))
OBJ_FILES += $(patsubst $(SRC_DIR)/common/%.c, $(OBJ_DIR)/common/%_c.o, $(COMMON_C_FILES))
OBJ_FILES += $(patsubst $(SRC_DIR)/$(ARCH)/%.S, $(OBJ_DIR)/$(ARCH)/%_S.o, $(KERNEL_S_FILES))

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
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $< -o $@

$(OBJ_DIR)/common/%_c.o: $(SRC_DIR)/common/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $< -o $@

# Compile S files
$(OBJ_DIR)/$(ARCH)/%_S.o: $(SRC_DIR)/$(ARCH)/%.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -I$(INCLUDE_DIR) -c $< -o $@

# Build the kernel img file by linking object files
$(KERNEL).img: $(OBJ_FILES)
	@echo $(OBJ_FILES)
	$(LD) $(LDFLAGS) -T $(BUILD_DIR)/linker.ld -o $(KERNEL).elf $(OBJ_FILES)
	$(OC) -O binary $(KERNEL).elf $(KERNEL).img
