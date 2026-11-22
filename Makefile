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
#   make bootdir DIR=<path>   boot files for a real card, firmware included

ROBCO_CORE := external/robco-core
IMAGE      := pipos
LINK_ADDR  := 0x8000
SLOT_END   := 0x00400000
CONFIG_H   := kernel/robco_config.h
BUILD      ?= build
PYTHON     ?= python3

# Pure code: no hardware access, also built for the host tests.
PURE_SRCS  := kernel/settings.c kernel/bootlines.c
SRCS       := kernel/main.c kernel/card.c kernel/screen.c kernel/panic.c $(PURE_SRCS)

# Host unit tests: tests/host/test_<name>.c with the pure kernel files.
HOST_TEST_DIR := tests/host
HOST_SRCS     := $(PURE_SRCS)
HOST_INCLUDES := kernel

SDIMAGE_DIR := $(BUILD)/sdroot
SDIMAGE_MIB ?= 64

ifeq ($(wildcard $(ROBCO_CORE)/mk/robco.mk),)
$(error $(ROBCO_CORE) is empty: run git submodule update --init)
endif

.DEFAULT_GOAL := all
include $(ROBCO_CORE)/mk/robco.mk
include $(ROBCO_CORE)/mk/host.mk

# Target tests boot the image built here; keep top-level targets serial.
.NOTPARALLEL:

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
	tools/check_pins.sh

check-pins-test:
	tests/host/check_pins_test.sh

build-dir-test:
	tests/host/build_dir_test.sh

# Host unit tests come from mk/host.mk. The shell tests run first.
test-host: check-pins check-pins-test build-dir-test

# ---- Cards and QEMU -----------------------------------------------------

# Extends the library's sdimage target, which packs $(SDIMAGE_DIR).
.PHONY: sdroot run bootdir
sdimage: sdroot

sdroot: all
	rm -rf $(SDIMAGE_DIR)
	$(ROBCO_CORE)/tools/mkbootdir.sh $(BUILD)/$(IMAGE).img $(SDIMAGE_DIR) \
	    --kernel-name PIPOS.IMG --no-firmware
	mkdir -p $(SDIMAGE_DIR)/PIPBOY/HOLO
	cp boot/PIPOS.CFG $(SDIMAGE_DIR)/PIPBOY/PIPOS.CFG

RUN_BOARD := $(if $(filter all,$(BOARD)),raspi2b,$(BOARD))
run: sdimage
	$(PYTHON) tools/run_qemu.py $(RUN_BOARD) $(BUILD)/$(IMAGE).img $(BUILD)/sd.img

bootdir: all
	$(if $(strip $(DIR)),,$(error DIR is not set: make bootdir DIR=<path>))
	$(ROBCO_CORE)/tools/mkbootdir.sh $(BUILD)/$(IMAGE).img $(DIR) --kernel-name PIPOS.IMG
	mkdir -p $(DIR)/PIPBOY/HOLO
	test -e $(DIR)/PIPBOY/PIPOS.CFG || cp boot/PIPOS.CFG $(DIR)/PIPBOY/PIPOS.CFG
