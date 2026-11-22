# ROM development

A ROM is the program Pip-OS runs after boot. It is one file, `/PIPBOY/ROM1.ROM` on the SD card. Pip-OS loads it at `0x00400000`, checks it and runs it in user mode. The ROM talks to the kernel only through system calls.

The file uses the `rimg` format with the `ROM1` magic and a META block for the title and icon. RobCo-Core specifies it in [IMAGE_FORMAT.md](https://github.com/RobCo-Industries/RobCo-Core/blob/bec7ffcf267db93e7bf4344e7e687ccfd86f52f6/docs/spec/IMAGE_FORMAT.md). The SDK in `sdk/` builds and seals it for you.

## A first ROM

Start from [`sdk/examples/hello`](../sdk/examples/hello). It prints a few lines, shows `sys_info` and `mem_info`, sleeps half a second and exits 0.

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

A holotape builds the same way with `KIND=holotape`. It links at `0x00800000`. Pip-OS cannot launch holotapes yet.

## Start-up and entry state

`crt0.S` puts the branch, the `rimg` header and an empty META block at the start of the image. On entry it saves r0 to r3, sp and lr in `pip_entry_regs`, clears BSS and calls `int pip_main(const pipos_startup_t *startup)`. The return value of `pip_main` goes to `exit`.

The ROM starts in user mode with sp at `0x01800000` and r0 pointing at the startup block. The block names the heap: `0x007F0000` bytes from `0x01000000`. The details are in [PIPOS_ABI.md](https://github.com/RobCo-Industries/RobCo-Core/blob/bec7ffcf267db93e7bf4344e7e687ccfd86f52f6/docs/spec/PIPOS_ABI.md#entry-state).

## System calls that work today

Include `<pipos/pip.h>` for everything, or the single headers in `sdk/include/pipos/`. Each call has a `pip_<name>()` wrapper in `libpip.a`. [API.md](API.md) has the exact results.

| Call | Wrapper | Does |
|---|---|---|
| `ticks_ms` | `pip_ticks_ms()` | Milliseconds since boot, 31 bits |
| `sleep_ms` | `pip_sleep_ms(ms)` | Waits. Above `0x7FFFFFFF` it returns `-PIP_ERANGE` at once. |
| `exit` | `pip_exit(status)` | Ends the ROM |
| `sys_info` | `pip_sys_info(&si)` | ABI and Pip-OS versions, board, SoC, RAM size |
| `mem_info` | `pip_mem_info(&mi)` | Heap and stack addresses |
| `log` | `pip_log(text)` | Prints at most 255 bytes on the serial console |
| `rand` | `pip_rand()` | A random number from the hardware generator |

`pip_syscall(nr, a0, a1, a2, a3)` makes any call by number. Every other number returns `-PIP_ENOSYS`.

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

`make sdimage` writes `build/sd.img` with the hello example as `/PIPBOY/ROM1.ROM`. `ROM=<file>` puts another image there, and `ROM=none` leaves it out. `make run` builds the card and boots it in QEMU. `BOARD` picks the board, `WINDOW=1` opens the screen.

```sh
make run ROM=$HOME/myrom/build/ROM1.ROM BOARD=raspi3b WINDOW=1
```

For a real card, copy the file to `PIPBOY/ROM1.ROM` on the boot partition. See [BUILD.md](BUILD.md#a-real-card).

## Faults

If the ROM touches memory it does not own or runs an undefined or privileged instruction, the kernel stops it. The serial console prints `ev=rom_kill` with the reason and the pc, and the screen shows a line such as `ROM(1) DATA ABORT AT 0x00400140`. The kernel then goes on to its main loop. Look the pc up in `build/<image>.map` or with `arm-none-eabi-addr2line -e build/ROM1.elf`. [BOOT.md](BOOT.md#running-the-rom) has the details.

## Limits today

- No graphics, input, audio, time, sensors, saves or files. These calls return `-PIP_ENOSYS`.
- Holotapes build but Pip-OS cannot launch them.
- A ROM that never calls `sleep_ms` keeps the kernel from its main-loop work, so script commands stop.
- `sleep_ms` counts in ticks of the 100 Hz timer.
