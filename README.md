# Pip-OS

Pip-OS is a bare-metal kernel for a wearable Pip-Boy built from a Raspberry Pi, themed on the Fallout games. It boots, shows the boot sequence, then checks and runs the ROM on the SD card. The ROM is the user environment, [Deitrix](https://github.com/RobCo-Industries/Deitrix). Holotapes are small programs a ROM can start.

Pip-OS is built on [RobCo-Core](https://github.com/RobCo-Industries/RobCo-Core), the shared foundation of the RobCo projects. RobCo-Core provides the start-up code, the drivers, the screen console, the FAT file system, the stage loader, input, audio and the QEMU test harness.

One image runs on the Pi Zero, Zero W, Pi 1, Pi 2 and Pi 3, in 32-bit mode.

## Status

What works now:

- Boot on all four QEMU boards: `raspi0`, `raspi1ap`, `raspi2b` and `raspi3b`.
- MMU and caches, interrupts, a 100 Hz tick and a kernel heap.
- Boot text on the HDMI framebuffer and the serial console.
- Configuration from `/PIPBOY/PIPOS.CFG`: colour scheme, boot line delay, volume and power-on beep.
- The holotape check: the boot screen says whether `/PIPBOY/HOLO/` holds `.HTP` files.
- The ROM check: the kernel loads `/PIPBOY/ROM1.ROM` into the ROM slot and validates it. The boot screen shows its name and version, or the reason it failed.
- Running the ROM in user mode, with the MMU keeping it inside its own memory. A fault stops the ROM and shows on screen.
- The first system calls: `ticks_ms`, `sleep_ms`, `exit`, `sys_info`, `mem_info`, `log` and `rand`. See [docs/API.md](docs/API.md).
- A ROM SDK in `sdk/`: headers, `crt0.S`, link scripts, `libpip.a` with the syscall wrappers, string functions, printf and a heap, and `pipos-app.mk` to build and seal a ROM. Start with [docs/ROM_DEVELOPMENT.md](docs/ROM_DEVELOPMENT.md).
- Input from GPIO buttons and a dial, the serial console and a test channel on the mini UART.
- Audio: a power-on beep through PWM, or a trace on the serial console in QEMU.
- The RTC and software clock when an RTC chip answers.

What does not work yet:

- The other system calls of RobCo-Core's [PIPOS_ABI.md](https://github.com/RobCo-Industries/RobCo-Core/blob/891b3a25a1e38f88c214ef171f9c05c2f36bc6a1/docs/spec/PIPOS_ABI.md): graphics, input, audio, time, sensors, saves, files and holotapes.
- Loading or running holotapes.
- Save files.
- The SPI display, battery monitoring and sensors.
- Real hardware. Every test runs in QEMU.

## Build

```sh
git clone --recurse-submodules https://github.com/RobCo-Industries/Pip-OS.git
cd Pip-OS
make CI=1
```

The image is `build/pipos.img`. [docs/BUILD.md](docs/BUILD.md) lists the tools and the build outputs.

## Test

```sh
make test
make test BOARD=raspi0
```

`make test` runs the host unit tests, then boots the kernel in QEMU on all four boards. See [docs/TESTING.md](docs/TESTING.md).

## Run in QEMU

```sh
make run BOARD=raspi2b
```

This builds `build/sd.img` with the kernel, a sample configuration and the SDK's hello example as the ROM, and boots it with RobCo-Core's runner, with the serial console on the terminal. Press Ctrl-A, then X, to quit. `make run WINDOW=1` also opens the QEMU display window.

## Prepare a card

```sh
make bootdir DIR=/path/to/dir
```

Copy the files in `DIR` to the FAT32 boot partition of the card, then add `PIPBOY/ROM1.ROM`. See [docs/BUILD.md](docs/BUILD.md#a-real-card).

## Documentation

| Page | Content |
|---|---|
| [docs/BUILD.md](docs/BUILD.md) | Tools, build, layout, SD card images |
| [docs/BOOT.md](docs/BOOT.md) | Boot flow, boot text, markers, memory map |
| [docs/CONFIG.md](docs/CONFIG.md) | `PIPOS.CFG` keys and defaults |
| [docs/TESTING.md](docs/TESTING.md) | Host tests, QEMU tests, CI |
| [docs/HARDWARE.md](docs/HARDWARE.md) | Target hardware and what the kernel supports |
| [docs/API.md](docs/API.md) | System calls that work today |
| [docs/ROM_DEVELOPMENT.md](docs/ROM_DEVELOPMENT.md) | Writing a ROM with the SDK |

RobCo-Core documents the library and the shared specifications:

- [Getting started](https://github.com/RobCo-Industries/RobCo-Core/blob/891b3a25a1e38f88c214ef171f9c05c2f36bc6a1/docs/guide/GETTING_STARTED.md) and the [API reference](https://github.com/RobCo-Industries/RobCo-Core/blob/891b3a25a1e38f88c214ef171f9c05c2f36bc6a1/docs/guide/API.md)
- [Boot protocol](https://github.com/RobCo-Industries/RobCo-Core/blob/891b3a25a1e38f88c214ef171f9c05c2f36bc6a1/docs/spec/BOOT_PROTOCOL.md), [image format](https://github.com/RobCo-Industries/RobCo-Core/blob/891b3a25a1e38f88c214ef171f9c05c2f36bc6a1/docs/spec/IMAGE_FORMAT.md) and [memory map](https://github.com/RobCo-Industries/RobCo-Core/blob/891b3a25a1e38f88c214ef171f9c05c2f36bc6a1/docs/spec/MEMORY_MAP.md)
- [Pip-OS ABI](https://github.com/RobCo-Industries/RobCo-Core/blob/891b3a25a1e38f88c214ef171f9c05c2f36bc6a1/docs/spec/PIPOS_ABI.md)

## Related projects

| Project | Role |
|---|---|
| [RobCo-Core](https://github.com/RobCo-Industries/RobCo-Core) | Shared library and specifications |
| [Deitrix](https://github.com/RobCo-Industries/Deitrix) | ROM and user environment for the Pip-Boy |
| [RETROS-BIOS](https://github.com/RobCo-Industries/RETROS-BIOS) | First stage of the terminal chain |
| [MFBootAgent](https://github.com/RobCo-Industries/MFBootAgent) | Second stage of the terminal chain |
| [UOS](https://github.com/RobCo-Industries/UOS) | Terminal operating system |

## License

MIT. See [LICENSE](LICENSE).
