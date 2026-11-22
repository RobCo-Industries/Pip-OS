# ROM development

The ROM developer guide comes with the SDK. Until then, this page points at what exists.

Pip-OS loads `/PIPBOY/ROM1.ROM` into the ROM slot, validates it and runs it in user mode. [BOOT.md](BOOT.md#running-the-rom) describes the entry state and [API.md](API.md) the calls that work.

The ROM file format is the `rimg` format with the `ROM1` magic and a META block. It is specified in RobCo-Core's [IMAGE_FORMAT.md](https://github.com/RobCo-Industries/RobCo-Core/blob/bec7ffcf267db93e7bf4344e7e687ccfd86f52f6/docs/spec/IMAGE_FORMAT.md). The system call interface a ROM will use is specified in [PIPOS_ABI.md](https://github.com/RobCo-Industries/RobCo-Core/blob/bec7ffcf267db93e7bf4344e7e687ccfd86f52f6/docs/spec/PIPOS_ABI.md).

`sdk/` holds the ABI header, the syscall wrappers, `crt0.S` and `rom.ld`. The test ROMs in [tests/roms/](../tests/roms) use them, and the `Makefile` shows how to build and seal one. [BOOT.md](BOOT.md#rom-check) lists the checks a ROM file must pass.
