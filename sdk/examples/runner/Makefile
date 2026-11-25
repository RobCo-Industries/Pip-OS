# The runner ROM: it launches the tape named in /PIPBOY/RUNNER.CFG and
# prints how it ended. `robco run` and `robco test` boot it.
PIPOS_SDK ?= ../..
IMAGE     := RUNNER.ROM
KIND      := rom
NAME      := RUNNER
TITLE     := Pip-OS runner
VERSION   := 1.0.0
SRCS      := main.c
include $(PIPOS_SDK)/pipos-app.mk

# The key of a run must have no static home: main.c's own object has an
# empty .data and an empty .bss, or the build stops.
.PHONY: check-static
all: check-static
check-static: $(PIP_OBJS)
	@$(CROSS)size -A $(PIP_OBJS) | awk '$$1 ~ /^\.(data|bss)/ && $$2 != 0 \
	    { print "runner: " $$1 " is not empty: " $$2 " bytes"; bad = 1 } \
	    END { exit bad }'
