# Pip-OS application build: compiles a ROM or a holotape, links it with
# crt0 and libpip.a, seals it with rimg.py and verifies the result.
# Include it at the end of the project's Makefile:
#
#   PIPOS_SDK := external/pip-os/sdk
#   IMAGE     := ROM1.ROM
#   KIND      := rom            # or holotape
#   NAME      := DEITRIX
#   SRCS      := src/main.c src/ui.c
#   include $(PIPOS_SDK)/pipos-app.mk
#
# Optional: TITLE (META title), ICON (32x32 PBM or 128 raw bytes),
# VERSION (such as 1.2.3), SUBTYPE (holotape: game or 0, the default,
# utility or 1), BUILD (output folder, default build), CFLAGS_EXTRA, ROBCO_CORE (default $(PIPOS_SDK)/../external/robco-core),
# LIBPIP_DIR (where libpip.a is built, default $(BUILD)/libpip), CROSS,
# PYTHON, CI=1 (warnings are errors).
#
# Targets: all, clean. The guide is docs/ROM_DEVELOPMENT.md.

PIP_SDK := $(patsubst %/,%,$(strip $(PIPOS_SDK)))
ifeq ($(PIP_SDK),)
$(error PIPOS_SDK is not set)
endif
ifeq ($(wildcard $(PIP_SDK)/crt0.S),)
$(error PIPOS_SDK=$(PIP_SDK) holds no crt0.S)
endif
ifeq ($(strip $(IMAGE)),)
$(error IMAGE is not set)
endif
ifeq ($(strip $(NAME)),)
$(error NAME is not set)
endif
ifeq ($(strip $(SRCS)),)
$(error SRCS is not set)
endif

KIND       ?= rom
BUILD      ?= build
ROBCO_CORE ?= $(PIP_SDK)/../external/robco-core
LIBPIP_DIR ?= $(BUILD)/libpip
CROSS      ?= arm-none-eabi-
PYTHON     ?= python3

ifeq ($(wildcard $(ROBCO_CORE)/tools/rimg.py),)
$(error RobCo-Core not found in $(ROBCO_CORE): run git submodule update --init --recursive, or set ROBCO_CORE)
endif

# SUBTYPE: the header subtype. A ROM has only 0. A holotape takes 0 or
# game, 1 or utility. Data holotapes, subtype 2, have no slot: seal refuses them.
PIP_SUBTYPE_IN := $(strip $(SUBTYPE))
ifeq ($(strip $(KIND)),rom)
PIP_LDS     := $(PIP_SDK)/rom.ld
PIP_MAGIC   := ROM1
ifeq ($(filter-out 0,$(PIP_SUBTYPE_IN)),)
PIP_SUBTYPE := 0
else
$(error SUBTYPE=$(PIP_SUBTYPE_IN) is not valid for KIND=rom: a ROM has only subtype 0)
endif
else ifeq ($(strip $(KIND)),holotape)
PIP_LDS     := $(PIP_SDK)/holotape.ld
PIP_MAGIC   := ROBCO78
ifeq ($(filter-out 0 game,$(PIP_SUBTYPE_IN)),)
PIP_SUBTYPE := 0
else ifeq ($(filter-out 1 utility,$(PIP_SUBTYPE_IN)),)
PIP_SUBTYPE := 1
else ifeq ($(filter-out 2 data,$(PIP_SUBTYPE_IN)),)
$(error SUBTYPE=$(PIP_SUBTYPE_IN) is a data holotape: it has no slot and cannot run, so the SDK does not build it)
else
$(error SUBTYPE=$(PIP_SUBTYPE_IN) is not valid for KIND=holotape: use 0 or game, 1 or utility)
endif
else
$(error KIND must be rom or holotape, not $(KIND))
endif

PIP_CC      := $(CROSS)gcc
PIP_AR      := $(CROSS)ar
PIP_LD      := $(CROSS)ld
PIP_OBJCOPY := $(CROSS)objcopy
PIP_RIMG    := $(ROBCO_CORE)/tools/rimg.py

# The kernel's CPU flags, from RobCo-Core's mk/robco.mk: ARMv6, ARM
# state, soft float, so no movw/movt and no FPU registers.
PIP_ARCHFLAGS := -mcpu=arm1176jzf-s -marm -mfloat-abi=soft -mgeneral-regs-only -mno-unaligned-access
PIP_BASEFLAGS := $(PIP_ARCHFLAGS) -std=gnu11 -ffreestanding -nostdlib \
                 -ffunction-sections -fdata-sections -fno-tree-loop-distribute-patterns \
                 -O2 -g -Wall -Wextra -MMD -MP -I$(PIP_SDK)/include
ifeq ($(CI),1)
PIP_BASEFLAGS += -Werror
endif
PIP_CFLAGS   := $(PIP_BASEFLAGS) $(CFLAGS_EXTRA)
# Only the library sees RobCo-Core's headers. The INI and CRC functions
# get private names in the archive, so a ROM may define its own crc32 or
# ini_parse next to pip_crc32 and pip_ini_parse.
PIP_RENAMES  := $(foreach f,crc32 crc32_update ini_parse ini_get ini_get_int ini_get_bool ini_strerror,-D$(f)=pip__robco_$(f))
PIP_LIBFLAGS := $(PIP_BASEFLAGS) -I$(ROBCO_CORE)/include $(PIP_RENAMES)

# --no-warn-rwx-segments needs binutils 2.39 or later.
PIP_LD_RWX := $(shell $(PIP_LD) --help 2>/dev/null | grep -q no-warn-rwx-segments && echo -Wl,--no-warn-rwx-segments)

# libpip.a: sdk/lib/*.c and RobCo-Core's pure string, printf, heap, INI
# and CRC code.
LIBPIP          := $(LIBPIP_DIR)/libpip.a
PIP_LIB_SRCS    := $(sort $(wildcard $(PIP_SDK)/lib/*.c))
PIP_LIB_CORE    := $(addprefix $(ROBCO_CORE)/src/lib/,k_string.c k_printf.c alloc.c ini.c crc32.c)
PIP_LIB_OBJS    := $(patsubst %.c,$(LIBPIP_DIR)/pip_%.o,$(notdir $(PIP_LIB_SRCS))) \
                   $(patsubst %.c,$(LIBPIP_DIR)/robco_%.o,$(notdir $(PIP_LIB_CORE)))

# One object per source under $(BUILD)/obj. The name keeps the source
# extension, so src/start.c and src/start.S give two objects. A "." path
# part is dropped, ".." becomes "_up" and an absolute path starts with
# "_abs", so every object stays inside $(BUILD). Paths with spaces are
# not supported.
pip_empty :=
pip_sp    := $(pip_empty) $(pip_empty)
pip_obj = $(BUILD)/obj/$(if $(filter /%,$(1)),_abs/)$(subst $(pip_sp),/,$(strip $(patsubst ..,_up,$(filter-out .,$(subst /, ,$(1)))))).o
PIP_OBJS  := $(foreach s,$(SRCS),$(call pip_obj,$(s)))
PIP_CRT0  := $(BUILD)/crt0_$(KIND).o
PIP_ELF   := $(BUILD)/$(basename $(IMAGE)).elf
PIP_OUT   := $(BUILD)/$(IMAGE)

ifneq ($(words $(PIP_OBJS)),$(words $(sort $(PIP_OBJS))))
$(error SRCS names the same source twice: $(SRCS))
endif

# Each .cfg file holds the settings its targets depend on. It is rewritten
# only when they change, so a new setting rebuilds what it affects.
# app.cfg: compiling and sealing. link.cfg: also the source list and the
# link inputs, so a source removed from SRCS leaves the ELF.
# libpip.cfg: the library's sources and flags.
PIP_CFG         := $(BUILD)/app.cfg
PIP_CFG_TEXT    := $(KIND) | $(NAME) | $(VERSION) | $(TITLE) | $(ICON) | $(PIP_CC) | \
                   $(PIP_CFLAGS) | $(PIP_MAGIC) | $(PIP_SUBTYPE) | $(PIP_OBJCOPY) | $(PYTHON) | $(PIP_RIMG)
PIP_LCFG        := $(BUILD)/link.cfg
PIP_LCFG_TEXT   := $(PIP_CFG_TEXT) | $(SRCS) | $(PIP_LDS) | $(LIBPIP) | $(PIP_LD_RWX)
PIP_LIBCFG      := $(LIBPIP_DIR)/libpip.cfg
PIP_LIBCFG_TEXT := $(PIP_CC) | $(PIP_AR) | $(PIP_LIBFLAGS) | $(PIP_LIB_SRCS) | $(PIP_LIB_CORE)

# A failed recipe removes its half-written target.
.DELETE_ON_ERROR:

ifeq ($(.DEFAULT_GOAL),)
.DEFAULT_GOAL := all
endif

.PHONY: all clean
all: $(PIP_OUT)

# Sealed and verified under a temporary name, so a failure leaves no image.
$(PIP_OUT): $(PIP_ELF) $(PIP_RIMG) $(PIP_CFG) $(ICON)
	rm -f $@ $@.tmp
	$(PIP_OBJCOPY) -O binary $< $@.tmp
	$(PYTHON) $(PIP_RIMG) seal $@.tmp --name $(NAME) \
	    $(if $(strip $(VERSION)),--image-version $(VERSION)) \
	    $(if $(strip $(TITLE)),--title "$(TITLE)") \
	    $(if $(strip $(ICON)),--icon $(ICON)) || { rm -f $@.tmp; exit 1; }
	$(PYTHON) $(PIP_RIMG) verify $@.tmp --magic $(PIP_MAGIC) || { rm -f $@.tmp; exit 1; }
	mv $@.tmp $@

$(PIP_ELF): $(PIP_CRT0) $(PIP_OBJS) $(LIBPIP) $(PIP_LDS) $(PIP_LCFG)
	$(PIP_CC) $(PIP_ARCHFLAGS) -nostdlib -T $(PIP_LDS) -Wl,--gc-sections $(PIP_LD_RWX) \
	    -Wl,-Map,$(@:.elf=.map) -o $@ $(PIP_CRT0) $(PIP_OBJS) $(LIBPIP) -lgcc

$(PIP_CRT0): $(PIP_SDK)/crt0.S $(PIP_CFG)
	@mkdir -p $(dir $@)
	$(PIP_CC) $(PIP_CFLAGS) '-DPIP_IMG_MAGIC="$(PIP_MAGIC)"' -DPIP_IMG_SUBTYPE=$(PIP_SUBTYPE) -c -o $@ $<

$(LIBPIP): $(PIP_LIB_OBJS) $(PIP_LIBCFG)
	rm -f $@
	$(PIP_AR) rcs $@ $(PIP_LIB_OBJS)

define pip_src_rule
$(call pip_obj,$(1)): $(1) $(PIP_CFG)
	@mkdir -p $$(dir $$@)
	$$(PIP_CC) $$(PIP_CFLAGS) -c -o $$@ $$<
endef
$(foreach s,$(SRCS),$(eval $(call pip_src_rule,$(s))))

define pip_lib_rule
$(LIBPIP_DIR)/$(2)$(basename $(notdir $(1))).o: $(1) $(PIP_LIBCFG)
	@mkdir -p $$(dir $$@)
	$$(PIP_CC) $$(PIP_LIBFLAGS) -c -o $$@ $$<
endef
$(foreach s,$(PIP_LIB_SRCS),$(eval $(call pip_lib_rule,$(s),pip_)))
$(foreach s,$(PIP_LIB_CORE),$(eval $(call pip_lib_rule,$(s),robco_)))

# pip_cfg FILE TEXT TARGETS: runs while make reads this file, before it
# builds anything. When TEXT differs from FILE, it deletes TARGETS and
# rewrites FILE. Deleting matters: make compares whole seconds on some
# hosts, so a newer FILE alone can miss a change made in the same second.
pip_cfg = $(shell mkdir -p '$(dir $(1))' && \
    printf '%s\n' '$(subst ','"'"',$(2))' > '$(1).new' && \
    if cmp -s '$(1).new' '$(1)'; then rm -f '$(1).new'; else rm -f $(3); mv '$(1).new' '$(1)'; fi)
ifeq ($(filter clean,$(MAKECMDGOALS)),)
$(call pip_cfg,$(PIP_CFG),$(PIP_CFG_TEXT),$(PIP_CRT0) $(PIP_OBJS) $(PIP_OUT))
$(call pip_cfg,$(PIP_LCFG),$(PIP_LCFG_TEXT),$(PIP_ELF))
$(call pip_cfg,$(PIP_LIBCFG),$(PIP_LIBCFG_TEXT),$(PIP_LIB_OBJS) $(LIBPIP))
endif

clean:
	rm -rf $(BUILD) $(LIBPIP_DIR)

-include $(PIP_OBJS:.o=.d) $(PIP_CRT0:.o=.d) $(PIP_LIB_OBJS:.o=.d)
