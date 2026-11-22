# Pip-OS kernel. Built with the RobCo-Core library in external/robco-core.
#
#   make                      build/pipos.img
#   make test                 host tests, then target tests on every board
#   make test BOARD=raspi0    target tests on one board
#   make test-host            host unit tests and the pin check
#   make test-target          QEMU target tests
#   make check-pins           CI workflow pins match the submodule
#   make check-pins-test      the pin check works without git
#   make sdimage              build/sd.img with the kernel and a sample config
#   make run BOARD=raspi2b    boot build/sd.img in QEMU on this terminal
#                             (WINDOW=1 also opens the display window)
#   make test-roms            build/roms/*.rom from tests/roms/
#   make bootdir DIR=<path>   boot files for a real card, firmware included

ROBCO_CORE := external/robco-core
IMAGE      := pipos
LINK_ADDR  := 0x8000
SLOT_END   := 0x00400000
CONFIG_H   := kernel/robco_config.h
BUILD      ?= build
PYTHON     ?= python3

# Pure code: no hardware access, also built for the host tests.
PURE_SRCS  := kernel/settings.c kernel/bootlines.c kernel/sysabi.c
SRCS       := kernel/main.c kernel/card.c kernel/screen.c kernel/panic.c \
              kernel/rom.c kernel/syscall.c $(PURE_SRCS)
# The ABI header in sdk/include is shared with ROM code.
EXTRA_CFLAGS += -Isdk/include

# Host unit tests: tests/host/test_<name>.c with the pure kernel files.
HOST_TEST_DIR := tests/host
HOST_SRCS     := $(PURE_SRCS)
HOST_INCLUDES := kernel sdk/include

# `make sdimage`: the kernel, config.txt, a sample config and the
# holotape directory. Set before the include: robco.mk reads them.
SDIMAGE_MIB   ?= 64
SDIMAGE_DEPS  := $(BUILD)/$(IMAGE).img
SDIMAGE_FILES := boot/PIPOS.CFG:PIPBOY/PIPOS.CFG

ifeq ($(wildcard $(ROBCO_CORE)/mk/robco.mk),)
$(error $(ROBCO_CORE) is empty: run git submodule update --init)
endif

.DEFAULT_GOAL := all
include $(ROBCO_CORE)/mk/robco.mk
include $(ROBCO_CORE)/mk/host.mk

# Target tests boot the image built here; keep top-level targets serial.
.NOTPARALLEL:

# ---- Test ROMs ----------------------------------------------------------

# tests/roms/<name>.c on top of sdk/, linked with sdk/rom.ld and sealed:
# $(BUILD)/roms/<name>.rom. fault.c builds once per fault case.
ROM_BUILD   := $(BUILD)/roms
TEST_ROMS   := hello ret5 tramp entry calls sleep spin
FAULT_CASES := wrkern rdperiph jmpkern undef priv null
ROM_IMAGES  := $(addprefix $(ROM_BUILD)/,$(addsuffix .rom,$(TEST_ROMS) $(addprefix fault_,$(FAULT_CASES))))
ROM_CFLAGS  := $(ROBCO_ARCHFLAGS) -std=gnu11 -ffreestanding -nostdlib -fno-builtin \
               -ffunction-sections -fdata-sections -fno-tree-loop-distribute-patterns \
               -O2 -g -Wall -Wextra -MMD -MP -Isdk/include -Itests/roms
ifeq ($(CI),1)
ROM_CFLAGS  += -Werror
endif
ROM_LDFLAGS := $(ROBCO_ARCHFLAGS) -nostdlib -T sdk/rom.ld -Wl,--gc-sections $(ROBCO_LD_RWX)
ROM_COMMON  := $(ROM_BUILD)/crt0.o $(ROM_BUILD)/rt.o $(ROM_BUILD)/regs.o

.PHONY: test-roms
all: test-roms
test-roms: $(ROM_IMAGES)

$(ROM_BUILD)/crt0.o: sdk/crt0.S
	@mkdir -p $(dir $@)
	$(CC) $(ROM_CFLAGS) -c -o $@ $<

$(ROM_BUILD)/fault_%.o: tests/roms/fault.c
	@mkdir -p $(dir $@)
	$(CC) $(ROM_CFLAGS) -DFAULT_$* -c -o $@ $<

$(ROM_BUILD)/%.o: tests/roms/%.c
	@mkdir -p $(dir $@)
	$(CC) $(ROM_CFLAGS) -c -o $@ $<

$(ROM_BUILD)/%.o: tests/roms/%.S
	@mkdir -p $(dir $@)
	$(CC) $(ROM_CFLAGS) -c -o $@ $<

$(ROM_BUILD)/%.elf: $(ROM_BUILD)/%.o $(ROM_COMMON) sdk/rom.ld
	$(CC) $(ROM_LDFLAGS) -Wl,-Map,$(@:.elf=.map) -o $@ $(ROM_COMMON) $< -lgcc

$(ROM_BUILD)/%.rom: $(ROM_BUILD)/%.elf $(ROBCO_CORE)/tools/rimg.py
	$(OBJCOPY) -O binary $< $@.tmp
	$(PYTHON) $(ROBCO_CORE)/tools/rimg.py seal $@.tmp --name $* --image-version 1.0.0 \
	    --title "Pip-OS test ROM $*"
	mv $@.tmp $@

.SECONDARY: $(ROM_IMAGES:.rom=.elf) $(ROM_IMAGES:.rom=.o) $(ROM_COMMON)
# Dependency files are never targets (no built-in %: %.o for them).
$(ROM_BUILD)/%.d: ;
-include $(wildcard $(ROM_BUILD)/*.d)

# ---- Python environment for the target tests --------------------------

VENV := .venv

.PHONY: venv
venv: $(VENV)/.installed

$(VENV)/.installed: $(ROBCO_CORE)/tests/requirements.txt
	$(PYTHON) -m venv $(VENV)
	$(VENV)/bin/pip install -q -r $<
	touch $@

# ---- Tests ------------------------------------------------------------

BOARD ?= all

.PHONY: test test-host test-target check-pins check-pins-test build-dir-test
test: test-host
	$(MAKE) test-target

test-target: all venv
	$(VENV)/bin/pytest tests -v --board $(BOARD) --build-dir $(BUILD)

check-pins:
	$(ROBCO_CORE)/tools/check_pins.sh .github/workflows/ci.yml

check-pins-test:
	tests/host/check_pins_test.sh

build-dir-test:
	tests/host/build_dir_test.sh

# Host unit tests come from mk/host.mk. The shell tests run first.
test-host: check-pins check-pins-test build-dir-test

# ---- Cards and QEMU -----------------------------------------------------

# Runs after SDIMAGE_FILES are copied, before the card is packed.
sdimage-stage::
	$(ROBCO_CORE)/tools/mkbootdir.sh $(BUILD)/$(IMAGE).img $(SDIMAGE_STAGE) \
	    --kernel-name PIPOS.IMG --no-firmware
	mkdir -p $(SDIMAGE_STAGE)/PIPBOY/HOLO

.PHONY: run bootdir

RUN_BOARD := $(if $(filter all,$(BOARD)),raspi2b,$(BOARD))
run: sdimage
	PYTHONPATH=$(ROBCO_CORE)/tests $(PYTHON) -m harness.run $(BUILD)/$(IMAGE).img \
	    --board $(RUN_BOARD) --sd $(BUILD)/sd.img $(if $(filter 1,$(WINDOW)),--window)

bootdir: all
	$(if $(strip $(DIR)),,$(error DIR is not set: make bootdir DIR=<path>))
	$(ROBCO_CORE)/tools/mkbootdir.sh $(BUILD)/$(IMAGE).img $(DIR) --kernel-name PIPOS.IMG
	mkdir -p $(DIR)/PIPBOY/HOLO
	test -e $(DIR)/PIPBOY/PIPOS.CFG || cp boot/PIPOS.CFG $(DIR)/PIPBOY/PIPOS.CFG
