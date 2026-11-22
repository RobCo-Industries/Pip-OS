# Build

Pip-OS builds with the RobCo-Core library, a git submodule in `external/robco-core`. One image runs on every supported board: Pi Zero, Zero W, Pi 1, Pi 2 and Pi 3 in 32-bit mode.

## Tools

| Tool | Used for |
|---|---|
| `arm-none-eabi-gcc` and binutils | The kernel image |
| GNU make 3.81 or later | The build |
| Python 3 with `venv` | Sealing the image, the target tests |
| A host C compiler | Host tests |
| `qemu-system-arm` and `qemu-system-aarch64` 10.1 | Target tests and `make run` |
| mtools | SD card images |

On macOS with Homebrew:

```sh
brew install arm-none-eabi-gcc qemu mtools python
```

RobCo-Core's [getting started guide](https://github.com/RobCo-Industries/RobCo-Core/blob/1e0fb655ea09497d133c6e3c3b086a2bb8cbca0e/docs/guide/GETTING_STARTED.md) lists the versions in detail.

## Build the image

```sh
git clone --recurse-submodules https://github.com/RobCo-Industries/Pip-OS.git
cd Pip-OS
make CI=1
```

In an existing clone, run `git submodule update --init` first. `CI=1` turns warnings into errors.

| Output | Content |
|---|---|
| `build/pipos.img` | The sealed `rimg` image. Copy it to the card as `PIPOS.IMG`. |
| `build/pipos.elf` | The linked ELF, for `gdb` and `objdump` |
| `build/pipos.map` | The linker map |
| `build/roms/<name>/<name>.rom` | The test ROMs, see [TESTING.md](TESTING.md#test-roms) |
| `build/examples/hello/ROM1.ROM` | The SDK's hello example, see [ROM_DEVELOPMENT.md](ROM_DEVELOPMENT.md) |

The image is a `rimg` `BOOT` image with the Pip-OS subtype, linked at `0x8000`. The link fails when the image and its stacks pass `0x00400000`, the end of the Pip-OS slot. `python3 external/robco-core/tools/rimg.py info build/pipos.img` prints its header.

## Layout

| Path | Content |
|---|---|
| `Makefile` | Includes `external/robco-core/mk/robco.mk` and `mk/host.mk` and adds the test, card and QEMU targets |
| `kernel/` | Kernel sources and `robco_config.h` |
| `kernel/settings.c`, `kernel/bootlines.c`, `kernel/sysabi.c` | Pure code, also built for the host tests |
| `sdk/` | The ROM SDK: headers in `include/pipos/`, `crt0.S`, `rom.ld`, `holotape.ld`, `lib/` for `libpip.a`, `pipos-app.mk` and `examples/hello` |
| `boot/config.txt` | The firmware settings for the Pip-Boy chain |
| `boot/PIPOS.CFG` | Sample kernel configuration, see [CONFIG.md](CONFIG.md) |
| `tests/` | Host tests in `tests/host/`, QEMU tests in `tests/*.py`, test ROMs in `tests/roms/` |
| `pytest.ini` | The boards the QEMU tests run on by default |
| `external/robco-core/` | The library |

## Button pins

`kernel/robco_config.h` sets the GPIO pins of the buttons and the dial with `PIPOS_PIN_*` macros. Override one on the command line:

```sh
make CI=1 EXTRA_CFLAGS=-DPIPOS_PIN_SELECT=19
```

Run `make clean` first so every file sees the new value.

## SD card image for QEMU

```sh
make sdimage
make run BOARD=raspi2b
```

`make sdimage` writes `build/sd.img`: a 64 MiB FAT32 card with `PIPOS.IMG`, `config.txt`, `PIPBOY/PIPOS.CFG` from `boot/PIPOS.CFG`, `PIPBOY/ROM1.ROM` and an empty `PIPBOY/HOLO/`. The ROM is the SDK's hello example. `ROM=<file>` puts another image there, and `ROM=none` leaves the card without a ROM. It downloads no firmware. The `Makefile` lists the configuration in `SDIMAGE_FILES`, and its `sdimage-stage::` rule adds the kernel, `config.txt` and the holotape directory. `make run` builds the card again, with the same `ROM` rule, and boots it with RobCo-Core's runner, `python3 -m harness.run`, the way the firmware does, with the serial console on the terminal. `BOARD` is `raspi0`, `raspi1ap`, `raspi2b` or `raspi3b`. `WINDOW=1` also opens the QEMU display window. Press Ctrl-A, then X, to quit.

## A real card

```sh
make bootdir DIR=/path/to/dir
```

This writes the boot files into `DIR`: the firmware files from RobCo-Core's `tools/fetch_firmware.sh`, `PIPOS.IMG`, `config.txt` and `PIPBOY/PIPOS.CFG` unless one exists. Copy them to the FAT32 boot partition of the card. It never writes a device. Put `ROM1.ROM` in `PIPBOY/` yourself. The kernel has not been tested on hardware yet.

`config.txt` holds exactly the settings of the Pip-Boy chain from RobCo-Core's [BOARD_SUPPORT.md](https://github.com/RobCo-Industries/RobCo-Core/blob/1e0fb655ea09497d133c6e3c3b086a2bb8cbca0e/docs/spec/BOARD_SUPPORT.md#configtxt).
