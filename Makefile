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
#   make test-roms            build/roms/<name>/<name>.rom from tests/roms/
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

# ---- ROMs ---------------------------------------------------------------

# Every ROM builds through sdk/pipos-app.mk and libpip.a.
#
# Test ROMs: tests/roms/<name>.c with rt.c, into $(BUILD)/roms/<name>/<name>.rom.
# fault.c builds once per fault case. They share one libpip.a.
# sdkcheck is a ROM project of its own in tests/roms/sdkcheck/.
ROM_BUILD   := $(BUILD)/roms
TEST_ROMS   := hello ret5 tramp entry calls sleep spin
FAULT_CASES := wrkern rdperiph jmpkern undef priv null
ROM_TARGETS := $(addprefix rom-,$(TEST_ROMS) $(addprefix fault_,$(FAULT_CASES)))
PIP_APP     := $(MAKE) --no-print-directory -f sdk/pipos-app.mk PIPOS_SDK=sdk \
               ROBCO_CORE=$(ROBCO_CORE) KIND=rom VERSION=1.0.0 LIBPIP_DIR=$(ROM_BUILD)/libpip
ROM_CFLAGS  := -fno-builtin -Itests/roms

.PHONY: test-roms rom-sdkcheck $(ROM_TARGETS)
all: test-roms
test-roms: $(ROM_TARGETS) rom-sdkcheck

$(addprefix rom-fault_,$(FAULT_CASES)): rom-fault_%:
	$(PIP_APP) BUILD=$(ROM_BUILD)/fault_$* IMAGE=fault_$*.rom NAME=fault_$* \
	    TITLE="Pip-OS test ROM fault_$*" SRCS="tests/roms/fault.c tests/roms/rt.c" \
	    CFLAGS_EXTRA="$(ROM_CFLAGS) -DFAULT_$*"

$(addprefix rom-,$(TEST_ROMS)): rom-%:
	$(PIP_APP) BUILD=$(ROM_BUILD)/$* IMAGE=$*.rom NAME=$* TITLE="Pip-OS test ROM $*" \
	    SRCS="tests/roms/$*.c tests/roms/rt.c $(if $(filter calls,$*),tests/roms/regs.S)" \
	    CFLAGS_EXTRA="$(ROM_CFLAGS)"

rom-sdkcheck:
	$(MAKE) --no-print-directory -C tests/roms/sdkcheck BUILD=$(abspath $(ROM_BUILD))/sdkcheck

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
