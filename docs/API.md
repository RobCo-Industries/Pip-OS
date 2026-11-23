# System call reference

A running image calls the kernel with `svc #0`. The call number goes in r7, the arguments in r0 to r3, and the result comes back in r0. A negative result is `-PIP_E*`. The kernel keeps every register except r0. The contract is RobCo-Core's [PIPOS_ABI.md](https://github.com/RobCo-Industries/RobCo-Core/blob/00d9a2d1efa10b08ea1c14f087f7e3316647d6a1/docs/spec/PIPOS_ABI.md). This page lists what Pip-OS does on top of it.

[`sdk/include/pipos/abi.h`](../sdk/include/pipos/abi.h) holds the call numbers, the error codes, the structs and every limit. [`sdk/include/pipos/syscall.h`](../sdk/include/pipos/syscall.h) declares a `pip_<name>()` wrapper for every call, plus a raw `pip_syscall()`. They live in `libpip.a`. See [ROM_DEVELOPMENT.md](ROM_DEVELOPMENT.md).

Pip-OS implements ABI 1.1, `PIP_ABI_VERSION` `0x00010001`. The SDK seals images with it. The loader checks only the major version, so an image sealed for 1.0 still runs. The kernel serves all 51 calls of the spec's table. A number outside the table returns `-PIP_ENOSYS` (-38). The boot prints the served numbers in the `ev=syscalls` marker.

| Error | Value |
|---|---|
| `-PIP_EPERM` | -1 |
| `-PIP_ENOENT` | -2 |
| `-PIP_EIO` | -5 |
| `-PIP_EBADF` | -9 |
| `-PIP_ENOMEM` | -12 |
| `-PIP_EFAULT` | -14 |
| `-PIP_EBUSY` | -16 |
| `-PIP_EINVAL` | -22 |
| `-PIP_ENOSPC` | -28 |
| `-PIP_ERANGE` | -34 |
| `-PIP_ENOSYS` | -38 |
| `-PIP_ENOEXEC` | -40 |
| `-PIP_ENODEV` | -41 |
| `-PIP_ETIMEDOUT` | -42 |

The tables below drop the `-PIP_` prefix in the error column.

## Pointer checks

The kernel checks every pointer against the caller's regions before it reads or writes. A bad pointer returns `-PIP_EFAULT` and the kernel writes nothing. The regions are in the spec's [Sandbox](https://github.com/RobCo-Industries/RobCo-Core/blob/00d9a2d1efa10b08ea1c14f087f7e3316647d6a1/docs/spec/PIPOS_ABI.md#sandbox) section.

| Case | Result |
|---|---|
| An out struct or buffer not entirely inside the caller's slot or its heap and stack region | `-PIP_EFAULT` |
| A string that reaches a byte outside the regions before its NUL | `-PIP_EFAULT` |
| A string with no NUL within its limit, all inside the regions | `-PIP_EINVAL` |

A holotape's input pointers may also point into the ROM slot. Its output pointers must point into its own slot or heap.

## System

| Number | Name | Arguments | Result | Errors |
|---|---|---|---|---|
| `0x42` | `ticks_ms` | none | Milliseconds since the tick started at boot, masked to 31 bits | none |
| `0x43` | `sleep_ms` | `uint32 ms` | 0 | `ERANGE` for `ms` above `0x7FFFFFFF`, at once |
| `0x44` | `exit` | `int32 status` | Does not return | none |
| `0x45` | `sys_info` | `pip_sys_info_t *out` | 0 | `EFAULT` |
| `0x46` | `mem_info` | `pip_mem_info_t *out` | 0 | `EFAULT` |
| `0x48` | `log` | `const char *text` | 0 | `EFAULT`, `EINVAL` with no NUL in the first 256 bytes |
| `0x49` | `rand` | none | 0 to 2^31 - 1 from the hardware generator | `EIO` when the generator fails |

The rules are in the spec's [System information](https://github.com/RobCo-Industries/RobCo-Core/blob/00d9a2d1efa10b08ea1c14f087f7e3316647d6a1/docs/spec/PIPOS_ABI.md#system-information) section.

- `ticks_ms` moves in steps of 10 ms, the 100 Hz tick.
- `sleep_ms` waits on the 1 MHz system timer, so it never ends early. The tick wakes it, so it can run up to 10 ms long. IRQs stay on, and the kernel runs its main-loop work while it waits.
- The exit status is the low 8 bits of `status`. `exit` prints it as `ev=rom_exit status=<0 to 255>` for the ROM. For a holotape, `holo_launch` returns it.
- `sys_info` writes `size` 32, `abi_version` `0x00010001`, `pipos_version` `(major << 16) | (minor << 8) | patch`, the firmware `board_rev`, `soc` as `0x2835`, `0x2836` or `0x2837`, `mem_size` in bytes, and two reserved words set to 0.
- `mem_info` reports the caller's own region. For the ROM: `heap_base` `0x01000000`, `heap_size` `0x007F0000`, `stack_top` `0x01800000`, `stack_size` `0x00010000`. For a holotape: `0x01800000`, `0x007F0000`, `0x02000000`, `0x00010000`.
- `log` prints on the PL011 with no newline added. Printable ASCII, `\n` and `\t` print as they are, every other byte as `?`.

## Graphics

| Number | Name | Arguments | Result | Errors |
|---|---|---|---|---|
| `0x01` | `draw_line` | `int32 x0, y0, x1, y1` | 0 | none |
| `0x02` | `draw_point` | `int32 x, y`, `uint32 color` | 0 | none |
| `0x03` | `draw_text` | `int32 x, y`, `const char *text`, `uint32 scale` | Width in pixels, 8 x `scale` per byte | `EINVAL` for `scale` outside 1 to 8 or more than 255 bytes, `EFAULT` |
| `0x04` | `clear` | `uint32 color` | 0 | none |
| `0x05` | `gfx_info` | `pip_gfx_info_t *out` | 0 | `EFAULT` |
| `0x06` | `rect` | `int32 x, y`, `uint32 wh`, `uint32 fill` | 0 | `EINVAL` for `fill` above 1 |
| `0x07` | `polyline` | `const int16 *xy`, `uint32 count`, `uint32 closed` | 0 | `EINVAL` for `count` outside 2 to 256 or `closed` above 1, `EFAULT` |
| `0x08` | `arc` | `const pip_arc_t *a` | 0 | `EINVAL` for `radius` outside 1 to 512, `start_deg` above 359, `end_deg` above 360 or `fill` above 1, `EFAULT` |
| `0x09` | `blit_1bpp` | `const pip_blit_t *b` | 0 | `EINVAL` for `w` outside 1 to 320, `h` outside 1 to 240, `stride` below one row or `flags` not 0, `EFAULT` |
| `0x0A` | `blit_rgb` | `const pip_blit_t *b` | 0 | As `blit_1bpp` |
| `0x0B` | `set_clip` | `int32 x, y`, `uint32 wh` | 0 | `EINVAL` when `wh` is not 0 but `w` or `h` is 0 |
| `0x0C` | `set_color` | `uint32 fg, bg` | 0 | none |
| `0x0D` | `text_metrics` | `const char *text`, `uint32 scale` | `(width << 16) \| height` | As `draw_text` |
| `0x0E` | `present` | none | 0 | `ENODEV` with no framebuffer, or one smaller than 320x240 |
| `0x0F` | `set_font` | `uint32 font_id` | 0 | `EINVAL` for any font but 0 |

The drawing rules, the pixel rules of lines, rectangles, arcs and text, and the check order are in the spec's [Display](https://github.com/RobCo-Industries/RobCo-Core/blob/00d9a2d1efa10b08ea1c14f087f7e3316647d6a1/docs/spec/PIPOS_ABI.md#display) section.

- `gfx_info` reports `backend` 0, HDMI, and `fg_default` as the normal text colour of the `PIPOS.CFG` scheme. Green is `0x33E066`.
- The back buffer is 320x240 words at `0x00C00000`, in kernel-only memory. The boot prints `ev=gfx bb=0x00c00000 size=307200 user=0 heap_end=...` and panics if an image could reach that memory or the kernel heap overlaps it.
- `present` scales the back buffer by 2 onto the 640x480 framebuffer and centres it on black. Each `present` takes about 1.4 ms in QEMU.
- When the ROM starts, the back buffer is cleared to black and the drawing state is reset. The boot screen stays until the ROM's first `present`.
- When the ROM ends, by exit, return or fault, the console redraws the whole boot screen and the kernel prints its exit or fault line below it.
- A holotape gets a fresh drawing state when it starts. The ROM's state comes back when the holotape ends.
- The SPI display is not supported.

## Input

| Number | Name | Arguments | Result | Errors |
|---|---|---|---|---|
| `0x10` | `read_buttons` | none | The `PIP_BTN_*_BIT` mask of the buttons down now | none |
| `0x11` | `read_dial` | none | The dial position in degrees, 0 to 359 | none |
| `0x12` | `poll_event` | `pip_event_t *out` | 1 with the oldest event in `*out`, 0 when the queue is empty | `EFAULT` |
| `0x13` | `wait_event` | `pip_event_t *out`, `uint32 timeout_ms` | 1 with the oldest event in `*out` | `ETIMEDOUT`, `ERANGE` for a timeout above `0x7FFFFFFF` other than `PIP_WAIT_FOREVER`, `EFAULT` |

The queue, the event layout, the time stamps and the repeat schedule are in the spec's [Input](https://github.com/RobCo-Industries/RobCo-Core/blob/00d9a2d1efa10b08ea1c14f087f7e3316647d6a1/docs/spec/PIPOS_ABI.md#input) section.

| Input | Event |
|---|---|
| Up, down, left, right, select, dial push | `PIP_EV_BUTTON`, `code` 0 to 5 |
| One dial turn | `PIP_EV_DIAL`, `code` 0, the signed detent count |
| Back, F1 to F4, characters | Nothing. Only the kernel sees them. |

- Events come from the GPIO buttons and dial, the serial console and the script channel alike.
- Every input call and the wait in `sleep_ms` take new input first. `wait_event` runs the kernel's main-loop work while it blocks, so script commands still answer.
- RobCo-Core keeps a 32-event queue in front of the image's queue. It fills only while the kernel does not poll. Pip-OS sets it to drop its oldest event too, so when more than 32 events arrive between two polls, the image gets the newest 32 in order. Characters and kernel-only keys count toward the 32.
- At image start the kernel reads every byte that waits in the UARTs and keeps those events. Bytes that reach a UART later belong to the image, even if the host sent them earlier. The mini UART holds 8 bytes and the console UART 16. QEMU keeps the rest until the kernel reads.
- `read_buttons` is the OR of the buttons each source holds. A release clears only the button its own source pressed.
- `read_dial` starts at 0 at boot.
- The kernel prints an `ev=key` marker for every input event, also while an image runs.

## Audio

| Number | Name | Arguments | Result | Errors |
|---|---|---|---|---|
| `0x20` | `tone` | `uint32 freq_hz, duration_ms` | 0 | `EINVAL` for `freq_hz` outside 20 to 20000 or `duration_ms` outside 1 to 60000, `EIO` |
| `0x21` | `sample` | `const uint8 *data`, `uint32 length` | 0 | `EINVAL` for `length` outside 1 to 65536, `EFAULT`, `EIO` |
| `0x22` | `audio_stop` | none | 0, also when nothing plays | none |
| `0x23` | `volume` | `uint32 level` | The previous level | `EINVAL` for `level` above 100 |
| `0x24` | `audio_status` | none | 1 while a sound the image started plays or a queued sample waits, else 0 | none |
| `0x25` | `play_seq` | `const pip_note_t *notes`, `uint32 count` | 0 | `EINVAL` for `count` outside 1 to 256 or any bad note, `EFAULT`, `EIO` |
| `0x26` | `sample_queue` | `const uint8 *data`, `uint32 length` | 0 | `EINVAL` for `length` outside 1 to 65536, `EFAULT`, `EBUSY` when a sample already waits, `EIO` |

`EIO` means the audio driver refused the sound. The rules are in the spec's [Audio](https://github.com/RobCo-Industries/RobCo-Core/blob/00d9a2d1efa10b08ea1c14f087f7e3316647d6a1/docs/spec/PIPOS_ABI.md#audio) section.

- The level starts at the `volume` key of `PIPOS.CFG`, 100 without one. See [CONFIG.md](CONFIG.md). It is a percentage of the square wave's duty cycle for tones. For a sample it scales each byte around 128.
- A ROM that wants its own level sets it and restores the old one before it exits.
- `sample` stops the current sound before it copies the data. An argument error returns before that, so the current sound plays on.
- `sample_queue` checks the length, then the pointer, then whether a sample waits. When a sample plays, it copies the data and queues it behind, with no gap. When none plays, it acts as `sample`. `sample_queue` is new in ABI 1.1. [ROM_DEVELOPMENT.md](ROM_DEVELOPMENT.md#playing-long-audio) has a streaming loop.
- `tone`, `sample`, `play_seq` and `audio_stop` drop the waiting sample with the one that plays. Image start and end stop both.
- On PWM the sound goes out on the audio pins in [HARDWARE.md](HARDWARE.md). Samples are not tried on real hardware yet.

QEMU has no PWM model. There Pip-OS uses RobCo-Core's trace backend: each change of sound prints one line on the UART instead of playing it.

```
@RC stage=pipos ev=audio op=tone hz=1050 vol=40 t_ms=408
@RC stage=pipos ev=audio op=stop hz=0 vol=40 t_ms=508
```

A sample prints two lines. `crc` is the CRC-32 of the copied data. `op=pcm_end` marks the natural end. A queued sample prints `op=pcm_next` with its `len` and `crc` when it takes over, and `op=pcm_end` comes once, after the last one. A stop or a replacement prints `op=stop` instead.

```
@RC stage=pipos ev=audio op=pcm rate=8000 len=800 vol=70 crc=eb779ef4 t_ms=179
@RC stage=pipos ev=audio op=pcm_end n=78 last=795 t_ms=280
```

`t_ms` is the audio driver's millisecond clock. A rest in a sequence prints `op=stop`. On some hosts QEMU delivers far fewer timer interrupts than samples, so `n` can be much smaller than `len`.

## Time, sensors and power

| Number | Name | Arguments | Result | Errors |
|---|---|---|---|---|
| `0x30` | `read_sensor` | `uint32 sensor`, `uint32 *value` | 0 | `EINVAL` for `sensor` above 6, `EFAULT`, `ENODEV` for sensors 1 to 6 and for sensor 0 while the clock is not set |
| `0x31` | `sensor_available` | `uint32 sensor` | 1 for sensor 0 while the clock is set, else 0 | `EINVAL` for `sensor` above 6 |
| `0x40` | `get_time` | `uint32 *unix_seconds` | 0 | `EFAULT`, `ENODEV` while the clock is not set |
| `0x41` | `get_battery` | none | none | Always `ENODEV` |
| `0x47` | `power` | `uint32 op` | 0 for ops 2 and 3. Ops 0 and 1 do not return. | `EINVAL` for `op` above 3, `EPERM` for a holotape |
| `0x4A` | `set_time` | `uint32 unix_seconds` | 0 | `EIO` when the RTC chip write fails, `EPERM` for a holotape |

The clock and sensor rules are in the spec's [Time, sensors and power](https://github.com/RobCo-Industries/RobCo-Core/blob/00d9a2d1efa10b08ea1c14f087f7e3316647d6a1/docs/spec/PIPOS_ABI.md#time-sensors-and-power) section.

- At boot the clock takes the time from the RTC chip when `ev=clock` says `rtc=present` and a time. See [HARDWARE.md](HARDWARE.md) for the chip. `set_time` also writes the chip when one answered at boot, so the time survives a reboot. It prints `ev=rtc_set rc=<n>` after each chip write. A failed write returns `-PIP_EIO` and the clock keeps the new value. The chip holds years 2000 to 2099. A time outside that range sets the clock only and returns 0. Without a chip the time is lost at power-off.
- No sensor other than the clock and no battery monitor is supported.

| `op` | What Pip-OS does |
|---|---|
| 0 | Prints `ev=reboot`, then resets the board through the watchdog |
| 1 | Prints `ev=halt`, stops the sound, redraws the console and shows `PIP-OS HALTED. SAFE TO POWER OFF.`, then stops with interrupts masked |
| 2 | Turns the screen black. Drawing and `present` keep working on the back buffer. |
| 3 | Shows the back buffer as it is now, or the console when the image has not called `present` |

## Saves and files

| Number | Name | Arguments | Result | Errors |
|---|---|---|---|---|
| `0x50` | `read_save` | `const char *name, void *buf, uint32 size` | Bytes copied: the save's size, or `size` if smaller | `EINVAL`, `EFAULT`, `ENOENT`, `ERANGE`, `EIO` |
| `0x51` | `write_save` | `const char *name, const void *buf, uint32 size` | `size` | `EINVAL`, `EFAULT`, `ERANGE`, `ENOSPC`, `EIO` |
| `0x52` | `save_info` | `const char *name, uint32 *size` | 0, and the save's size in `*size` | `EINVAL`, `EFAULT`, `ENOENT`, `ERANGE`, `EIO` |
| `0x53` | `save_delete` | `const char *name` | 0 | `EINVAL`, `EFAULT`, `ENOENT`, `EIO` |
| `0x54` | `file_open` | `const char *path` | Handle 0 to 7 | `EINVAL`, `EFAULT`, `EPERM`, `ENOENT`, `ENOMEM`, `EIO` |
| `0x55` | `file_read` | `int32 handle, void *buf, uint32 size` | Bytes read, 0 at the end | `EBADF`, `EFAULT`, `EIO` |
| `0x56` | `file_close` | `int32 handle` | 0 | `EBADF` |
| `0x57` | `dir_list` | `const char *path, pip_dirent_t *out, uint32 max, uint32 start` | Entries written, 0 past the end | `EINVAL`, `EFAULT`, `EPERM`, `ENOENT`, `EIO` |

The name and path rules, the 64 KiB save limit and the read-only card rules are in the spec's [Save files](https://github.com/RobCo-Industries/RobCo-Core/blob/00d9a2d1efa10b08ea1c14f087f7e3316647d6a1/docs/spec/PIPOS_ABI.md#save-files) and [Files](https://github.com/RobCo-Industries/RobCo-Core/blob/00d9a2d1efa10b08ea1c14f087f7e3316647d6a1/docs/spec/PIPOS_ABI.md#files) sections. The work-file recovery is in RobCo-Core's [storage guide](https://github.com/RobCo-Industries/RobCo-Core/blob/00d9a2d1efa10b08ea1c14f087f7e3316647d6a1/docs/guide/api/storage.md#whole-files).

- Saves live in `/PIPBOY/SAVE`. `write_save` copies the data in first, then creates the folder when it is missing.
- `/PIPBOY/SAVE` and everything in it return `-PIP_EPERM` to the file calls. A PC tool can give `PIPBOY` or `SAVE` an 8.3 alias such as `SAVE~1`, so Pip-OS also looks the first two components up on the card by long and short name. That costs one scan of the root for a path with two or more components, and one more scan of `/PIPBOY` when the first component is that folder. A path with one component costs no scan.
- `file_read` takes any `size` and reads through a 4 KiB kernel buffer. It runs the kernel's main-loop work between chunks, so script commands keep working during long reads.
- `dir_list` shows the save folder under the name that returns `-PIP_EPERM`.
- Pip-OS keeps a 64 KiB static buffer for saves and file reads, 8 file handles of 640 bytes each, and a 1280-byte buffer for `dir_list`.
- On a missing card each call can take about 3 seconds before it returns `-PIP_EIO`: the SD driver's time-out.
- In QEMU a 64 KiB `write_save` takes 20 to 120 ms, depending on the board.

## Holotapes

| Number | Name | Arguments | Result | Errors |
|---|---|---|---|---|
| `0x60` | `holo_enumerate` | `pip_holo_info_t *out, uint32 max` | Entries written | `EINVAL` for `max` outside 1 to 32, `EFAULT`, `EIO` |
| `0x61` | `holo_launch` | `const char *name, const char *args` | The holotape's exit status, 0 to 255 | `EPERM`, `EINVAL`, `EFAULT`, `ENOENT`, `ENOEXEC`, `EIO` |
| `0x62` | `holo_last_status` | none | What the ROM's last `holo_launch` returned | `ENOENT` before the first launch |
| `0x63` | `holo_eject_event` | none | 0 | none |

The listing, the launch steps, the name rules and what a holotape may touch are in the spec's [Holotapes](https://github.com/RobCo-Industries/RobCo-Core/blob/00d9a2d1efa10b08ea1c14f087f7e3316647d6a1/docs/spec/PIPOS_ABI.md#holotapes) and [Sandbox](https://github.com/RobCo-Industries/RobCo-Core/blob/00d9a2d1efa10b08ea1c14f087f7e3316647d6a1/docs/spec/PIPOS_ABI.md#sandbox) sections. [ROM_DEVELOPMENT.md](ROM_DEVELOPMENT.md#writing-a-holotape) shows how to build one.

- `holo_enumerate` skips a file smaller than 288 bytes, the stub, header and META block it reads.
- `holo_launch` copies both strings in before it checks anything else. A `NULL` `args` returns `-PIP_EFAULT`.
- The holotape runs inside the ROM's call, loaded at `0x00800000`. `exit` and a return from `pip_main` end the holotape only. A fault or a `kill` script command ends it with `-PIP_EFAULT`, and the ROM goes on.
- When `umode_run()` refuses the holotape task, `holo_launch` returns `-PIP_ENOEXEC` and the kernel prints `ev=error code=umode`.
- A holotape fault prints `ev=holo_kill` and is not shown on the screen.
- The volume a holotape sets stays after it ends.

## Limits

- An image can run forever. The kernel sets no time limit.
- The kernel runs its main-loop work, such as script commands, only inside `sleep_ms`, the input calls, `file_read` and `holo_enumerate`. An image that calls none of them blocks the maintenance commands. See [BOOT.md](BOOT.md#running-the-rom).
- Nothing is validated on real hardware. Every test runs in QEMU.
