# Pip-OS ROM SDK

This folder builds ROMs and holotapes for Pip-OS.

- `include/pipos/`: the public headers. Include `<pipos/pip.h>`.
- `lib/`: the sources of `libpip.a`: the system-call wrappers, `printf`, the string functions, the heap and the self-check markers (`pipos/selftest.h`). The string, printf and heap code comes from RobCo-Core.
- `crt0.S`, `rom.ld`, `holotape.ld`: start-up code and link scripts.
- `pipos-app.mk`: the make fragment a ROM project includes.
- `examples/hello/`: a first ROM to copy.
- `examples/tape/`: a first holotape to copy.
- `testing/pipos_testing/`: the Python test helpers and pytest plugin for a ROM project's QEMU tests.

Read the guide: [docs/ROM_DEVELOPMENT.md](../docs/ROM_DEVELOPMENT.md).

A ROM project with Pip-OS as a submodule also calls Pip-OS's Makefile for the kernel and the card: see [Using Pip-OS from another repository](../docs/ROM_DEVELOPMENT.md#using-pip-os-from-another-repository).
