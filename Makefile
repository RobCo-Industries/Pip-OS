# Pip-OS kernel. Built with the RobCo-Core library in external/robco-core.
#
#   make                      build/pipos.img
#   make test                 host tests, then target tests on every board
#   make test BOARD=raspi0    target tests on one board
#   make test-host            host unit tests and the pin check
#   make test-target          QEMU target tests
#   make check-pins           CI workflow pins and doc links match the submodule
#   make check-pins-test      the pin check and the link check wiring work without git
#   make kernel               build/pipos.img and its ELF only
#   make sdimage              build/sd.img with the kernel, a sample config,
#                             the hello example as ROM1.ROM (ROM=<file> picks
#                             another image, ROM=none leaves it out), the
#                             tape example in PIPBOY/HOLO (TAPE=<files>,
#                             TAPE=none) and the CARD_DIR=<dir> tree
#   make run BOARD=raspi2b    boot build/sd.img in QEMU on this terminal
#                             (WINDOW=1 also opens the display window)
#   make test-roms            build/roms/<name>/<name>.rom from tests/roms/
#   make sdk-example          build/examples/hello/ROM1.ROM and
#                             build/examples/tape/BEACON.HTP from sdk/examples
#   make bootdir DIR=<path>   the files of build/sd.img for a real card,
#                             firmware included (FIRMWARE=0 leaves it out,
#                             CLEAN=1 removes files an earlier run wrote)
#   make conformance-bootdir DIR=<path>
#                             bootdir with the conformance ROM as
#                             PIPBOY/ROM1.ROM and its two holotapes
#
# Another repository calls these as make -C <pip-os> <target> with an
# absolute BUILD and absolute paths: see docs/ROM_DEVELOPMENT.md.

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
              kernel/audio_rules.c \
              kernel/bootlines.c \
              kernel/gfx_draw.c \
              kernel/holo_rules.c \
              kernel/input_events.c \
              kernel/settings.c \
              kernel/storage_rules.c \
              kernel/sysabi.c \
              kernel/sysdispatch.c \
              kernel/time_clock.c \

SRCS       := $(sort $(wildcard kernel/*.c))
# The ABI header in sdk/include is shared with ROM code.
# override: a command-line EXTRA_CFLAGS, such as a test build's, adds to it.
override EXTRA_CFLAGS += -Isdk/include

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
# The card's holotapes, whatever ROM says: the tape example unless TAPE
# names other files, which keep their own names. TAPE=none leaves it out.
TAPE_BUILD     = $(abspath $(BUILD))/examples/tape
EXAMPLE_TAPE   = $(TAPE_BUILD)/BEACON.HTP
TAPE          ?= $(EXAMPLE_TAPE)
CARD_TAPES    := $(filter-out none,$(TAPE))
CARD_ROM      := $(filter-out none,$(ROM))
# tools/card.py check-tapes: two names that differ only in case, a name
# that is not *.HTP, or a TAPE or ROM file that is a link or a special
# file stop the build. A long *.HTP name gets a warning.
CARD          := $(PYTHON) tools/card.py
ifneq ($(CARD_TAPES)$(CARD_ROM),)
TAPE_ERRORS := $(shell $(CARD) check-tapes $(if $(CARD_ROM),--rom $(CARD_ROM)) -- $(CARD_TAPES))
ifneq ($(TAPE_ERRORS),)
$(error $(TAPE_ERRORS))
endif
endif
SDIMAGE_FILES += $(addsuffix :PIPBOY/HOLO/,$(CARD_TAPES))
# CARD_DIR: a folder tree copied onto the card root after the standard
# files above and the kernel. tools/card.py stage-card-dir stops the build when one
# of its files would replace a standard one. It replaces RobCo-Core's
# SDIMAGE_DIR, which copies first and lets the standard files win.
ifneq ($(filter command% environment%,$(origin SDIMAGE_DIR)),)
$(error SDIMAGE_DIR is not a Pip-OS setting: use CARD_DIR=<dir>, see docs/BUILD.md)
endif
ifneq ($(word 2,$(CARD_DIR)),)
$(error CARD_DIR cannot contain spaces)
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
#   tests/roms/<name>.c       one ROM, into $(ROM_BUILD)/<name>/<name>.rom
#   tests/roms/<name>/Makefile a ROM project of its own, such as sdkcheck
# fault.c builds once per fault case. rt.h names libpip's self-check
# markers, pipos/selftest.h.
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
	    TITLE="Pip-OS test ROM fault_$*" SRCS="tests/roms/fault.c" \
	    CFLAGS_EXTRA="$(ROM_CFLAGS) -DFAULT_$*"

$(addprefix rom-,$(TEST_ROMS)): rom-%:
	$(PIP_APP) BUILD=$(ROM_BUILD)/$* IMAGE=$*.rom NAME=$* TITLE="Pip-OS test ROM $*" \
	    SRCS="tests/roms/$*.c $(if $(filter calls,$*),tests/roms/regs.S)" \
	    CFLAGS_EXTRA="$(ROM_CFLAGS)"

# Test holotapes: tests/roms/holo_tapes/<name>.c, into
# $(HOLO_BUILD)/<name>/<name>.htp. test_holo.py copies them to /PIPBOY/HOLO.
HOLO_BUILD   := $(BUILD)/holotapes
TEST_HOLOS   := $(basename $(notdir $(wildcard tests/roms/holo_tapes/*.c)))
HOLO_TARGETS := $(addprefix holo-,$(TEST_HOLOS))
.PHONY: $(HOLO_TARGETS)
test-roms: $(HOLO_TARGETS)

$(HOLO_TARGETS): holo-%:
	$(PIP_APP) KIND=holotape BUILD=$(HOLO_BUILD)/$* IMAGE=$*.htp NAME=$* \
	    TITLE="Holotape_$*" SRCS="tests/roms/holo_tapes/$*.c" \
	    CFLAGS_EXTRA="$(ROM_CFLAGS)"

# tape_util: the tape source sealed as a utility, subtype 1.
.PHONY: holo-tape_util
test-roms: holo-tape_util
holo-tape_util:
	$(PIP_APP) KIND=holotape SUBTYPE=utility BUILD=$(HOLO_BUILD)/tape_util IMAGE=tape_util.htp \
	    NAME=tape_util TITLE="Holotape_tape_util" SRCS="tests/roms/holo_tapes/tape.c" \
	    CFLAGS_EXTRA="$(ROM_CFLAGS)"

$(addprefix rom-,$(ROM_PROJECTS)): rom-%:
	$(MAKE) --no-print-directory -C tests/roms/$* BUILD=$(abspath $(ROM_BUILD))/$*

# The hello and tape examples, the default ROM and holotape of the card.
# Each builds only when the card uses it, so ROM=<file> TAPE=none builds
# neither.
.PHONY: example-hello example-tape
sdk-example: example-hello example-tape
example-hello:
	$(MAKE) --no-print-directory -C sdk/examples/hello BUILD=$(EXAMPLE_BUILD)
example-tape:
	$(MAKE) --no-print-directory -C sdk/examples/tape BUILD=$(TAPE_BUILD)

$(EXAMPLE_ROM): example-hello
$(EXAMPLE_TAPE): example-tape

# ---- Conformance ROM ------------------------------------------------------

# conformance/: a ROM that runs every system call and shows a PASS/FAIL
# table, its companion holotape, and a data holotape made from it.
# `make conformance-card` writes build/conformance.img through sdimage,
# so build/sd.img is the same card afterwards. See docs/TESTING.md.
CONF_BUILD   = $(abspath $(BUILD))/conformance
CONF_ROM     = $(CONF_BUILD)/rom/CONFORM.ROM
CONF_TAPE    = $(CONF_BUILD)/tape/CONFTAPE.HTP
CONF_DATA    = $(CONF_BUILD)/CONFDATA.HTP
CONF_FILES   = boot/PIPOS.CFG:PIPBOY/PIPOS.CFG $(CONF_ROM):PIPBOY/ROM1.ROM \
               $(CONF_TAPE):PIPBOY/HOLO/ $(CONF_DATA):PIPBOY/HOLO/

.PHONY: conformance conformance-card
all: conformance

conformance:
	$(MAKE) --no-print-directory -C conformance/rom BUILD=$(CONF_BUILD)/rom
	$(MAKE) --no-print-directory -C conformance/tape BUILD=$(CONF_BUILD)/tape
	$(PYTHON) conformance/mkdata.py $(CONF_TAPE) $(CONF_DATA)

conformance-card: conformance $(BUILD)/$(IMAGE).img
	$(MAKE) --no-print-directory sdimage SDIMAGE_FILES="$(CONF_FILES)"
	cp $(BUILD)/sd.img $(BUILD)/conformance.img

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

# DOC_ROOT: the tree whose Markdown links check-doc-links reads. The
# submodule path is relative to it. tests/host/check_doc_links_test.sh
# points it at a temporary copy.
DOC_ROOT ?= .

check-pins: check-doc-links
	$(ROBCO_CORE)/tools/check_pins.sh .github/workflows/ci.yml

.PHONY: check-doc-links
check-doc-links:
	$(ROBCO_CORE)/tools/check_doc_links.sh $(DOC_ROOT) external/robco-core

check-pins-test:
	tests/host/check_pins_test.sh
	tests/host/check_doc_links_test.sh

build-dir-test:
	tests/host/build_dir_test.sh

# The panel model of tests/test_tft.py: RobCo-Core's ILI9341 model fed
# with a test build's tftdump reply.
PANEL_MODEL := $(BUILD)/host/panel_model
.PHONY: panel-model
panel-model: $(PANEL_MODEL)
all: $(PANEL_MODEL)
$(PANEL_MODEL): tests/host/panel_model.c $(ROBCO_CORE)/tests/host/ili9341_model.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -std=c99 -O2 -Wall -Wextra -Werror -I$(ROBCO_CORE)/tests/host -o $@ $<

# Host unit tests come from mk/host.mk. The shell tests run first.
test-host: check-pins check-pins-test build-dir-test libpip-host-test sdk-example-test sdk-rebuild-test \
           sdimage-rom-test bootdir-test conformance-bootdir-test outside-project-test

.PHONY: sdk-example-test sdk-rebuild-test sdimage-rom-test outside-project-test
sdk-example-test:
	tests/host/sdk_example_test.sh

sdk-rebuild-test:
	tests/host/sdk_rebuild_test.sh

sdimage-rom-test:
	tests/host/sdimage_rom_test.sh

outside-project-test:
	tests/host/outside_project_test.sh

.PHONY: bootdir-test conformance-bootdir-test
bootdir-test:
	tests/host/bootdir_test.sh

conformance-bootdir-test:
	tests/host/conformance_bootdir_test.sh

# libpip.a on the host: sdk/lib/ without svc.c and the wrappers, and RobCo-Core's
# string, printf, heap, INI and CRC code. libpip_names.h renames the C library
# functions to pipt_* in these objects so they do not replace the host's.
# -UROBCO_HOST keeps k_string.c's memcpy, memmove, memset and memcmp.
LIBPIP_HOST_SRCS := $(filter-out sdk/lib/svc.c sdk/lib/syscall.c,$(sort $(wildcard sdk/lib/*.c))) \
                    $(addprefix $(ROBCO_CORE)/src/lib/,k_string.c k_printf.c alloc.c ini.c crc32.c)
# Objects are pip_<name>.o for sdk/lib/ and robco_<name>.o for RobCo-Core,
# as in pipos-app.mk: sdk/lib/ini.c wraps RobCo-Core's ini.c.
libpip_host_obj = $(HOST_BUILD)/libpip/$(if $(filter sdk/%,$(1)),pip_,robco_)$(notdir $(1:.c=.o))
LIBPIP_HOST_OBJS := $(foreach s,$(LIBPIP_HOST_SRCS),$(call libpip_host_obj,$(s)))
LIBPIP_HOST_HDRS := tests/host/libpip_names.h $(wildcard sdk/include/pipos/*.h) \
                    $(addprefix $(ROBCO_CORE)/include/robco/,klibc.h alloc.h ini.h crc32.h)

.PHONY: libpip-host-test
libpip-host-test: $(HOST_BUILD)/libpip_test
	$(HOST_BUILD)/libpip_test

define libpip_host_rule
$(call libpip_host_obj,$(1)): $(1) $(LIBPIP_HOST_HDRS)
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

# Runs after SDIMAGE_FILES are copied, before the card is packed. CARD_DIR
# comes last, so stage-card-dir sees every standard file. check-stage then
# checks the whole staged tree, the same check as make bootdir.
sdimage-stage::
	$(ROBCO_CORE)/tools/mkbootdir.sh $(BUILD)/$(IMAGE).img $(SDIMAGE_STAGE) \
	    --kernel-name PIPOS.IMG --no-firmware
	mkdir -p $(SDIMAGE_STAGE)/PIPBOY/HOLO
	$(if $(strip $(CARD_DIR)),$(CARD) stage-card-dir $(SDIMAGE_STAGE) $(CARD_DIR))
	$(CARD) check-stage $(SDIMAGE_STAGE) $(CARD_DIR)

.PHONY: kernel run bootdir conformance-bootdir

# Only the kernel: no test ROM, example or conformance ROM.
kernel: $(BUILD)/$(IMAGE).img

RUN_BOARD := $(if $(filter all,$(BOARD)),raspi2b,$(BOARD))
run: sdimage
	PYTHONPATH=$(ROBCO_CORE)/tests $(PYTHON) -m harness.run $(BUILD)/$(IMAGE).img \
	    --board $(RUN_BOARD) --sd $(BUILD)/sd.img $(if $(filter 1,$(WINDOW)),--window)

# The files of `make sdimage`, with the same ROM, TAPE and CARD_DIR rules,
# plus the firmware. They are staged in $(BUILD)/bootstage first, so a
# CARD_DIR clash or a check-stage error stops the build before DIR
# changes. A PIPBOY/PIPOS.CFG already in DIR stays: it holds the user's
# settings. tools/card.py sync-bootdir keeps DIR/BOOTDIR.LST, the files the run
# wrote, and checks its whole plan against DIR before it changes anything.
BOOT_STAGE := $(BUILD)/bootstage
bootdir: $(BUILD)/$(IMAGE).img $(CARD_ROM) $(CARD_TAPES)
	$(if $(strip $(DIR)),,$(error DIR is not set: make bootdir DIR=<path>))
	rm -rf $(BOOT_STAGE)
	$(ROBCO_CORE)/tools/mkbootdir.sh $(BUILD)/$(IMAGE).img $(BOOT_STAGE) --kernel-name PIPOS.IMG \
	    $(if $(filter 0,$(FIRMWARE)),--no-firmware)
	mkdir -p $(BOOT_STAGE)/PIPBOY/HOLO
	cp boot/PIPOS.CFG $(BOOT_STAGE)/PIPBOY/PIPOS.CFG
	$(if $(CARD_ROM),cp $(CARD_ROM) $(BOOT_STAGE)/PIPBOY/ROM1.ROM)
	$(if $(CARD_TAPES),cp $(CARD_TAPES) $(BOOT_STAGE)/PIPBOY/HOLO/)
	$(if $(strip $(CARD_DIR)),$(CARD) stage-card-dir $(BOOT_STAGE) $(CARD_DIR))
	$(CARD) check-stage $(BOOT_STAGE) $(CARD_DIR)
	$(CARD) sync-bootdir $(BOOT_STAGE) $(DIR) $(if $(filter 1,$(CLEAN)),--clean)

# The same files as build/conformance.img, plus the firmware.
conformance-bootdir: conformance
	$(MAKE) --no-print-directory bootdir CLEAN=$(CLEAN) ROM=$(CONF_ROM) TAPE="$(CONF_TAPE) $(CONF_DATA)" \
	    CARD_DIR=
