# ROM development

Pip-OS cannot run a ROM yet. It loads `/PIPBOY/ROM1.ROM` into the ROM slot, validates it and shows its name and version on the boot screen. It never jumps to it.

The ROM file format is the `rimg` format with the `ROM1` magic and a META block. It is specified in RobCo-Core's [IMAGE_FORMAT.md](https://github.com/RobCo-Industries/RobCo-Core/blob/061f224f06200e94b38e49e4fbef7f8300ecc5bd/docs/spec/IMAGE_FORMAT.md). The system call interface a ROM will use is specified in [PIPOS_ABI.md](https://github.com/RobCo-Industries/RobCo-Core/blob/061f224f06200e94b38e49e4fbef7f8300ecc5bd/docs/spec/PIPOS_ABI.md) and is not implemented yet.

`make_rom()` in [tests/pipos_helpers.py](../tests/pipos_helpers.py) builds a valid ROM file for the tests. [BOOT.md](BOOT.md#rom-check) lists the checks a ROM file must pass.
