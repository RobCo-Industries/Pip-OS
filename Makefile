# Pip-OS kernel. Built with the RobCo-Core library in external/robco-core.
#
#   make                      build/pipos.img
#   make test                 host tests, then target tests on every board
#   make test BOARD=raspi0    target tests on one board
#   make test-host            host unit tests and the pin check
#   make test-target          QEMU target tests
#   make check-pins           CI workflow pins match the submodule
#   make check-pins-test      the pin check works without git
#   make sdimage              build/sd.img with the kernel, a sample config and
#                             the hello example as ROM1.ROM (ROM=<file> picks
#                             another image, ROM=none leaves it out)
#   make run BOARD=raspi2b    boot build/sd.img in QEMU on this terminal
#                             (WINDOW=1 also opens the display window)
#   make test-roms            build/roms/<name>/<name>.rom from tests/roms/
#   make sdk-example          build/examples/hello/ROM1.ROM from sdk/examples/hello
#   make bootdir DIR=<path>   boot files for a real card, firmware included

ROBCO_CORE := external/robco-core
IMAGE      := pipos
LINK_ADDR  := 0x8000
SLOT_END   := 0x00400000
CONFIG_H   := kernel/robco_config.h
BUILD      ?= build
PYTHON     ?= python3

# Every kernel/*.c builds into the kernel. PURE_SRCS, one per line and
# sorted, are the ones with no hardware access: the host tests link them.
PURE_SRCS  := \
              kernel/bootlines.c \
              kernel/gfx_draw.c \
              kernel/input_events.c \
              kernel/settings.c \
              kernel/storage_rules.c \
              kernel/sysabi.c \
              kernel/sysdispatch.c \

SRCS       := $(sort $(wildcard kernel/*.c))
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
# The card's /PIPBOY/ROM1.ROM: the hello example unless ROM names another
# image. ROM=none leaves the card without one.
EXAMPLE_BUILD  = $(abspath $(BUILD))/examples/hello
EXAMPLE_ROM    = $(EXAMPLE_BUILD)/ROM1.ROM
ROM           ?= $(EXAMPLE_ROM)
ifneq ($(ROM),none)
SDIMAGE_FILES += $(ROM):PIPBOY/ROM1.ROM
endif

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
# Test ROMs, all found by wildcard:
#   tests/roms/<name>.c       one ROM with rt.c, into $(ROM_BUILD)/<name>/<name>.rom
#   tests/roms/<name>/Makefile a ROM project of its own, such as sdkcheck
# fault.c builds once per fault case; rt.c is the shared runtime.
# calls also links regs.S. They share one libpip.a.
ROM_BUILD    := $(BUILD)/roms
TEST_ROMS    := $(filter-out rt fault,$(basename $(notdir $(wildcard tests/roms/*.c))))
ROM_PROJECTS := $(patsubst tests/roms/%/Makefile,%,$(wildcard tests/roms/*/Makefile))
FAULT_CASES  := wrkern rdperiph jmpkern undef priv null
ROM_TARGETS  := $(addprefix rom-,$(TEST_ROMS) $(addprefix fault_,$(FAULT_CASES)))
PIP_APP      := $(MAKE) --no-print-directory -f sdk/pipos-app.mk PIPOS_SDK=sdk \
                ROBCO_CORE=$(ROBCO_CORE) KIND=rom VERSION=1.0.0 LIBPIP_DIR=$(ROM_BUILD)/libpip
ROM_CFLAGS   := -fno-builtin -Itests/roms

.PHONY: test-roms sdk-example $(ROM_TARGETS) $(addprefix rom-,$(ROM_PROJECTS))
all: test-roms sdk-example
test-roms: $(ROM_TARGETS) $(addprefix rom-,$(ROM_PROJECTS))

$(addprefix rom-fault_,$(FAULT_CASES)): rom-fault_%:
	$(PIP_APP) BUILD=$(ROM_BUILD)/fault_$* IMAGE=fault_$*.rom NAME=fault_$* \
	    TITLE="Pip-OS test ROM fault_$*" SRCS="tests/roms/fault.c tests/roms/rt.c" \
	    CFLAGS_EXTRA="$(ROM_CFLAGS) -DFAULT_$*"

$(addprefix rom-,$(TEST_ROMS)): rom-%:
	$(PIP_APP) BUILD=$(ROM_BUILD)/$* IMAGE=$*.rom NAME=$* TITLE="Pip-OS test ROM $*" \
	    SRCS="tests/roms/$*.c tests/roms/rt.c $(if $(filter calls,$*),tests/roms/regs.S)" \
	    CFLAGS_EXTRA="$(ROM_CFLAGS)"

$(addprefix rom-,$(ROM_PROJECTS)): rom-%:
	$(MAKE) --no-print-directory -C tests/roms/$* BUILD=$(abspath $(ROM_BUILD))/$*

# The hello example, the default ROM of the card.
sdk-example:
	$(MAKE) --no-print-directory -C sdk/examples/hello BUILD=$(EXAMPLE_BUILD)

$(EXAMPLE_ROM): sdk-example

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
test-host: check-pins check-pins-test build-dir-test libpip-host-test sdk-example-test sdk-rebuild-test \
           sdimage-rom-test

.PHONY: sdk-example-test sdk-rebuild-test sdimage-rom-test
sdk-example-test:
	tests/host/sdk_example_test.sh

sdk-rebuild-test:
	tests/host/sdk_rebuild_test.sh

sdimage-rom-test:
	tests/host/sdimage_rom_test.sh

# libpip.a on the host: sdk/lib/ without svc.c and the wrappers, and RobCo-Core's
# string, printf and heap code. libpip_names.h renames the C library
# functions to pipt_* in these objects so they do not replace the host's.
# -UROBCO_HOST keeps k_string.c's memcpy, memmove, memset and memcmp.
LIBPIP_HOST_SRCS := $(filter-out sdk/lib/svc.c sdk/lib/syscall.c,$(sort $(wildcard sdk/lib/*.c))) \
                    $(addprefix $(ROBCO_CORE)/src/lib/,k_string.c k_printf.c alloc.c)
LIBPIP_HOST_OBJS := $(addprefix $(HOST_BUILD)/libpip/,$(notdir $(LIBPIP_HOST_SRCS:.c=.o)))
LIBPIP_HOST_HDRS := tests/host/libpip_names.h $(wildcard sdk/include/pipos/*.h) \
                    $(ROBCO_CORE)/include/robco/klibc.h $(ROBCO_CORE)/include/robco/alloc.h

.PHONY: libpip-host-test
libpip-host-test: $(HOST_BUILD)/libpip_test
	$(HOST_BUILD)/libpip_test

define libpip_host_rule
$(HOST_BUILD)/libpip/$(notdir $(1:.c=.o)): $(1) $(LIBPIP_HOST_HDRS)
	@mkdir -p $$(dir $$@)
	$$(HOST_CC) $$(HOST_CFLAGS) -UROBCO_HOST -fno-builtin -Isdk/include -include tests/host/libpip_names.h -c -o $$@ $$<
endef
$(foreach s,$(LIBPIP_HOST_SRCS),$(eval $(call libpip_host_rule,$(s))))

# host.mk only sees headers at the top of HOST_INCLUDES, not sdk/include/pipos/.
$(HOST_TESTS): $(wildcard sdk/include/pipos/*.h)
# test_sdk_calls.c includes the wrappers.
$(HOST_BUILD)/test_sdk_calls: sdk/lib/syscall.c

$(HOST_BUILD)/libpip_test: tests/host/libpip_test.c $(LIBPIP_HOST_OBJS) $(LIBPIP_HOST_HDRS)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -fno-builtin -Isdk/include -o $@ $< $(LIBPIP_HOST_OBJS)

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
