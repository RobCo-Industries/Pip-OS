# System call reference

A running image calls the kernel with `svc #0`. The call number goes in r7, the arguments in r0 to r3, and the result comes back in r0. A negative result is `-PIP_E*`. The kernel keeps every register except r0. The contract is RobCo-Core's [PIPOS_ABI.md](https://github.com/RobCo-Industries/RobCo-Core/blob/ecc956ff88d02f4c3aaab4e43db3072a2526dc79/docs/spec/PIPOS_ABI.md). This page lists what Pip-OS does on top of it.

[`sdk/include/pipos/abi.h`](../sdk/include/pipos/abi.h) holds the call numbers, the error codes, the structs and every limit. [`sdk/include/pipos/syscall.h`](../sdk/include/pipos/syscall.h) declares a `pip_<name>()` wrapper for every call, plus a raw `pip_syscall()`. They live in `libpip.a`. See [ROM_DEVELOPMENT.md](ROM_DEVELOPMENT.md).

Pip-OS implements ABI 1.5, `PIP_ABI_VERSION` `0x00010005`. The SDK seals images with it. The loader checks only the major version, so an image sealed for 1.0 to 1.4 still runs. The kernel serves all 52 calls of the spec's table. A number outside the table returns `-PIP_ENOSYS` (-38). The boot prints the served numbers in the `ev=syscalls` marker.

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

The kernel checks every pointer against the caller's regions before it reads or writes. A bad pointer returns `-PIP_EFAULT` and the kernel writes nothing. The regions are in the spec's [Sandbox](https://github.com/RobCo-Industries/RobCo-Core/blob/ecc956ff88d02f4c3aaab4e43db3072a2526dc79/docs/spec/PIPOS_ABI.md#sandbox) section.

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

The rules are in the spec's [System information](https://github.com/RobCo-Industries/RobCo-Core/blob/ecc956ff88d02f4c3aaab4e43db3072a2526dc79/docs/spec/PIPOS_ABI.md#system-information) section.

- `ticks_ms` moves in steps of 10 ms, the 100 Hz tick.
- `sleep_ms` waits on the 1 MHz system timer, so it never ends early. The tick wakes it, so it can run up to 10 ms long. IRQs stay on, and the kernel runs its main-loop work while it waits.
- The exit status is the low 8 bits of `status`. `exit` prints it as `ev=rom_exit status=<0 to 255>` for the ROM. For a holotape, `holo_launch` returns it.
- `sys_info` writes `size` 32, `abi_version` `0x00010005`, `pipos_version` `(major << 16) | (minor << 8) | patch`, the firmware `board_rev`, `soc` as `0x2835`, `0x2836` or `0x2837`, `mem_size` in bytes, `hw_flags` and one reserved word set to 0.
- `hw_flags` sits at offset `0x18` and is new in ABI 1.3. Bit 0, `PIP_HW_RTC`, is set when a clock chip answered at boot. Bit 2, `PIP_HW_AUDIO`, is set when the audio driver started at boot, so the sound calls can play. When the PWM output fails at boot and the trace backend takes over, the bit is set too. Bit 3, `PIP_HW_TAPE`, is set when `PIPOS.CFG` sets `[holotape] slot = on` and the slot backend started at boot, since ABI 1.4. Bit 4, `PIP_HW_AUDIO_MID`, is set when `PIPOS.CFG` sets `[audio] system_sound = yes` and the rest ramp reached its end at boot, since ABI 1.5. It tells an image that the output rests at its midpoint, so a sample that starts and ends at 128 plays with no thump and a tone is a square centred on that level. It is clear with the sound off, after a failed ramp, and whenever `PIP_HW_AUDIO` is clear. It never changes while an image runs. See the spec's [Rest level](https://github.com/RobCo-Industries/RobCo-Core/blob/ecc956ff88d02f4c3aaab4e43db3072a2526dc79/docs/spec/PIPOS_ABI.md#rest-level) section. Bit 1 and bits 5 to 31 are 0. A Pip-OS before 1.3 writes 0 there, so a ROM checks `abi_version >= 0x00010003` before it reads the field.
- What a ROM can tell its user: a set time lasts past power-off only when `PIP_HW_RTC` is set, the time lies in 946684800 to 4102444799, and `set_time` returns 0. With the bit clear the time is lost at power-off. The full rule is in the spec's [System information](https://github.com/RobCo-Industries/RobCo-Core/blob/ecc956ff88d02f4c3aaab4e43db3072a2526dc79/docs/spec/PIPOS_ABI.md#system-information) section.
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
| `0x0E` | `present` | none | 0 | `ENODEV` with no framebuffer, or one smaller than 320x240. `EIO` when the panel refused the frame. |
| `0x0F` | `set_font` | `uint32 font_id` | 0 | `EINVAL` for any font but 0 |

The drawing rules, the pixel rules of lines, rectangles, arcs and text, and the check order are in the spec's [Display](https://github.com/RobCo-Industries/RobCo-Core/blob/ecc956ff88d02f4c3aaab4e43db3072a2526dc79/docs/spec/PIPOS_ABI.md#display) section.

- `gfx_info` reports `backend` 1, `PIP_GFX_BACKEND_SPI`, while the panel takes frames, else 0, HDMI. The ABI has no value for both, so `backend = both` reports 1. It reports `fg_default` as the normal text colour of the `PIPOS.CFG` scheme. Green is `0x33E066`.
- The back buffer is 320x240 words at `0x00C00000`, in kernel-only memory. The boot prints `ev=gfx bb=0x00c00000 size=307200 user=0 heap_end=...` and panics if an image could reach that memory or the kernel heap overlaps it.
- `present` scales the back buffer by 2 onto the 640x480 framebuffer and centres it on black. Each `present` takes about 1.4 ms in QEMU.
- When the ROM starts, the back buffer is cleared to black and the drawing state is reset. The boot screen stays until the ROM's first `present`.
- When the ROM ends, by exit, return or fault, the console redraws the whole boot screen and the kernel prints its exit or fault line below it.
- A holotape gets a fresh drawing state when it starts. The ROM's state comes back when the holotape ends.
- With the SPI panel, `present` sends the smallest rectangle that changed since the last frame sent. A full frame is 153611 bytes: about 128 ms at 9.62 MHz, the rate set for the default 10 MHz. A 40-character line of text, 320x16 pixels, is 10251 bytes, about 8.5 ms. An unchanged frame sends nothing. The call returns when the bytes are out, so it blocks the ROM that long. Sound keeps playing: it runs from a timer interrupt. A ROM that streams with `sample_queue` must queue chunks that last longer than one `present`. See [ARCHITECTURE.md](ARCHITECTURE.md#display-path).

## Input

| Number | Name | Arguments | Result | Errors |
|---|---|---|---|---|
| `0x10` | `read_buttons` | none | The `PIP_BTN_*_BIT` mask of the buttons down now | none |
| `0x11` | `read_dial` | none | The dial position in degrees, 0 to 359 | none |
| `0x12` | `poll_event` | `pip_event_t *out` | 1 with the oldest event in `*out`, 0 when the queue is empty | `EFAULT` |
| `0x13` | `wait_event` | `pip_event_t *out`, `uint32 timeout_ms` | 1 with the oldest event in `*out` | `ETIMEDOUT`, `ERANGE` for a timeout above `0x7FFFFFFF` other than `PIP_WAIT_FOREVER`, `EFAULT` |

The queue, the event layout, the time stamps and the repeat schedule are in the spec's [Input](https://github.com/RobCo-Industries/RobCo-Core/blob/ecc956ff88d02f4c3aaab4e43db3072a2526dc79/docs/spec/PIPOS_ABI.md#input) section.

| Input | Event |
|---|---|
| Up, down, left, right, select, dial push | `PIP_EV_BUTTON`, `code` 0 to 5 |
| Back | `PIP_EV_BUTTON`, `code` 6, `PIP_BTN_BACK`. New in ABI 1.2. |
| One dial turn | `PIP_EV_DIAL`, `code` 0, the signed detent count |
| One holotape slot change | `PIP_EV_TAPE`, `code` 0, `value` the `PIP_HOLO_EV_*` bits of that change. The ROM's queue only. New in ABI 1.4. |
| F1 to F4, characters | Nothing. Only the kernel sees them. |

- Events come from the GPIO buttons and dial, the serial console and the script channel alike.
- Every input call and the wait in `sleep_ms` take new input first. `wait_event` runs the kernel's main-loop work while it blocks, so script commands still answer.
- RobCo-Core keeps a 32-event queue in front of the image's queue. It fills only while the kernel does not poll. Pip-OS sets it to drop its oldest event too, so when more than 32 events arrive between two polls, the image gets the newest 32 in order. Characters and kernel-only keys count toward the 32.
- At image start the kernel reads every byte that waits in the UARTs and keeps those events. Bytes that reach a UART later belong to the image, even if the host sent them earlier. The mini UART holds 8 bytes and the console UART 16. QEMU keeps the rest until the kernel reads.
- Back comes from its GPIO pin, from Backspace or a lone Escape on the serial console, and from `key back` on the script channel. It sends press, release and repeat events like the other buttons, and sets bit 6, `PIP_BTN_BACK_BIT`, of `read_buttons` while held. The kernel uses Back for nothing else. An image built for ABI 1.2 that runs on a 1.0 or 1.1 Pip-OS never sees it.
- `read_buttons` is the OR of the buttons each source holds. A release clears only the button its own source pressed.
- `read_dial` starts at 0 at boot.
- The kernel prints an `ev=key` marker for every input event, also while an image runs.

## Audio

| Number | Name | Arguments | Result | Errors |
|---|---|---|---|---|
| `0x20` | `tone` | `uint32 freq_hz, duration_ms` | 0 | `EINVAL` for `freq_hz` outside 20 to 20000 or `duration_ms` outside 1 to 60000, `EIO` |
| `0x21` | `sample` | `const uint8 *data`, `uint32 length`, played at the rate of `sample_rate` | 0 | `EINVAL` for `length` outside 1 to 65536, `EFAULT`, `EIO` |
| `0x22` | `audio_stop` | none | 0, also when nothing plays | none |
| `0x23` | `volume` | `uint32 level` | The previous level | `EINVAL` for `level` above 100 |
| `0x24` | `audio_status` | none | 1 while a sound the image started plays or a queued sample waits, else 0 | none |
| `0x25` | `play_seq` | `const pip_note_t *notes`, `uint32 count` | 0 | `EINVAL` for `count` outside 1 to 256 or any bad note, `EFAULT`, `EIO` |
| `0x26` | `sample_queue` | `const uint8 *data`, `uint32 length` | 0 | `EINVAL` for `length` outside 1 to 65536, `EFAULT`, `EBUSY` when a sample already waits, `EIO` |
| `0x27` | `sample_rate` | `uint32 hz`, 8000 or 37500 | The previous rate | `EINVAL` for any other value |

`EIO` means the audio driver refused the sound. The rules are in the spec's [Audio](https://github.com/RobCo-Industries/RobCo-Core/blob/ecc956ff88d02f4c3aaab4e43db3072a2526dc79/docs/spec/PIPOS_ABI.md#audio) section.

- The level starts at the `volume` key of `PIPOS.CFG`, 100 without one. See [CONFIG.md](CONFIG.md). It is a percentage of the square wave's duty cycle for tones. For a sample it scales each byte around 128.
- With `[audio] system_sound` on, the pin rests at the midpoint, so a ROM's tones are squares centred on it. Their height follows the volume: 127 steps each side at 100, set by `ROBCO_AUDIO_TONE_PEAK` in `kernel/robco_config.h`. A volume change takes effect at the next note. With the setting off, tones are the pulse as before.
- A ROM that wants its own level sets it and restores the old one before it exits.
- `sample` stops the current sound before it copies the data. An argument error returns before that, so the current sound plays on.
- When several errors apply, a call returns the first in this order: a number out of its limits, a bad pointer, a bad note, a waiting sample for `sample_queue`, then the driver's `EIO`.
- `sample_queue` checks the length, then the pointer, then whether a sample waits. When a sample plays, it copies the data and queues it behind, with no gap. When none plays, it acts as `sample`. `sample_queue` is new in ABI 1.1. [ROM_DEVELOPMENT.md](ROM_DEVELOPMENT.md#playing-long-audio) has a streaming loop.
- `tone`, `sample`, `play_seq` and `audio_stop` drop the waiting sample with the one that plays. Image start and end stop both.
- With `[audio] system_sound` on, the kernel may play a short disk click between the ROM's sounds, owed by the ROM's card and slot calls. See [BOOT.md](BOOT.md#clicks-under-a-rom). A ROM's sound always wins: a call that starts or stops a sound first stops a click that plays, and `sample_queue` never queues behind one. `audio_status` and `volume` cut no click. `audio_status` reads a click as silence: it returns 0 while only a click plays. `volume` sets the level for what plays next, and a click that plays takes the new level from its next sample, so it may change level midway. Nothing a ROM can observe shows a click. Each click prints its own `op=pcm len=789` record on the trace backend. A ROM cannot switch the clicks off in this version. The owner turns them off with `system_sound = no` in `PIPOS.CFG`, the default.
- `sample_rate` sets the rate of `sample` and `sample_queue`: 8000 or 37500 Hz. It returns the rate before. Any other value returns `-PIP_EINVAL` and changes nothing. A valid value never fails, whatever plays, and also with `PIP_HW_AUDIO` clear. It starts, stops and drops nothing, and cuts no click, like `volume`. `sample_rate` is new in ABI 1.5. See [Sample rate](#sample-rate).
- On PWM the sound goes out on the audio pins in [HARDWARE.md](HARDWARE.md). Samples are not tried on real hardware yet.

### Sample rate

A play takes the rate in force when it starts and keeps it to its end. A call to `sample_rate` never changes a sound that plays.

| Call | Rate it plays at |
|---|---|
| `sample` | The rate in force at the call |
| `sample_queue` behind a play | The rate of the play it joins, whatever the rate in force |
| `sample_queue` with no sample playing | The rate in force at the call: it starts a new play |
| `tone`, `play_seq` | Not affected |

So a ROM may set the rate, call `sample`, and set the old rate back at once. A ROM that streams with `sample_queue` keeps its rate set until the stream ends: a late chunk starts a new play at the rate in force.

The rate belongs to the image. Every image starts at 8000 Hz, a ROM and a holotape alike. A holotape's rate ends with it: when it ends, the ROM gets back the rate it had at `holo_launch`. The kernel keeps both in the audio group's per-image hooks. The volume belongs to the device and does not reset.

The limits stay in bytes at both rates: 1 to 65536 bytes per call and one waiting sample.

| Rate | One second takes | 65536 bytes last |
|---|---|---|
| 8000 Hz | 8000 bytes | 8.192 s |
| 37500 Hz | 37500 bytes | About 1.748 s |

37500 Hz is one sample per period of the PWM carrier, `AUDIO_PCM_RATE_MAX` in RobCo-Core. The trace shows the rate of each play in `op=pcm rate=`.

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

`t_ms` is the audio driver's millisecond clock. When a note ends on its own, `op=stop` carries the asked end, not the tick that stopped it. The sound itself changes only on the 100 Hz tick, so a 4 ms note shows a 4 ms gap in the trace but sounds about 10 ms. A rest in a sequence prints `op=stop`. So does every sound cut by another one, once. On some hosts QEMU delivers far fewer timer interrupts than samples, so `n` can be much smaller than `len`.

## Time, sensors and power

| Number | Name | Arguments | Result | Errors |
|---|---|---|---|---|
| `0x30` | `read_sensor` | `uint32 sensor`, `uint32 *value` | 0 | `EINVAL` for `sensor` above 6, `EFAULT`, `ENODEV` for sensors 1 to 6 and for sensor 0 while the clock is not set |
| `0x31` | `sensor_available` | `uint32 sensor` | 1 for sensor 0 while the clock is set, else 0 | `EINVAL` for `sensor` above 6 |
| `0x40` | `get_time` | `uint32 *unix_seconds` | 0 | `EFAULT`, `ENODEV` while the clock is not set |
| `0x41` | `get_battery` | none | none | Always `ENODEV` |
| `0x47` | `power` | `uint32 op` | 0 for ops 2 and 3. Ops 0 and 1 do not return. | `EINVAL` for `op` above 3, `EPERM` for a holotape |
| `0x4A` | `set_time` | `uint32 unix_seconds` | 0 | `EIO` when the RTC chip write fails, `EPERM` for a holotape |

The clock and sensor rules are in the spec's [Time, sensors and power](https://github.com/RobCo-Industries/RobCo-Core/blob/ecc956ff88d02f4c3aaab4e43db3072a2526dc79/docs/spec/PIPOS_ABI.md#time-sensors-and-power) section.

- At boot the clock takes the time from the RTC chip when `ev=clock` says `rtc=present` and a time. See [HARDWARE.md](HARDWARE.md) for the chip. `set_time` also writes the chip when one answered at boot, so the time survives a reboot. It prints `ev=rtc_set rc=<n>` after each chip write. A failed write returns `-PIP_EIO` and the clock keeps the new value. The chip holds years 2000 to 2099. A time outside that range sets the clock only and returns 0. Without a chip the time is lost at power-off.
- No sensor other than the clock and no battery monitor is supported.

| `op` | What Pip-OS does |
|---|---|
| 0 | Prints `ev=reboot`, then resets the board through the watchdog |
| 1 | Prints `ev=halt`, stops the sound, redraws the console and shows `PIP-OS HALTED. SAFE TO POWER OFF.`, then stops with interrupts masked |
| 2 | Turns the screen black. Drawing and `present` keep working on the back buffer. The panel gets display off, then sleep in. The backlight line goes low. |
| 3 | Shows the back buffer as it is now, or the console when the image has not called `present`. The panel gets sleep out and display on first, then the whole frame. |

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

The name and path rules, the 64 KiB save limit and the read-only card rules are in the spec's [Save files](https://github.com/RobCo-Industries/RobCo-Core/blob/ecc956ff88d02f4c3aaab4e43db3072a2526dc79/docs/spec/PIPOS_ABI.md#save-files) and [Files](https://github.com/RobCo-Industries/RobCo-Core/blob/ecc956ff88d02f4c3aaab4e43db3072a2526dc79/docs/spec/PIPOS_ABI.md#files) sections. The work-file recovery is in RobCo-Core's [storage guide](https://github.com/RobCo-Industries/RobCo-Core/blob/ecc956ff88d02f4c3aaab4e43db3072a2526dc79/docs/guide/api/storage.md#whole-files).

- Saves live in `/PIPBOY/SAVE`. `write_save` copies the data in first, then creates the folder when it is missing.
- `/PIPBOY/SAVE` and everything in it return `-PIP_EPERM` to the file calls. A PC tool can give `PIPBOY` or `SAVE` an 8.3 alias such as `SAVE~1`, so Pip-OS also looks the first two components up on the card by long and short name. That costs one scan of the root for a path with two or more components, and one more scan of `/PIPBOY` when the first component is that folder. A path with one component costs no scan.
- `file_read` takes any `size` and reads through a 4 KiB kernel buffer. It runs the kernel's main-loop work between chunks, so script commands keep working during long reads.
- `dir_list` shows the save folder under the name that returns `-PIP_EPERM`.
- A listing skips an entry whose long name starts with a dot, and an entry with the hidden, system or volume label attribute. A card written from a Mac holds `._<name>` side files and folders such as `.Spotlight-V100`, `.fseventsd` and `.Trashes`; FAT gives `._CULVERT.HTP` the 8.3 alias `_CULVE~1.HTP`, so the rule reads the long name. `dir_list` skips them too, and `start` counts only the entries it shows. `file_open` still opens such a file by its path.
- Pip-OS keeps a 64 KiB static buffer for saves and file reads, 8 file handles of 640 bytes each, and a 1280-byte buffer for `dir_list`.
- On a missing card each call can take about 3 seconds before it returns `-PIP_EIO`: the SD driver's time-out.
- In QEMU a 64 KiB `write_save` takes 20 to 120 ms, depending on the board.

## Holotapes

| Number | Name | Arguments | Result | Errors |
|---|---|---|---|---|
| `0x60` | `holo_enumerate` | `pip_holo_info_t *out, uint32 max` | Entries written | `EINVAL` for `max` outside 1 to 32, `EFAULT`, `EIO` |
| `0x61` | `holo_launch` | `const char *name, const char *args` | The holotape's exit status, 0 to 255 | `EPERM`, `EINVAL`, `EFAULT`, `ENOENT`, `ENOEXEC`, `EIO`, `ENODEV` when the slot card of a running slot tape left |
| `0x62` | `holo_last_status` | none | What the ROM's last `holo_launch` returned | `ENOENT` before the first launch |
| `0x63` | `holo_eject_event` | none | The ROM's `PIP_HOLO_EV_*` bits since its last call, then cleared. 0 for a holotape and with `PIP_HW_TAPE` clear. | none |

The listing, the launch steps, the name rules and what a holotape may touch are in the spec's [Holotapes](https://github.com/RobCo-Industries/RobCo-Core/blob/ecc956ff88d02f4c3aaab4e43db3072a2526dc79/docs/spec/PIPOS_ABI.md#holotapes) and [Sandbox](https://github.com/RobCo-Industries/RobCo-Core/blob/ecc956ff88d02f4c3aaab4e43db3072a2526dc79/docs/spec/PIPOS_ABI.md#sandbox) sections. [ROM_DEVELOPMENT.md](ROM_DEVELOPMENT.md#writing-a-holotape) shows how to build one.

- `holo_enumerate` skips a file smaller than 288 bytes, the stub, header and META block it reads.
- `holo_enumerate` and `holo_launch` skip the entries `dir_list` skips, see [Saves and files](#saves-and-files). A hidden tape is not listed, and `holo_launch` returns `-PIP_ENOENT` for it.
- `holo_launch` copies both strings in before it checks anything else. A `NULL` `args` returns `-PIP_EFAULT`.
- The holotape runs inside the ROM's call, loaded at `0x00800000`. `exit` and a return from `pip_main` end the holotape only. A fault or a `kill` script command ends it with `-PIP_EFAULT`, and the ROM goes on.
- When `umode_run()` refuses the holotape task, `holo_launch` returns `-PIP_ENOEXEC` and the kernel prints `ev=error code=umode`.
- A holotape fault prints `ev=holo_kill` and is not shown on the screen.
- The volume a holotape sets stays after it ends.

### The holotape slot

ABI 1.4 adds a second place for tapes: the root folder of the card in the holotape slot. `[holotape] slot = on` in `PIPOS.CFG` turns it on, see [CONFIG.md](CONFIG.md#keys). With the default `off`, no slot pin is touched and every call behaves as ABI 1.3.

- `holo_enumerate` lists the slot tapes first, with `PIP_HOLO_SLOT` in `flags`, then the boot card tapes whose name the slot card does not hold. A slot file shadows by name alone, also when its header fails. The 32 entries count both places.
- `holo_launch` looks on the slot card first while its volume is mounted. A name the slot card does not hold goes to the boot card. A read error on the slot card returns `-PIP_EIO` and makes the card bad. A pull returns `-PIP_ENOENT` or `-PIP_EIO`, never a boot card tape.
- A slot tape whose card leaves ends at its next system call of any kind. The kernel prints `ev=holo_kill reason=eject pc=<pc>` and `holo_launch` returns `-PIP_ENODEV`. A tape from the boot card runs on.
- `holo_eject_event` returns `PIP_HOLO_EV_INSERTED`, `PIP_HOLO_EV_REMOVED` and `PIP_HOLO_EV_BAD`. Each change also queues `PIP_EV_TAPE` for the ROM, never for a holotape.
- Slot work runs inside `sleep_ms`, `poll_event`, `wait_event`, `read_buttons`, `read_dial`, `file_read`, `holo_enumerate` and `holo_eject_event`.
- A card attaches in small steps, one per pass of that work, so these calls return at their usual pace while it attaches. Until `INSERTED`, the slot is settling: no slot tape is listed or launched and no bit is set. A slow card only takes longer to report. See [ARCHITECTURE.md](ARCHITECTURE.md#the-stepped-attach).
- `file_open`, `dir_list` and the save calls never reach the slot card.
- A folder named like a tape, such as `SLOT.HTP`, in the root of the slot card counts as missing, as on the boot card. `holo_enumerate` skips it and it shadows nothing, and `holo_launch` looks on the boot card next.

## Limits

- An image can run forever. The kernel sets no time limit.
- The kernel runs its main-loop work, such as script commands, only inside `sleep_ms`, the input calls, `file_read` and `holo_enumerate`. An image that calls none of them blocks the maintenance commands. See [BOOT.md](BOOT.md#running-the-rom).
- The automated tests run in QEMU. On a Pi Zero W 1.1, Pip-OS `2d10c2c` ran a ROM and one holotape with serial keys as input, see [Hardware results](TESTING.md#hardware-results).
