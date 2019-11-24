# BCM defines the target SoC board
# Possible values are : 
# - 2835 -> Raspberry Pi A, B, Zero...
# - 2836 -> Raspberry Pi 2
# - 2837 -> Raspberry Pi 3
BCM ?= 2835

USE_MINI_UART ?= 0

ARMGNU = arm-none-eabi
ARCH = aarch32
CPU = arm1176jzf-s

ifeq ($(BCM),2836)
	# If the target is BCM2836, use Cortex-A7 as CPU
	CPU = cortex-a7
else ifeq ($(BCM),2837)
	# If the target is BCM2837, the used toolchain should be made 
	# for 64bits architecture
	ARMGNU = aarch64-unknown-linux-gnu
	ARCH = aarch64
	CPU = cortex-a53
endif

CC = $(ARMGNU)-gcc
AS = $(ARMGNU)-as
LD = $(ARMGNU)-ld
OC = $(ARMGNU)-objcopy

CFLAGS = -O2 -Wall -Wextra -nostdlib -nostartfiles -ffreestanding -DBCM$(BCM) -D__$(ARCH)__ -DUSE_MINI_UART=$(USE_MINI_UART) -MMD
LDFLAGS += --defsym=__$(ARCH)__=1

# The bootloader on Raspberry Pi will use kernel7.img for 
# 32bits arch and kernel8.img for 64bits
KERNEL = kernel8

QEMU = qemu-system-arm
QEMU_MACHINE = raspi2

ifeq ($(ARCH),aarch32)
	KERNEL = kernel7
	CFLAGS += -mcpu=$(CPU) -fpic $(DIRECTIVES)
else
	CFLAGS += -mgeneral-regs-only $(DIRECTIVES)
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
	$(LD) -T $(BUILD_DIR)/linker.ld $(LDFLAGS) -o $(KERNEL).img $(OBJ_FILES)
