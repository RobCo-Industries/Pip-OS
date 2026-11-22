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
# VERSION (such as 1.2.3), BUILD (output folder, default build),
# CFLAGS_EXTRA, ROBCO_CORE (default $(PIPOS_SDK)/../external/robco-core),
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

ifeq ($(strip $(KIND)),rom)
PIP_LDS     := $(PIP_SDK)/rom.ld
PIP_MAGIC   := ROM1
PIP_SUBTYPE := 0
else ifeq ($(strip $(KIND)),holotape)
PIP_LDS     := $(PIP_SDK)/holotape.ld
PIP_MAGIC   := ROBCO78
PIP_SUBTYPE := 0
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
# Only the library sees RobCo-Core's headers.
PIP_LIBFLAGS := $(PIP_BASEFLAGS) -I$(ROBCO_CORE)/include

# --no-warn-rwx-segments needs binutils 2.39 or later.
PIP_LD_RWX := $(shell $(PIP_LD) --help 2>/dev/null | grep -q no-warn-rwx-segments && echo -Wl,--no-warn-rwx-segments)

# libpip.a: sdk/lib/*.c and RobCo-Core's pure string, printf and heap code.
LIBPIP          := $(LIBPIP_DIR)/libpip.a
PIP_LIB_SRCS    := $(sort $(wildcard $(PIP_SDK)/lib/*.c))
PIP_LIB_CORE    := $(addprefix $(ROBCO_CORE)/src/lib/,k_string.c k_printf.c alloc.c)
PIP_LIB_OBJS    := $(patsubst %.c,$(LIBPIP_DIR)/pip_%.o,$(notdir $(PIP_LIB_SRCS))) \
                   $(patsubst %.c,$(LIBPIP_DIR)/robco_%.o,$(notdir $(PIP_LIB_CORE)))

# One object per source under $(BUILD)/obj, ".." becomes "__".
pip_obj = $(BUILD)/obj/$(subst ..,__,$(patsubst /%,%,$(basename $(1)))).o
PIP_OBJS  := $(foreach s,$(SRCS),$(call pip_obj,$(s)))
PIP_CRT0  := $(BUILD)/crt0_$(KIND).o
PIP_ELF   := $(BUILD)/$(basename $(IMAGE)).elf
PIP_OUT   := $(BUILD)/$(IMAGE)

# Rebuilds everything when a setting changes.
PIP_CFG   := $(BUILD)/app.cfg
PIP_CFG_TEXT := $(KIND) $(NAME) $(VERSION) $(TITLE) $(ICON) $(CFLAGS_EXTRA) $(CI)

ifeq ($(.DEFAULT_GOAL),)
.DEFAULT_GOAL := all
endif

.PHONY: all clean pip-force
all: $(PIP_OUT)

$(PIP_OUT): $(PIP_ELF) $(PIP_RIMG) $(PIP_CFG) $(ICON)
	$(PIP_OBJCOPY) -O binary $< $@.tmp
	$(PYTHON) $(PIP_RIMG) seal $@.tmp --name $(NAME) \
	    $(if $(strip $(VERSION)),--image-version $(VERSION)) \
	    $(if $(strip $(TITLE)),--title "$(TITLE)") \
	    $(if $(strip $(ICON)),--icon $(ICON))
	mv $@.tmp $@
	$(PYTHON) $(PIP_RIMG) verify $@ --magic $(PIP_MAGIC)

$(PIP_ELF): $(PIP_CRT0) $(PIP_OBJS) $(LIBPIP) $(PIP_LDS)
	$(PIP_CC) $(PIP_ARCHFLAGS) -nostdlib -T $(PIP_LDS) -Wl,--gc-sections $(PIP_LD_RWX) \
	    -Wl,-Map,$(@:.elf=.map) -o $@ $(PIP_CRT0) $(PIP_OBJS) $(LIBPIP) -lgcc

$(PIP_CRT0): $(PIP_SDK)/crt0.S $(PIP_CFG)
	@mkdir -p $(dir $@)
	$(PIP_CC) $(PIP_CFLAGS) '-DPIP_IMG_MAGIC="$(PIP_MAGIC)"' -DPIP_IMG_SUBTYPE=$(PIP_SUBTYPE) -c -o $@ $<

$(LIBPIP): $(PIP_LIB_OBJS)
	rm -f $@
	$(PIP_AR) rcs $@ $^

define pip_src_rule
$(call pip_obj,$(1)): $(1) $(PIP_CFG)
	@mkdir -p $$(dir $$@)
	$$(PIP_CC) $$(PIP_CFLAGS) -c -o $$@ $$<
endef
$(foreach s,$(SRCS),$(eval $(call pip_src_rule,$(s))))

define pip_lib_rule
$(LIBPIP_DIR)/$(2)$(basename $(notdir $(1))).o: $(1)
	@mkdir -p $$(dir $$@)
	$$(PIP_CC) $$(PIP_LIBFLAGS) -c -o $$@ $$<
endef
$(foreach s,$(PIP_LIB_SRCS),$(eval $(call pip_lib_rule,$(s),pip_)))
$(foreach s,$(PIP_LIB_CORE),$(eval $(call pip_lib_rule,$(s),robco_)))

$(PIP_CFG): pip-force
	@mkdir -p $(dir $@)
	@printf '%s\n' '$(subst ','"'"',$(PIP_CFG_TEXT))' > $@.new
	@if cmp -s $@.new $@; then rm -f $@.new; else mv $@.new $@; fi

clean:
	rm -rf $(BUILD) $(LIBPIP_DIR)

-include $(PIP_OBJS:.o=.d) $(PIP_CRT0:.o=.d) $(PIP_LIB_OBJS:.o=.d)
