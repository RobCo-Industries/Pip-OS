# ROM development

A ROM is the program Pip-OS runs after boot. It is one file, `/PIPBOY/ROM1.ROM` on the SD card. Pip-OS loads it at `0x00400000`, checks it and runs it in user mode. The ROM talks to the kernel only through system calls.

The file uses the `rimg` format with the `ROM1` magic and a META block for the title and icon. RobCo-Core specifies it in [IMAGE_FORMAT.md](https://github.com/RobCo-Industries/RobCo-Core/blob/891b3a25a1e38f88c214ef171f9c05c2f36bc6a1/docs/spec/IMAGE_FORMAT.md). The SDK in `sdk/` builds and seals it for you.

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
| `KIND` | `rom` or `holotape`. Sets the magic, `ROM1` or `ROBCO78`, the link script and the slot. Subtype 0 for both. |
| `NAME` | Name in the header, at most 16 bytes. Required. |
| `SRCS` | C and assembly sources. Required. |
| `TITLE` | META title, ASCII, at most 64 bytes |
| `ICON` | META icon: a 32x32 PBM, P1 or P4, or 128 raw bytes |
| `VERSION` | Image version, such as `1.2.0` |
| `BUILD` | Output folder, `build` by default |
| `CFLAGS_EXTRA` | More compiler flags |
| `ROBCO_CORE` | RobCo-Core, `$(PIPOS_SDK)/../external/robco-core` by default |
| `CI=1` | Warnings become errors |

Targets are `all` and `clean`. The fragment compiles with the kernel's CPU flags: ARMv6, ARM state, soft float, `-ffreestanding -nostdlib`. It builds `libpip.a` in `$(BUILD)/libpip`, links with `-lgcc` for division and other compiler helpers, seals with `rimg.py seal` and checks the result with `rimg.py verify`.

`make` rebuilds what a change affects: an edited source or header, a new setting, a source added to or removed from `SRCS`. Every object lands under `$(BUILD)/obj` with its source extension, so `start.c` and `start.S` can sit side by side. If sealing or verifying fails, no image is left behind. Paths with spaces are not supported.

A holotape builds the same way with `KIND=holotape`. It links at `0x00800000`.

## Writing a holotape

A holotape is a small program the ROM starts. Start from [`sdk/examples/tape`](../sdk/examples/tape), a tape named `BEACON`. It shows its title and argument string, draws a few shapes, plays a short tune and waits. UP and DOWN change the exit status it shows, SELECT ends it with that status.

What differs from a ROM:

- `KIND=holotape` in the Makefile. The image gets the `ROBCO78` magic, subtype 0 (game), and links at `0x00800000` in the 4 MiB holotape slot.
- It starts with sp at `0x02000000` and its own heap from `0x01800000`. `pip_mem_info()` reports them.
- It can read the ROM slot but not write it. The ROM's heap, stack and file handles are out of reach.
- `holo_launch`, `power` and `set_time` return `-PIP_EPERM`.
- Its arguments are the string the ROM passed to `holo_launch`, at most 99 bytes, in `startup->args`.
- The return value of `pip_main`, or the value given to `pip_exit()`, is the exit status, 0 to 255. The ROM gets it back from `pip_holo_launch()`. A fault ends the tape and the ROM gets `-PIP_EFAULT`.
- Pip-OS stops the sound, empties the event queue and resets the drawing state before and after the tape. The ROM redraws its screen when the tape ends.

To put a tape on the card, copy it to `/PIPBOY/HOLO/` under an 8.3 name ending in `.HTP`. `make sdimage` already puts the example there as `/PIPBOY/HOLO/BEACON.HTP`. The rules are in RobCo-Core's [PIPOS_ABI.md](https://github.com/RobCo-Industries/RobCo-Core/blob/891b3a25a1e38f88c214ef171f9c05c2f36bc6a1/docs/spec/PIPOS_ABI.md#holotapes).

## Start-up and entry state

`crt0.S` puts the branch, the `rimg` header and an empty META block at the start of the image. On entry it saves r0 to r3, sp and lr in `pip_entry_regs`, clears BSS and calls `int pip_main(const pipos_startup_t *startup)`. The return value of `pip_main` goes to `exit`.

The ROM starts in user mode with sp at `0x01800000` and r0 pointing at the startup block. The block names the heap: `0x007F0000` bytes from `0x01000000`. The details are in [PIPOS_ABI.md](https://github.com/RobCo-Industries/RobCo-Core/blob/891b3a25a1e38f88c214ef171f9c05c2f36bc6a1/docs/spec/PIPOS_ABI.md#entry-state).

## System calls

Include `<pipos/pip.h>` for everything, or the single headers in `sdk/include/pipos/`. `libpip.a` has a `pip_<name>()` wrapper for every call of the ABI, declared in `pipos/syscall.h` with the types of the spec. A call the kernel does not serve yet returns `-PIP_ENOSYS`. [API.md](API.md) has the exact results of the served calls.

| Number | Call | Wrapper | Kernel serves it |
|---|---|---|---|
| `0x01` | `draw_line` | `pip_draw_line()` | no |
| `0x02` | `draw_point` | `pip_draw_point()` | no |
| `0x03` | `draw_text` | `pip_draw_text()` | no |
| `0x04` | `clear` | `pip_clear()` | no |
| `0x05` | `gfx_info` | `pip_gfx_info()` | no |
| `0x06` | `rect` | `pip_rect()` | no |
| `0x07` | `polyline` | `pip_polyline()` | no |
| `0x08` | `arc` | `pip_arc()` | no |
| `0x09` | `blit_1bpp` | `pip_blit_1bpp()` | no |
| `0x0A` | `blit_rgb` | `pip_blit_rgb()` | no |
| `0x0B` | `set_clip` | `pip_set_clip()` | no |
| `0x0C` | `set_color` | `pip_set_color()` | no |
| `0x0D` | `text_metrics` | `pip_text_metrics()` | no |
| `0x0E` | `present` | `pip_present()` | no |
| `0x0F` | `set_font` | `pip_set_font()` | no |
| `0x10` | `read_buttons` | `pip_read_buttons()` | no |
| `0x11` | `read_dial` | `pip_read_dial()` | no |
| `0x12` | `poll_event` | `pip_poll_event()` | no |
| `0x13` | `wait_event` | `pip_wait_event()` | no |
| `0x20` | `tone` | `pip_tone()` | no |
| `0x21` | `sample` | `pip_sample()` | no |
| `0x22` | `audio_stop` | `pip_audio_stop()` | no |
| `0x23` | `volume` | `pip_volume()` | no |
| `0x24` | `audio_status` | `pip_audio_status()` | no |
| `0x25` | `play_seq` | `pip_play_seq()` | no |
| `0x30` | `read_sensor` | `pip_read_sensor()` | no |
| `0x31` | `sensor_available` | `pip_sensor_available()` | no |
| `0x40` | `get_time` | `pip_get_time()` | no |
| `0x41` | `get_battery` | `pip_get_battery()` | no |
| `0x42` | `ticks_ms` | `pip_ticks_ms()` | yes |
| `0x43` | `sleep_ms` | `pip_sleep_ms()` | yes |
| `0x44` | `exit` | `pip_exit()` | yes |
| `0x45` | `sys_info` | `pip_sys_info()` | yes |
| `0x46` | `mem_info` | `pip_mem_info()` | yes |
| `0x47` | `power` | `pip_power()` | no |
| `0x48` | `log` | `pip_log()` | yes |
| `0x49` | `rand` | `pip_rand()` | yes |
| `0x4A` | `set_time` | `pip_set_time()` | no |
| `0x50` | `read_save` | `pip_read_save()` | no |
| `0x51` | `write_save` | `pip_write_save()` | no |
| `0x52` | `save_info` | `pip_save_info()` | no |
| `0x53` | `save_delete` | `pip_save_delete()` | no |
| `0x54` | `file_open` | `pip_file_open()` | no |
| `0x55` | `file_read` | `pip_file_read()` | no |
| `0x56` | `file_close` | `pip_file_close()` | no |
| `0x57` | `dir_list` | `pip_dir_list()` | no |
| `0x60` | `holo_enumerate` | `pip_holo_enumerate()` | no |
| `0x61` | `holo_launch` | `pip_holo_launch()` | no |
| `0x62` | `holo_last_status` | `pip_holo_last_status()` | no |
| `0x63` | `holo_eject_event` | `pip_holo_eject_event()` | no |

`pip_syscall(nr, a0, a1, a2, a3)` makes any call by number. A number outside the table returns `-PIP_ENOSYS`.

`rect` and `set_clip` take the width and height packed as `PIP_WH(w, h)`. `pip_rect_xywh()` and `pip_set_clip_xywh()` take them apart. `PIP_TM_WIDTH()` and `PIP_TM_HEIGHT()` unpack the result of `text_metrics`. `abi.h` names every limit of the spec, such as `PIP_TEXT_MAX` and `PIP_SAVE_MAX`.

## Library functions

| Header | Functions |
|---|---|
| `pipos/string.h` | `memcpy`, `memmove`, `memset`, `memcmp`, `strlen`, `strnlen`, `strcmp`, `strncmp`, `strchr`, `strlcpy` |
| `pipos/printf.h` | `pip_snprintf`, `pip_vsnprintf`, `pip_printf` |
| `pipos/heap.h` | `pip_malloc`, `pip_calloc`, `pip_free`, `pip_heap_free`, `pip_heap_init` |

The printf functions know `%d %u %x %X %c %s %p %%` with a `0` flag and a width. There are no `l` modifiers, no precision and no floating point. `pip_printf` prints up to 1023 bytes per call, split into `log` calls of 255 bytes.

The heap sets itself up on the first call. Blocks are 8-byte aligned and cost 16 bytes of header each. `pip_malloc` returns NULL when no free block fits. The headers list every limit.

Do not include `<robco/...>` headers. The `pipos/` headers are the whole interface.

## Put the ROM on a card

`make sdimage` writes `build/sd.img` with the hello example as `/PIPBOY/ROM1.ROM` and the tape example as `/PIPBOY/HOLO/BEACON.HTP`. `ROM=<file>` puts another image there, and `ROM=none` leaves it out. The tape stays on the card in both cases. `TAPE=<file>` puts another holotape there under its own name, and `TAPE=none` leaves it out. `make run` builds the card and boots it in QEMU. `BOARD` picks the board, `WINDOW=1` opens the screen. The keys are in [BUILD.md](BUILD.md#sd-card-image-for-qemu).

```sh
make run ROM=$HOME/myrom/build/ROM1.ROM BOARD=raspi3b WINDOW=1
```

For a real card, copy the file to `PIPBOY/ROM1.ROM` on the boot partition. See [BUILD.md](BUILD.md#a-real-card).

## Faults

If the ROM touches memory it does not own or runs an undefined or privileged instruction, the kernel stops it. The serial console prints `ev=rom_kill` with the reason and the pc, and the screen shows a line such as `ROM(1) DATA ABORT AT 0x00400140`. The kernel then goes on to its main loop. Look the pc up in `build/<image>.map` or with `arm-none-eabi-addr2line -e build/ROM1.elf`. [BOOT.md](BOOT.md#running-the-rom) has the details.

## Limits today

- No graphics, input, audio, time, sensors, saves, files or holotapes yet. These calls return `-PIP_ENOSYS`.
- A ROM that never calls `sleep_ms` keeps the kernel from its main-loop work, so script commands stop.
- `sleep_ms` never ends early, but it can run up to 10 ms long: the 100 Hz tick wakes it.
- `ticks_ms` moves in steps of 10 ms.
