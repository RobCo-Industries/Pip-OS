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

RobCo-Core's [getting started guide](https://github.com/RobCo-Industries/RobCo-Core/blob/00d9a2d1efa10b08ea1c14f087f7e3316647d6a1/docs/guide/GETTING_STARTED.md) lists the versions in detail.

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

## Make targets and variables

| Target | What it does |
|---|---|
| `make`, `make all` | The kernel, the test ROMs and holotapes, the SDK examples and the conformance ROM |
| `make kernel` | `build/pipos.img`, its ELF and map only |
| `make sdimage` | `build/sd.img`, see [SD card image for QEMU](#sd-card-image-for-qemu) |
| `make run` | Builds the card and boots it in QEMU on this terminal |
| `make bootdir DIR=<dir>` | The files of the card plus the firmware, see [A real card](#a-real-card) |
| `make conformance`, `make conformance-card`, `make conformance-bootdir DIR=<dir>` | The conformance ROM, its card and its real-card files |
| `make sdk-example`, `make test-roms` | The SDK examples and the test ROMs only |
| `make test`, `make test-host`, `make test-target` | The tests, see [TESTING.md](TESTING.md) |
| `make check-pins` | The CI pins and the RobCo-Core doc links match the submodule |
| `make clean` | Removes `build/` |

| Variable | Default | Meaning |
|---|---|---|
| `BUILD` | `build` | Output folder. From another repository, pass an absolute path. |
| `ROM` | The hello example | The card's `PIPBOY/ROM1.ROM`, or `none` |
| `TAPE` | The tape example | Holotapes for `PIPBOY/HOLO/`, separated by spaces, or `none`. Each name must end in `.HTP`, and two names may not differ only in case. See [ROM_DEVELOPMENT.md](ROM_DEVELOPMENT.md#using-pip-os-from-another-repository). |
| `CARD_DIR` | unset | A folder tree copied onto the card after the standard files. A file at the path of a standard file, or two paths that differ only in case, stop the build. A name longer than 8.3 gets a warning. |
| `BOARD` | `all` | QEMU board for `test` and `run`. `run` takes `raspi2b` for `all`. |
| `WINDOW` | unset | `WINDOW=1` makes `run` open the display window |
| `DIR` | unset | The folder for `bootdir` and `conformance-bootdir` |
| `FIRMWARE` | unset | `FIRMWARE=0` leaves the firmware out of `bootdir` |
| `CLEAN` | unset | `CLEAN=1` lets `bootdir` remove files an earlier run wrote that this run does not, see [A real card](#a-real-card) |
| `CI` | unset | `CI=1` turns warnings into errors |
| `PYTHON` | `python3` | Python for the tools |

A ROM project in another repository calls these targets with `make -C <pip-os>`. See [Using Pip-OS from another repository](ROM_DEVELOPMENT.md#using-pip-os-from-another-repository).

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

`make sdimage` writes `build/sd.img`: a 64 MiB FAT32 card with `PIPOS.IMG`, `config.txt`, `PIPBOY/PIPOS.CFG` from `boot/PIPOS.CFG`, `PIPBOY/ROM1.ROM` and `PIPBOY/HOLO/BEACON.HTP`. The ROM is the SDK's hello example and the holotape is the SDK's tape example. `ROM=<file>` puts another image there, and `ROM=none` leaves the card without a ROM. The holotape does not depend on `ROM`: `TAPE=<files>` puts other holotapes in `PIPBOY/HOLO/` under their own names, and `TAPE=none` leaves the folder empty. `CARD_DIR=<dir>` copies a folder tree onto the card root after the standard files. The build stops when one of its files has the path of a standard file. A `BOOTDIR.LST` at the root of `CARD_DIR`, in any case, also stops it: that name is reserved for the manifest of `make bootdir`. Then `tools/card.py check-stage` checks the whole staged card, the same check as `make bootdir`: two paths that differ only in case, such as `NAME.TXT` and `name.txt` or a file `Data` and a folder `DATA`, stop the build, because FAT would keep only one. A case-sensitive host can hold both; a case-insensitive one cannot. A `CARD_DIR` path with a name longer than 8.3 gets a warning: see [ROM_DEVELOPMENT.md](ROM_DEVELOPMENT.md#using-pip-os-from-another-repository). Each example builds only when the card uses it. It downloads no firmware. The `Makefile` lists the configuration in `SDIMAGE_FILES`, and its `sdimage-stage::` rule adds the kernel, `config.txt`, the holotape directory and `CARD_DIR` through `tools/card.py stage-card-dir`. See [Links and special files](#links-and-special-files) for what the sources may hold. RobCo-Core's `SDIMAGE_DIR` is refused: use `CARD_DIR`. `make run` builds the card again, with the same `ROM` rule, and boots it with RobCo-Core's runner, `python3 -m harness.run`, the way the firmware does, with the serial console on the terminal. `BOARD` is `raspi0`, `raspi1ap`, `raspi2b` or `raspi3b`. `WINDOW=1` also opens the QEMU display window. Press Ctrl-A, then X, to quit.

The buttons come from the serial console, so type in the terminal that runs `make run`, not in the QEMU window:

| Key | Button |
|---|---|
| Up arrow, Down arrow | UP, DOWN |
| Left arrow, Right arrow | LEFT, RIGHT |
| Enter | SELECT |
| Backspace, Escape | BACK, button 6 of the ROM |

The console has no key for the dial. On the default card, the hello example lists `BEACON.HTP`. Press Enter to run it, Up or Down to change its exit status, and Enter again to go back. The ROM then shows `Last run: exit status 3`, or the status you picked. Left or Backspace ends the ROM.

## A real card

```sh
make bootdir DIR=/path/to/dir
```

This writes the files of `make sdimage` into `DIR`, plus the firmware files from RobCo-Core's `tools/fetch_firmware.sh`: `PIPOS.IMG`, `config.txt`, `PIPBOY/PIPOS.CFG` unless one exists, the ROM as `PIPBOY/ROM1.ROM`, the holotapes in `PIPBOY/HOLO/` and the `CARD_DIR` tree. `ROM`, `TAPE` and `CARD_DIR` work as for `make sdimage`, so by default the card holds the hello example and `BEACON.HTP`. It builds only the kernel and the files it copies. Copy them to the FAT32 boot partition of the card. It never writes a device. The kernel has not been tested on hardware yet.

`make conformance-bootdir DIR=/path/to/dir` writes the same files with the conformance ROM as `PIPBOY/ROM1.ROM` and its two holotapes in `PIPBOY/HOLO/`. See [HARDWARE_VALIDATION.md](HARDWARE_VALIDATION.md). With `FIRMWARE=0`, both targets leave the firmware out and need no network.

### Running bootdir again

Use a new, empty folder and it is one command. `bootdir` never deletes a file in `DIR` unless you ask. It writes `BOOTDIR.LST` in `DIR`, the list of files it wrote. It does not list `PIPBOY/PIPOS.CFG`. The card does not need `BOOTDIR.LST`, but it does no harm there.

`bootdir` first makes a plan: the folders to create, the files to write and, with `CLEAN=1`, the stale files to remove. It checks the whole plan against `DIR` and changes nothing if any check fails. It lists every problem it finds, not only the first. Names compare as on FAT, ignoring case. For each file in `DIR` that this run would not write:

- A file that `BOOTDIR.LST` names is stale, for example the ROM after `ROM=none` or a tape no longer in `TAPE`. `bootdir` stops, lists the files, and changes nothing. Use an empty folder, or run again with `CLEAN=1` to remove them.
- A file that `BOOTDIR.LST` does not name, at a path `bootdir` writes, also stops it, even with `CLEAN=1`. These paths are the firmware files, `PIPOS.IMG`, `config.txt`, `PIPBOY/ROM1.ROM` and `PIPBOY/HOLO/*.HTP`. This happens with a folder from before `BOOTDIR.LST` existed. `bootdir` cannot tell whether the file is stale, so delete or move it yourself, or use an empty folder.
- Any other file is yours. `bootdir` leaves it and names it once.

A file or folder in `DIR` whose name differs only in case from one `bootdir` writes, such as `pipos.img` or `pipboy/`, also stops it, even with `CLEAN=1`. So does a file where the plan needs a folder, such as a file `DATA` when `CARD_DIR` holds `DATA/README.TXT`, or a folder where it writes a file, at any depth. A stale file that `CLEAN=1` removes is not in the way. A file it must overwrite or remove, or a folder it must create in, write into or remove from, that is not writable stops it too. A symbolic link at a path it writes or removes, or at a folder on the way to one, stops it: see [Links and special files](#links-and-special-files). So does a `BOOTDIR.LST` line that is not a plain relative path inside `DIR`, such as `../X`, `/etc/X`, `A//B` or `PIPBOY/`. Blank lines and Windows line ends are fine. Fix these yourself, or use an empty folder.

"Nothing changed" holds for every stop above: the checks only read `DIR`. They cannot foresee a full disk or another program changing `DIR` at the same time. `bootdir` writes each file under a temporary name in its folder and then renames it, so a crash leaves each file either old or new. If a write fails after the checks passed, `bootdir` says so and that `DIR` is partly updated. Fix the cause and run it again.

`CLEAN=1` removes only files that `BOOTDIR.LST` names and this run does not write. It never removes `PIPBOY/PIPOS.CFG`. `conformance-bootdir` follows the same rules.

### Links and special files

FAT holds only files and folders. `tools/card.py` does all the card staging and checks, and it never follows a symbolic link it did not get on the command line:

- The sources hold regular files and folders only. A `TAPE` file, the `ROM`, or anything inside `CARD_DIR` or the staged card that is a symbolic link, a device, a FIFO or a socket stops the build and is named. A link could copy a file from outside the project onto the card. A `ROM` or `TAPE` that is a folder also stops the build, for `sdimage`, `run` and `bootdir` alike. `CARD_DIR` itself may be a link to a folder.
- In `DIR`, a link at any path `bootdir` writes or removes stops it, and nothing changes. That covers the files and folders it writes, every folder on the way to them, the stale files `CLEAN=1` removes and `BOOTDIR.LST`. A link elsewhere in `DIR` is left alone. A `PIPBOY/PIPOS.CFG` that exists, even as a link, is kept and never written.
- `DIR` itself may be a link to a folder, for example when you keep the card folder somewhere else. `bootdir` resolves it once, says so, and writes into the real folder.

`config.txt` holds exactly the settings of the Pip-Boy chain from RobCo-Core's [BOARD_SUPPORT.md](https://github.com/RobCo-Industries/RobCo-Core/blob/00d9a2d1efa10b08ea1c14f087f7e3316647d6a1/docs/spec/BOARD_SUPPORT.md#configtxt).
