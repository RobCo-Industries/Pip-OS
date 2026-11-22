# Pip-OS ROM SDK

This folder builds ROMs and holotapes for Pip-OS.

- `include/pipos/`: the public headers. Include `<pipos/pip.h>`.
- `lib/`: the sources of `libpip.a`.
- `crt0.S`, `rom.ld`, `holotape.ld`: start-up code and link scripts.
- `pipos-app.mk`: the make fragment a ROM project includes.
- `examples/hello/`: a first ROM to copy.

Read the guide: [docs/ROM_DEVELOPMENT.md](../docs/ROM_DEVELOPMENT.md).
