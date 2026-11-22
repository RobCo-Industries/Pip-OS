# System call interface

Pip-OS does not implement a system call interface yet. The kernel boots, checks the ROM file and stays in its main loop. It does not run the ROM.

The interface ROMs and holotapes will use is specified in RobCo-Core's [PIPOS_ABI.md](https://github.com/RobCo-Industries/RobCo-Core/blob/061f224f06200e94b38e49e4fbef7f8300ecc5bd/docs/spec/PIPOS_ABI.md). The submodule holds the same file at `external/robco-core/docs/spec/PIPOS_ABI.md`.

What the kernel does today is in [BOOT.md](BOOT.md).
