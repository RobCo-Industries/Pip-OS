# Pip OS ![arch](https://img.shields.io/badge/Architecture-ARMv7--A-blue) ![ver](https://img.shields.io/badge/Version-0.0.1.0-green)

This is the repository for the Operating System used in Pip-Boy products.

## Building from sources

### Requirements 

- An ARM toolchain compatible to your machine ([GNU ARM](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-rm/downloads))
- An emulator to test the OS (we use [QEMU](https://www.qemu.org/download/))

```sh
cd build 
export ARM_TOOLCHAIN=<path to your toolchain>
make build
make run
```
