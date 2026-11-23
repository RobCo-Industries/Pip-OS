# The conformance ROM, built with the SDK like any third-party ROM.
# The top-level `make conformance` builds it into build/conformance/rom.
PIPOS_SDK ?= ../../sdk
IMAGE     := CONFORM.ROM
KIND      := rom
NAME      := CONFORM
TITLE     := Pip-OS conformance
VERSION   := 1.0.0
SRCS      := main.c checks.c interact.c
include $(PIPOS_SDK)/pipos-app.mk
