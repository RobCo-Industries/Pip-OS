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

SDIMAGE_DIR := $(BUILD)/sdroot
SDIMAGE_MIB ?= 64

ifeq ($(wildcard $(ROBCO_CORE)/mk/robco.mk),)
$(error $(ROBCO_CORE) is empty: run git submodule update --init)
endif

.DEFAULT_GOAL := all
include $(ROBCO_CORE)/mk/robco.mk

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

# Host unit tests: tests/host/test_<name>.c, the pure kernel files and
# the pure library files they use, built with the host compiler.
HOST_CC       ?= cc
HOST_SANITIZE ?= 1
HOST_BUILD    := $(BUILD)/host
HOST_CFLAGS   := -std=gnu11 -Wall -Wextra -Werror -g -O1 -DROBCO_HOST=1 \
                 -I $(ROBCO_CORE)/include -I $(ROBCO_CORE)/tests/host -I kernel

ifeq ($(HOST_SANITIZE),1)
HOST_SAN_FLAGS := -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer
HOST_SAN_OK := $(shell mkdir -p $(HOST_BUILD) && \
                 printf 'int main(void){return 0;}\n' > $(HOST_BUILD)/probe.c && \
                 $(HOST_CC) $(HOST_SAN_FLAGS) -o $(HOST_BUILD)/probe $(HOST_BUILD)/probe.c >/dev/null 2>&1 && \
                 $(HOST_BUILD)/probe && echo 1; rm -rf $(HOST_BUILD)/probe $(HOST_BUILD)/probe.c $(HOST_BUILD)/probe.dSYM)
ifeq ($(HOST_SAN_OK),1)
HOST_CFLAGS += $(HOST_SAN_FLAGS)
endif
endif

HOST_LIB_SRCS  := $(addprefix $(ROBCO_CORE)/src/lib/,ini.c k_printf.c k_string.c)
HOST_TEST_SRCS := $(sort $(wildcard tests/host/test_*.c))
HOST_TESTS     := $(patsubst tests/host/%.c,$(HOST_BUILD)/%,$(HOST_TEST_SRCS))
HOST_HDRS      := $(wildcard kernel/*.h) $(ROBCO_CORE)/tests/host/rc_test.h

$(HOST_BUILD)/test_%: tests/host/test_%.c $(PURE_SRCS) $(HOST_LIB_SRCS) $(HOST_HDRS)
	@mkdir -p $(HOST_BUILD)
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(PURE_SRCS) $(HOST_LIB_SRCS)

test-host: check-pins check-pins-test build-dir-test $(HOST_TESTS)
	@set -u; failed=""; \
	for t in $(HOST_TESTS); do \
	  ./$$t || failed="$$failed $${t##*/}"; \
	done; \
	if [ -n "$$failed" ]; then echo "test-host: FAILED:$$failed" >&2; exit 1; fi; \
	echo "test-host: all passed"

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
