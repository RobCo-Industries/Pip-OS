# ROM development

A ROM is the program Pip-OS runs after boot. It is one file, `/PIPBOY/ROM1.ROM` on the SD card. Pip-OS loads it at `0x00400000`, checks it and runs it in user mode. The ROM talks to the kernel only through system calls.

The file uses the `rimg` format with the `ROM1` magic and a META block for the title and icon. RobCo-Core specifies it in [IMAGE_FORMAT.md](https://github.com/RobCo-Industries/RobCo-Core/blob/2ef412b25aad06e20eae85bacc4ea43c6d930085/docs/spec/IMAGE_FORMAT.md). The SDK in `sdk/` builds and seals it for you.

## A first ROM

Start from [`sdk/examples/hello`](../sdk/examples/hello). It prints a few lines, shows `sys_info` and `mem_info` and sleeps half a second. Then it lists the holotapes on the card and runs the one you pick, see [Writing a holotape](#writing-a-holotape). LEFT ends it with status 0.

1. Copy the folder somewhere: `cp -R sdk/examples/hello ~/myrom`.
2. Build it, pointing `PIPOS_SDK` at the `sdk/` folder of a Pip-OS checkout with its submodule:

   ```sh
   make -C ~/myrom PIPOS_SDK=/path/to/Pip-OS/sdk
   ```

3. The result is `~/myrom/build/ROM1.ROM`. The build already ran `rimg.py verify` on it.
4. Run it in QEMU from the Pip-OS folder:

   ```sh
   make run ROM=$HOME/myrom/build/ROM1.ROM WINDOW=1
   ```

The tools are the ones Pip-OS needs, see [BUILD.md](BUILD.md#tools). In a ROM project that has Pip-OS as a submodule at `external/pip-os`, set `PIPOS_SDK := external/pip-os/sdk` in the Makefile instead.

## The Makefile

```make
PIPOS_SDK := external/pip-os/sdk
IMAGE     := ROM1.ROM
KIND      := rom
NAME      := DEITRIX
SRCS      := src/main.c src/ui.c
include $(PIPOS_SDK)/pipos-app.mk
```

| Variable | Meaning |
|---|---|
| `PIPOS_SDK` | The `sdk/` folder, relative or absolute. Required. |
| `IMAGE` | Output file name inside `BUILD`. Required. |
| `KIND` | `rom` or `holotape`. Sets the magic, `ROM1` or `ROBCO78`, the link script and the slot. |
| `NAME` | Name in the header, at most 16 bytes. Required. |
| `SRCS` | C and assembly sources. Required. |
| `TITLE` | META title, ASCII, at most 64 bytes |
| `ICON` | META icon: a 32x32 PBM, P1 or P4, or 128 raw bytes |
| `VERSION` | Image version, such as `1.2.0` |
| `SUBTYPE` | Holotape subtype: `game` or `0`, the default, or `utility` or `1`. A data tape, `2`, has no slot, so the build refuses it. A ROM takes only `0`. A change reseals the image. |
| `ABI` | The lowest Pip-OS ABI the image needs, `MAJOR.MINOR`, such as `1.3`. See [The target ABI](#the-target-abi). Not set, the image targets the ABI of the headers, 1.4. |
| `BUILD` | Output folder, `build` by default |
| `CFLAGS_EXTRA` | More compiler flags |
| `ROBCO_CORE` | RobCo-Core, `$(PIPOS_SDK)/../external/robco-core` by default |
| `CI=1` | Warnings become errors |
| `FLAGS_JSON` | Where `flags-json` writes, `$(BUILD)/flags.json` by default |

Targets are `all`, `clean` and `flags-json`. The fragment compiles with the kernel's CPU flags: ARMv6, ARM state, soft float, `-ffreestanding -nostdlib`. It builds `libpip.a` in `$(BUILD)/libpip`, links with `-lgcc` for division and other compiler helpers, seals with `rimg.py seal` and checks the result with `rimg.py verify`.

`make` rebuilds what a change affects: an edited source or header, a new setting, a source added to or removed from `SRCS`. Every object lands under `$(BUILD)/obj` with its source extension, so `start.c` and `start.S` can sit side by side. If sealing or verifying fails, no image is left behind. Paths with spaces are not supported.

A holotape builds the same way with `KIND=holotape`. It links at `0x00800000`.

`libpip.a` is archived with `ar rcsD`, which stores no dates or ids, so two builds from one commit give the same archive.

### The target ABI

`ABI := 1.3` states the lowest ABI the image needs. `pipos-app.mk` passes `-DPIPOS_ABI_TARGET=0x00010003` to `crt0.S` and to every source of `SRCS`, never to `libpip.a`.

- `crt0.S` writes the value into the header's `abi_version`. The loader checks only the major, so the image loads on every 1.x Pip-OS.
- The headers hide the wrapper and the `PIP_SYS_*` number of every call added after the target. A use fails to compile with an error that names the call and the ABI that added it.
- Structs, error codes and the constants of newer ABIs stay visible, such as `PIP_HW_TAPE`, `PIP_EV_TAPE` and `PIP_HOLO_EV_*`.
- A target above the headers' ABI fails to compile, with both versions in the error.
- Make refuses a value that is not `MAJOR.MINOR` in decimal, a leading zero, a major other than 1 or a minor above 65535, before it compiles anything.
- Without `ABI`, no define is passed and the image has the same bytes as before the variable existed.

The calls hidden by target, from the spec's change list:

| Target | Hidden |
|---|---|
| 1.0 | `sample_queue` |
| 1.1 to 1.4 | Nothing |

ABI 1.2, 1.3 and 1.4 added no call. A change of `ABI` rebuilds the startup code and every source.

### flags.json

`make flags-json` writes the build flags as the section "flags.json" of RobCo-Core's `docs/spec/SDK_PACKAGE.md` describes them, for a tool that builds without `make`. It writes the variables the build rules use, so the file and the build cannot drift apart. Paths are relative to a package root: `include`, `lib/crt0.S`, `lib/holotape.ld`, `lib/rom.ld`, `lib/libpip.a`. No flag holds a path.

The file leaves out what is the project's or `make`'s own: `CFLAGS_EXTRA`, `-Werror` of `CI=1`, `-MMD -MP` and the map file. `-Wl,--no-warn-rwx-segments` is in `ldflags` only when the linker of the machine that writes the file knows it.

`tests/host/flags_json_test.sh` builds the example tape and the example ROM twice, by `make` and by `tests/host/flags_json_build.py`, which follows the spec's build order and reads only `flags.json` and a package folder. Both give the same SHA-256. This is tested on one machine only.

`make sdk-package` puts `flags.json` in the SDK package with the headers, `libpip.a`, `crt0.S`, the linker scripts and the kernel. See [SDK_PACKAGE.md](SDK_PACKAGE.md).

## Writing a holotape

A holotape is a small program the ROM starts. Start from [`sdk/examples/tape`](../sdk/examples/tape), a tape named `BEACON`. It shows its title and argument string, draws a few shapes, plays a short tune and waits. UP and DOWN change the exit status it shows, SELECT ends it with that status.

What differs from a ROM:

- `KIND=holotape` in the Makefile. The image gets the `ROBCO78` magic and links at `0x00800000` in the 4 MiB holotape slot. Its subtype is game, 0, unless `SUBTYPE=utility` makes it a utility, 1. `holo_enumerate` reports it.
- It starts with sp at `0x02000000` and its own heap from `0x01800000`. `pip_mem_info()` reports them.
- It can read the ROM slot but not write it. The ROM's heap, stack and file handles are out of reach.
- `holo_launch`, `power` and `set_time` return `-PIP_EPERM`.
- Its arguments are the string the ROM passed to `holo_launch`, at most 99 bytes, in `startup->args`.
- The return value of `pip_main`, or the value given to `pip_exit()`, is the exit status, 0 to 255. The ROM gets it back from `pip_holo_launch()`. A fault ends the tape and the ROM gets `-PIP_EFAULT`.
- Pip-OS stops the sound, empties the event queue and resets the drawing state before and after the tape. The ROM redraws its screen when the tape ends.

To put a tape on the card, copy it to `/PIPBOY/HOLO/` under an 8.3 name ending in `.HTP`. `make sdimage` already puts the example there as `/PIPBOY/HOLO/BEACON.HTP`. The rules are in RobCo-Core's [PIPOS_ABI.md](https://github.com/RobCo-Industries/RobCo-Core/blob/2ef412b25aad06e20eae85bacc4ea43c6d930085/docs/spec/PIPOS_ABI.md#holotapes).

### The holotape slot

Since ABI 1.4, tapes can also sit in the root folder of a removable card in the holotape slot. A ROM that offers them runs this loop:

1. At start, read `hw_flags` with `pip_sys_info()`. With `PIP_HW_TAPE` clear, the tape list changes only when the boot card does.
2. Call `pip_holo_enumerate()` and show the list. `PIP_HOLO_SLOT` in `flags` marks the slot tapes.
3. On each `PIP_EV_TAPE` event, and after each `pip_holo_launch()` returns, call `pip_holo_eject_event()`.
4. When it returns a non-zero mask, call `pip_holo_enumerate()` again. Show a message on `PIP_HOLO_EV_BAD` and drop it on `PIP_HOLO_EV_REMOVED`.

```c
pip_event_t ev;

if (pip_wait_event(&ev, PIP_WAIT_FOREVER) == 1 && ev.type == PIP_EV_TAPE) {
    if (pip_holo_eject_event() != 0) {
        n = pip_holo_enumerate(list, PIP_HOLO_ENUM_MAX);
    }
}
```

The event is a hint: the queue is emptied around `holo_launch` and can drop events. `pip_holo_eject_event()` is the truth. `pip_holo_launch()` returns `-PIP_ENODEV` when the slot card of the running tape was pulled. The kernel does its slot work only inside `sleep_ms`, `poll_event`, `wait_event`, `read_buttons`, `read_dial`, `file_read`, `holo_enumerate` and `holo_eject_event`. A ROM that calls none of them sees no change.

## Start-up and entry state

`crt0.S` puts the branch, the `rimg` header and an empty META block at the start of the image. On entry it saves r0 to r3, sp and lr in `pip_entry_regs`, clears BSS and calls `int pip_main(const pipos_startup_t *startup)`. The return value of `pip_main` goes to `exit`.

The ROM starts in user mode with sp at `0x01800000` and r0 pointing at the startup block. The block names the heap: `0x007F0000` bytes from `0x01000000`. The details are in [PIPOS_ABI.md](https://github.com/RobCo-Industries/RobCo-Core/blob/2ef412b25aad06e20eae85bacc4ea43c6d930085/docs/spec/PIPOS_ABI.md#entry-state).

## System calls

Include `<pipos/pip.h>` for everything, or the single headers in `sdk/include/pipos/`. `libpip.a` has a `pip_<name>()` wrapper for every call of the ABI, declared in `pipos/syscall.h` with the types of the spec. The kernel serves every call. [API.md](API.md) has the results, the errors and the limits of each.

| Number | Call | Wrapper |
|---|---|---|
| `0x01` | `draw_line` | `pip_draw_line()` |
| `0x02` | `draw_point` | `pip_draw_point()` |
| `0x03` | `draw_text` | `pip_draw_text()` |
| `0x04` | `clear` | `pip_clear()` |
| `0x05` | `gfx_info` | `pip_gfx_info()` |
| `0x06` | `rect` | `pip_rect()` |
| `0x07` | `polyline` | `pip_polyline()` |
| `0x08` | `arc` | `pip_arc()` |
| `0x09` | `blit_1bpp` | `pip_blit_1bpp()` |
| `0x0A` | `blit_rgb` | `pip_blit_rgb()` |
| `0x0B` | `set_clip` | `pip_set_clip()` |
| `0x0C` | `set_color` | `pip_set_color()` |
| `0x0D` | `text_metrics` | `pip_text_metrics()` |
| `0x0E` | `present` | `pip_present()` |
| `0x0F` | `set_font` | `pip_set_font()` |
| `0x10` | `read_buttons` | `pip_read_buttons()` |
| `0x11` | `read_dial` | `pip_read_dial()` |
| `0x12` | `poll_event` | `pip_poll_event()` |
| `0x13` | `wait_event` | `pip_wait_event()` |
| `0x20` | `tone` | `pip_tone()` |
| `0x21` | `sample` | `pip_sample()` |
| `0x22` | `audio_stop` | `pip_audio_stop()` |
| `0x23` | `volume` | `pip_volume()` |
| `0x24` | `audio_status` | `pip_audio_status()` |
| `0x25` | `play_seq` | `pip_play_seq()` |
| `0x26` | `sample_queue` | `pip_sample_queue()` |
| `0x30` | `read_sensor` | `pip_read_sensor()` |
| `0x31` | `sensor_available` | `pip_sensor_available()` |
| `0x40` | `get_time` | `pip_get_time()` |
| `0x41` | `get_battery` | `pip_get_battery()` |
| `0x42` | `ticks_ms` | `pip_ticks_ms()` |
| `0x43` | `sleep_ms` | `pip_sleep_ms()` |
| `0x44` | `exit` | `pip_exit()` |
| `0x45` | `sys_info` | `pip_sys_info()` |
| `0x46` | `mem_info` | `pip_mem_info()` |
| `0x47` | `power` | `pip_power()` |
| `0x48` | `log` | `pip_log()` |
| `0x49` | `rand` | `pip_rand()` |
| `0x4A` | `set_time` | `pip_set_time()` |
| `0x50` | `read_save` | `pip_read_save()` |
| `0x51` | `write_save` | `pip_write_save()` |
| `0x52` | `save_info` | `pip_save_info()` |
| `0x53` | `save_delete` | `pip_save_delete()` |
| `0x54` | `file_open` | `pip_file_open()` |
| `0x55` | `file_read` | `pip_file_read()` |
| `0x56` | `file_close` | `pip_file_close()` |
| `0x57` | `dir_list` | `pip_dir_list()` |
| `0x60` | `holo_enumerate` | `pip_holo_enumerate()` |
| `0x61` | `holo_launch` | `pip_holo_launch()` |
| `0x62` | `holo_last_status` | `pip_holo_last_status()` |
| `0x63` | `holo_eject_event` | `pip_holo_eject_event()` |

`pip_syscall(nr, a0, a1, a2, a3)` makes any call by number. A number outside the table returns `-PIP_ENOSYS`.

`rect` and `set_clip` take the width and height packed as `PIP_WH(w, h)`. `pip_rect_xywh()` and `pip_set_clip_xywh()` take them apart. `PIP_TM_WIDTH()` and `PIP_TM_HEIGHT()` unpack the result of `text_metrics`. `abi.h` names every limit of the spec, such as `PIP_TEXT_MAX` and `PIP_SAVE_MAX`.

## Playing long audio

`sample` plays at most 65536 bytes, 8.192 s at 8000 Hz. For longer sounds, read the data in chunks and pass each one to `pip_sample_queue()`. The kernel copies the chunk, so one buffer is enough. It plays the chunk right after the one that plays. When a chunk already waits, the call returns `-PIP_EBUSY`: do other work or sleep, then call again with the same chunk.

```c
static uint8_t chunk[4096];

void play_file(const char *path)
{
    int32_t fd = pip_file_open(path);
    int32_t n;

    if (fd < 0) {
        return;
    }
    while ((n = pip_file_read(fd, chunk, sizeof chunk)) > 0) {
        while (pip_sample_queue(chunk, (uint32_t)n) == -PIP_EBUSY) {
            pip_sleep_ms(20);   /* or draw a frame, read input */
        }
    }
    pip_file_close(fd);
}
```

A 4096-byte chunk lasts 512 ms, so a 20 ms sleep leaves plenty of margin. If the ROM comes back too late, the sound ends and the next call starts it again, with a gap. `sample_queue` is new in ABI 1.1: on a 1.0 Pip-OS it returns `-PIP_ENOSYS`. Read `startup->abi_version` to check.

## Library functions

| Header | Functions |
|---|---|
| `pipos/string.h` | `memcpy`, `memmove`, `memset`, `memcmp`, `strlen`, `strnlen`, `strcmp`, `strncmp`, `strchr`, `strlcpy` |
| `pipos/printf.h` | `pip_snprintf`, `pip_vsnprintf`, `pip_printf` |
| `pipos/heap.h` | `pip_malloc`, `pip_calloc`, `pip_free`, `pip_heap_free`, `pip_heap_init` |
| `pipos/ini.h` | `pip_ini_parse`, `pip_ini_get`, `pip_ini_get_int`, `pip_ini_get_bool`, `pip_ini_strerror` |
| `pipos/crc32.h` | `pip_crc32`, `pip_crc32_update` |
| `pipos/selftest.h` | `pip_check`, `pip_check_eq`, `pip_checks_done`, `pip_marker_begin`, `pip_marker_hex`, `pip_marker_dec`, `pip_marker_end`, `pip_marker_time` |

The printf functions know `%d %u %x %X %c %s %p %%` with the `-` and `0` flags, a width and a precision, as in C. `*` takes the width or precision from an `int` argument. There are no `l` modifiers and no floating point. `pip_printf` prints up to 1023 bytes per call, split into `log` calls of 255 bytes.

The heap sets itself up on the first call. Blocks are 8-byte aligned and cost 16 bytes of header each. `pip_malloc` returns NULL when no free block fits. The headers list every limit.

The INI functions read a config file held in memory, such as one read with `file_read`. They allocate nothing. Keys before the first `[section]` belong to the section `""`. `pip_ini_get_int` takes decimal or `0x` hex within a range, and `pip_ini_get_bool` takes `true`, `yes`, `on`, `1` and their opposites. A line holds at most 255 bytes, a value 127. `pipos/ini.h` gives the full syntax and the error codes.

`pip_crc32` is CRC-32/ISO-HDLC, the zlib `crc32()` and the CRC of the image header. `pip_crc32("123456789", 9)` is `0xCBF43926`. To checksum data in pieces, start `pip_crc32_update` with 0 and pass each result back in.

The `selftest.h` functions print the check lines that the tests read, see [TESTING.md](TESTING.md#testing-a-rom-project).

Do not include `<robco/...>` headers. The `pipos/` headers are the whole interface.

## Put the ROM on a card

`make sdimage` writes `build/sd.img` with the hello example as `/PIPBOY/ROM1.ROM` and the tape example as `/PIPBOY/HOLO/BEACON.HTP`. `ROM=<file>` puts another image there, and `ROM=none` leaves it out. The tape stays on the card in both cases. `TAPE=<files>` puts other holotapes there under their own names, and `TAPE=none` leaves it out. `CARD_DIR=<dir>` copies a folder tree onto the card, see [Using Pip-OS from another repository](#using-pip-os-from-another-repository). `make run` builds the card and boots it in QEMU. `BOARD` picks the board, `WINDOW=1` opens the screen. The keys are in [BUILD.md](BUILD.md#sd-card-image-for-qemu).

```sh
make run ROM=$HOME/myrom/build/ROM1.ROM BOARD=raspi3b WINDOW=1
```

For a real card, copy the file to `PIPBOY/ROM1.ROM` on the boot partition. See [BUILD.md](BUILD.md#a-real-card).

## Using Pip-OS from another repository

A ROM project in a repository of its own adds Pip-OS as a git submodule, for example in `external/pip-os`, and fetches it with `git submodule update --init --recursive`. It builds its ROM with `pipos-app.mk`, then calls Pip-OS's own Makefile for the kernel, the card and QEMU:

```sh
make -C external/pip-os <target> BUILD=<absolute folder> ...
```

Pass absolute paths. `make -C` runs in the Pip-OS folder, so a relative path would point inside it. With `ROM=<file>` and `TAPE=none` or `TAPE=<files>`, none of these targets builds the bundled examples, the test ROMs or the conformance ROM.

| Target | What it does |
|---|---|
| `kernel` | `$(BUILD)/pipos.img` and `$(BUILD)/pipos.elf` only |
| `sdimage` | `$(BUILD)/sd.img`, the card for QEMU |
| `run` | Builds the card and boots it in QEMU on this terminal |
| `bootdir DIR=<dir>` | The files of the card for a real one, firmware included |

| Variable | Meaning |
|---|---|
| `BUILD` | Output folder. Use an absolute path outside the Pip-OS tree. |
| `ROM` | The card's `PIPBOY/ROM1.ROM`. `none` leaves it out. The hello example by default. |
| `TAPE` | One or more holotapes for `PIPBOY/HOLO/`, separated by spaces. Each keeps its file name. Two names that differ only in case stop the build, because FAT ignores case. A name that does not end in `.HTP` stops it too, since `holo_enumerate` never lists it. A longer name than 8.3 `NAME.HTP` gets a warning: ROMs see it under a FAT short name such as `LONGNA~1.HTP`. `none` leaves the folder empty. The tape example by default. |
| `CARD_DIR` | A folder whose tree is copied onto the card root after the standard files, such as `content/PIPBOY/DATA/README.TXT`. |
| `BOARD`, `WINDOW` | The QEMU board for `run`, and `WINDOW=1` for the display window |
| `DIR`, `FIRMWARE`, `CLEAN` | The folder for `bootdir`, `FIRMWARE=0` to leave the firmware out, and `CLEAN=1` to remove files an earlier `bootdir` run wrote, see [Running bootdir again](BUILD.md#running-bootdir-again) |

The standard files are `PIPOS.IMG`, `config.txt`, `PIPBOY/PIPOS.CFG`, `PIPBOY/ROM1.ROM`, the `TAPE` files and, for `bootdir`, the firmware. When a file in `CARD_DIR` has the path of a standard file, the build stops and writes nothing. FAT ignores case, so `pipboy/rom1.rom` counts as `PIPBOY/ROM1.ROM`. A folder in `CARD_DIR` that differs from a standard folder only in case also stops the build. `BOOTDIR.LST` at the root of `CARD_DIR`, in any case, stops `sdimage`, `run` and `bootdir`: that name is reserved for the manifest `bootdir` writes there, and deeper paths such as `DATA/BOOTDIR.LST` are fine. Folders with the same name merge, so `CARD_DIR` can add tapes to `PIPBOY/HOLO/`. `CARD_DIR` holds regular files and folders only: a symbolic link, a device, a FIFO or a socket inside it stops the build, and so does a `ROM` or `TAPE` file that is one. A `ROM` or `TAPE` that is a folder stops the build too. `CARD_DIR` itself may be a link to a folder, see [Links and special files](BUILD.md#links-and-special-files). Use `TAPE` for the tapes you build and `CARD_DIR` for files you keep in the repository.

After staging, the build checks the whole card at once, for `sdimage`, `run` and `bootdir` alike. Two paths that differ only in case, such as `NAME.TXT` and `name.txt`, or a file and a folder such as `Data` and `DATA/`, stop it: FAT would keep only one. A ROM opens these files with `file_open`, which takes 8.3 names and upper-cases the path. So case does not matter: `content/data/level1.bin` opens as `/DATA/LEVEL1.BIN`. A longer name, such as `LongLevelName.bin`, is stored under a FAT alias such as `LONGLE~1.BIN`. A ROM can open it only by that alias, which it finds with `dir_list`, so the build warns and names the file. Use 8.3 names for files a ROM reads.

`bootdir` keeps a `PIPBOY/PIPOS.CFG` that is already in `DIR`, because it holds the user's settings.

A complete Makefile for such a project:

```make
PIP_OS     := external/pip-os
BUILD      ?= build
BOARD      ?= raspi2b

PIPOS_SDK  := $(PIP_OS)/sdk
IMAGE      := ROM1.ROM
KIND       := rom
NAME       := MYROM
TITLE      := My ROM
VERSION    := 0.1.0
SRCS       := $(sort $(wildcard src/*.c))
include $(PIPOS_SDK)/pipos-app.mk

PIPOS_MAKE := $(MAKE) --no-print-directory -C $(PIP_OS) BUILD=$(abspath $(BUILD))/pipos
CARD_VARS  := ROM=$(abspath $(PIP_OUT)) TAPE=none CARD_DIR=$(abspath content)

.PHONY: kernel sdimage run bootdir
kernel:
	$(PIPOS_MAKE) kernel

sdimage: all
	$(PIPOS_MAKE) sdimage $(CARD_VARS)

run: all
	$(PIPOS_MAKE) run $(CARD_VARS) BOARD=$(BOARD) WINDOW=$(WINDOW)

bootdir: all
	$(PIPOS_MAKE) bootdir $(CARD_VARS) DIR=$(abspath $(DIR)) FIRMWARE=$(FIRMWARE) CLEAN=$(CLEAN)
```

`$(PIP_OUT)` is the sealed image that `pipos-app.mk` builds. The card lands in `build/pipos/sd.img`. For tests, see [TESTING.md](TESTING.md#testing-a-rom-project).

## Faults

If the ROM touches memory it does not own or runs an undefined or privileged instruction, the kernel stops it. The serial console prints `ev=rom_kill` with the reason and the pc, and the screen shows a line such as `ROM(1) DATA ABORT AT 0x00400140`. The kernel then goes on to its main loop. Look the pc up in `build/<image>.map` or with `arm-none-eabi-addr2line -e build/ROM1.elf`. [BOOT.md](BOOT.md#running-the-rom) has the details.

## Limits today

- A ROM that calls none of `sleep_ms`, the input calls, `file_read` or `holo_enumerate` keeps the kernel from its main-loop work, so script commands stop, `kill` included.
- `sleep_ms` never ends early, but it can run up to 10 ms long: the 100 Hz tick wakes it.
- `ticks_ms` moves in steps of 10 ms.
- `set_time` sets the software clock only. It never writes the RTC chip, so the next boot starts again from the chip, or with no time.
- The SPI display, battery monitoring, sensors other than the clock and PCM streaming are not supported. See [HARDWARE.md](HARDWARE.md#not-supported).
- Nothing is validated on real hardware.
