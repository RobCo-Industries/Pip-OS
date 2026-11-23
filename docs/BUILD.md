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

RobCo-Core's [getting started guide](https://github.com/RobCo-Industries/RobCo-Core/blob/d6f756d259d8e6faefa983a719560d19209121d0/docs/guide/GETTING_STARTED.md) lists the versions in detail.

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
| `build/examples/tape/BEACON.HTP` | The SDK's tape example |
| `build/conformance/`, `build/conformance.img` | The conformance ROM, its tapes and its card from `make conformance` and `make conformance-card`, see [TESTING.md](TESTING.md#conformance-rom) |

The image is a `rimg` `BOOT` image with the Pip-OS subtype, linked at `0x8000`. The link fails when the image and its stacks pass `0x00400000`, the end of the Pip-OS slot. `python3 external/robco-core/tools/rimg.py info build/pipos.img` prints its header.

## Layout

| Path | Content |
|---|---|
| `Makefile` | Includes `external/robco-core/mk/robco.mk` and `mk/host.mk` and adds the test, card and QEMU targets |
| `kernel/` | Kernel sources and `robco_config.h` |
| `kernel/syscall.c`, `kernel/sys.h` | The system-call dispatcher and the interface of the call groups |
| `kernel/sys_<group>.c` | One group of system calls each: `system`, `gfx`, `input`, `audio`, `time`, `storage`, `holo` |
| `PURE_SRCS` in the `Makefile` | Pure code, also built for the host tests: the `*_rules.c` files, `gfx_draw.c`, `input_events.c`, `time_clock.c`, `bootlines.c`, `settings.c`, `sysabi.c` and `sysdispatch.c` in `kernel/` |
| `sdk/` | The ROM SDK: headers in `include/pipos/`, `crt0.S`, `rom.ld`, `holotape.ld`, `lib/` for `libpip.a`, `pipos-app.mk`, and the examples `examples/hello` and `examples/tape` |
| `boot/config.txt` | The firmware settings for the Pip-Boy chain |
| `boot/PIPOS.CFG` | Sample kernel configuration, see [CONFIG.md](CONFIG.md) |
| `tools/check_doc_links.sh` | The doc link check of `make check-pins` |
| `conformance/` | The conformance ROM and its tapes, built with the SDK only, see [TESTING.md](TESTING.md#conformance-rom) |
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

`make sdimage` writes `build/sd.img`: a 64 MiB FAT32 card with `PIPOS.IMG`, `config.txt`, `PIPBOY/PIPOS.CFG` from `boot/PIPOS.CFG`, `PIPBOY/ROM1.ROM` and `PIPBOY/HOLO/BEACON.HTP`. The ROM is the SDK's hello example and the holotape is the SDK's tape example. `ROM=<file>` puts another image there, and `ROM=none` leaves the card without a ROM. The holotape does not depend on `ROM`: `TAPE=<file>` puts another holotape in `PIPBOY/HOLO/` under its own name, and `TAPE=none` leaves the folder empty. It downloads no firmware. The `Makefile` lists the configuration in `SDIMAGE_FILES`, and its `sdimage-stage::` rule adds the kernel, `config.txt` and the holotape directory. `make run` builds the card again, with the same `ROM` rule, and boots it with RobCo-Core's runner, `python3 -m harness.run`, the way the firmware does, with the serial console on the terminal. `BOARD` is `raspi0`, `raspi1ap`, `raspi2b` or `raspi3b`. `WINDOW=1` also opens the QEMU display window. Press Ctrl-A, then X, to quit.

The buttons come from the serial console, so type in the terminal that runs `make run`, not in the QEMU window:

| Key | Button |
|---|---|
| Up arrow, Down arrow | UP, DOWN |
| Left arrow, Right arrow | LEFT, RIGHT |
| Enter | SELECT |
| Backspace, Escape | BACK, seen only by the kernel |

The console has no key for the dial. On the default card, the hello example lists `BEACON.HTP`. Press Enter to run it, Up or Down to change its exit status, and Enter again to go back. The ROM then shows `Last run: exit status 3`, or the status you picked. Left ends the ROM.

## A real card

```sh
make bootdir DIR=/path/to/dir
```

This writes the boot files into `DIR`: the firmware files from RobCo-Core's `tools/fetch_firmware.sh`, `PIPOS.IMG`, `config.txt`, an empty `PIPBOY/HOLO/` and `PIPBOY/PIPOS.CFG` unless one exists. Copy them to the FAT32 boot partition of the card. It never writes a device. Put `ROM1.ROM` in `PIPBOY/` yourself, such as `build/examples/hello/ROM1.ROM`, and holotapes in `PIPBOY/HOLO/`, such as `build/examples/tape/BEACON.HTP`. `make sdk-example` builds both. The kernel has not been tested on hardware yet.

`make conformance-bootdir DIR=/path/to/dir` writes the same files, plus the conformance ROM as `PIPBOY/ROM1.ROM` and its two holotapes in `PIPBOY/HOLO/`. See [HARDWARE_VALIDATION.md](HARDWARE_VALIDATION.md). With `FIRMWARE=0`, both targets leave the firmware out and need no network.

`config.txt` holds exactly the settings of the Pip-Boy chain from RobCo-Core's [BOARD_SUPPORT.md](https://github.com/RobCo-Industries/RobCo-Core/blob/d6f756d259d8e6faefa983a719560d19209121d0/docs/spec/BOARD_SUPPORT.md#configtxt).
