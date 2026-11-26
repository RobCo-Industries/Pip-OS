# Boot

This page describes what the kernel does from power-on to its main loop, how it runs the ROM, the serial markers it prints and the memory it uses.

## Boot flow

The firmware loads `PIPOS.IMG` at `0x8000` and enters it. RobCo-Core's start-up code sets up the stacks and vectors, detects the board and calls `robco_main()` in `kernel/main.c`. Pip-OS is a first stage: it never trusts a boot info block left in memory.

| Step | What runs | Marker |
|---|---|---|
| 1 | PL011 UART at 115200 baud | `ev=boot` |
| 2 | MMU and caches on, identity map | `ev=mmu` |
| 3 | Framebuffer at 640x480x32, screen console in green | none |
| 4 | Heap from the end of the image to `0x003FFFFF` | `ev=heap` |
| 5 | Interrupts and a 100 Hz tick | `ev=tick` |
| 6 | Input: GPIO buttons and dial, serial console, script channel on the mini UART | `ev=input` |
| 7 | Audio. QEMU has no PWM, so the trace backend is picked there. | `ev=audio_init` |
| 8 | I2C and the RTC. A chip that answers sets the software clock. | `ev=clock` |
| 9 | SD card and its boot volume, see [The card](#the-card) | `ev=card`, `ev=mount` |
| 10 | `/PIPBOY/PIPOS.CFG`, see [CONFIG.md](CONFIG.md) | `ev=config` |
| 11 | Colour scheme, volume, line delay, power-on beep | `ev=audio` |
| 11a | The SPI panel when `[display] backend` asks for it, see [The display backend](#the-display-backend) | `ev=display` |
| 11b | The holotape slot when `[holotape] slot = on`. With `off`, the default, no slot pin is touched and no marker prints. A panel setting that clashes with the slot keeps it off, see [CONFIG.md](CONFIG.md#slot-and-panel). A card in at boot is reported later, from the first slot work. | `ev=holo_slot` |
| 12 | Boot text, holotape check, ROM check | `ev=holo`, `ev=load` or `ev=error`, `ev=rom` |
| 13 | The ROM, if it passed the check, see [Running the ROM](#running-the-rom) | `ev=jump`, then `ev=rom_exit` or `ev=rom_kill` |
| 14 | Main loop | `ev=ready` |

A failed card, file or configuration step never stops the boot. A failure in steps 2 to 5 calls `pipos_panic()`.

## The display backend

With `backend = tft` or `both` in `PIPOS.CFG`, the kernel calls RobCo-Core's `tft_init()` with the panel keys after the settings. Then it reads the panel id with `tft_read_id()`. It prints one marker:

```
@RC stage=pipos ev=display backend=both tft=absent rc=0 hz=9722222 asked=tft bad=0
```

That line is from QEMU, where nothing answers and the core clock gives 9.72 MHz.

| Key | Meaning |
|---|---|
| `backend` | What is in use: `hdmi`, `tft` or `both` |
| `tft` | `ok`: the panel answered. `absent`: `tft_init()` worked but no id came back. `err`: `tft_init()` failed. |
| `rc` | What `tft_init()` returned, `0` or a negative `TFT_ERR_*` |
| `hz` | The SPI rate set, `0` when `tft_init()` failed |
| `asked` | The `backend` key of `PIPOS.CFG` |
| `bad` | How many `backend` and `tft_*` values the kernel refused |

`tft_init()` runs a second time after `TFT_ERR_BUS`. When it fails, the kernel uses HDMI only and shows `SPI DISPLAY FAILED, USING HDMI` on the boot console and the PL011. A panel that gives no id may still work: a module without its MISO line wired reads the same. So the panel stays in use. With `backend = tft` HDMI then also takes the frames, as with `both`, so a device with a dead panel stays usable on HDMI.

With the default `backend = hdmi` the kernel prints no `ev=display` marker and does not touch SPI0.

## The card

The kernel mounts the card with RobCo-Core's `fs_mount_boot()`. Every RobCo stage uses this rule, see [BOOT_PROTOCOL.md](https://github.com/RobCo-Industries/RobCo-Core/blob/815afabebf6baa728f69e2c2b016a188c5fd1119/docs/spec/BOOT_PROTOCOL.md#finding-loading-and-entering-the-next-stage). It reads the four primary MBR entries and tries them in this order:

1. Entries with a FAT type byte (`0x01`, `0x04`, `0x06`, `0x0B`, `0x0C`, `0x0E`), in slot order.
2. Then the other entries, in slot order. Some tools write a wrong type byte, and the mount reads the boot sector anyway.

Extended (`0x05`, `0x0F`) and GPT (`0xEE`) entries are skipped. A card with no partition table whose block 0 is a FAT boot sector mounts as a whole.

The kernel uses the first volume that holds `/PIPBOY/PIPOS.CFG` or `/PIPBOY/ROM1.ROM` as a file of at least one byte. A folder or an empty file of that name does not count. When no volume holds one, the kernel uses the first volume that mounts. So a card with one FAT volume boots whatever it holds.

When nothing mounts, `ev=card` reports `rc=nomount` with the last file system error in `fs` and the reason in `why`:

| `why` | Meaning |
|---|---|
| `mount` | FAT entries exist, none mounts |
| `nofat` | No FAT entry, and no other entry mounts. Also a card with no table and no FAT boot sector. |
| `gpt` | A GPT card. GPT is not supported. |
| `release` | A probed volume did not unmount |

When block 0 does not read, `ev=card` reports `rc=nopart` with the error in `part`. The screen shows `ROM(1) NOT LOADED: NO SD CARD` for every card failure.

## Boot text

The kernel prints these lines on the screen and on the UART. With `line_delay_ms` set, it waits that long before each line after the first, so the pause comes after the last character of the line before.

### Text that writes itself

With `[display] char_delay_ms` above 0, the kernel writes each boot line on the HDMI console one character at a time and waits that long after each one. A block cursor shows the writing place while the boot text writes, and goes away after the last boot line. The SPI panel gets each line whole, when it is done. The UART gets each line whole too, in every case, so the serial log keeps its bytes. With `char_delay_ms = 0`, the default, each line appears at once and no cursor shows.

Typing makes no sound of its own. No character waits for a sound. With the system sound on, each typed line adds disk clicks, see [The disk click](#the-disk-click).

### The system sound

With `[audio] system_sound = yes`, or its old name `key_click = yes`, the kernel plays the system sound at power-on instead of the square beep:

1. `audio_rest_mid()` from RobCo-Core moves the pin's rest level from duty 0 to the midpoint with a 40 ms ramp.
2. The kernel waits for the ramp to end.
3. If `beep` is on, it plays the soft beep, `sound_beep` of `kernel/sound_table.c`: a 1000 Hz sine, 60 ms, 2250 bytes at 37500 Hz.

If `audio_rest_mid()` fails, the kernel prints `ev=sound rc=<n>` with the library's error code and boots as with the system sound off: the square beep, if `beep` is on.

In QEMU the boot then prints, before the first boot line:

```
@RC stage=pipos ev=audio op=pcm rate=22050 len=882 vol=100 crc=2d541516 t_ms=30
@RC stage=pipos ev=audio op=pcm_end n=132 last=881 t_ms=216
@RC stage=pipos ev=audio op=pcm rate=37500 len=2250 vol=80 crc=3a2acd1e t_ms=219
@RC stage=pipos ev=audio op=pcm_end n=155 last=2194 t_ms=279
```

The ramp plays at full scale whatever the volume. Times and counts vary with the host. With `system_sound = no`, the default, the boot plays the square beep as before: the same pin levels, trace and UART bytes.

Nobody has heard the soft beep on a board yet.

### The disk click

With the system sound on, the machine's work makes clicks. Two kinds of work add to a debt of clicks:

| Work | Adds |
|---|---|
| A read or write call on the card or the holotape slot | `2 + blocks / 4` |
| A boot line written with typing on | `1 + printable characters / 6` |

The debt stops at 40 clicks. `kernel/dsound_rules.c` holds the rules. `kernel/dsound.c` wraps the card's and the slot's block devices, so drivers, the file system and the loader know nothing of sound. The wrapper counts each call with IRQs masked.

A tick callback, the fourth and last tick slot, asks the limiter every 10 ms. The limiter pays the debt in bursts of 3 to 21 clicks. Inside a burst a click starts every 30 to 80 ms. Between bursts it waits 150 to 600 ms more. Each click is the next of the 12 clicks of `sound_click`, 789 bytes at 37500 Hz, 21 ms. A due click starts only when the voice is idle. While the ramp, the beep or a click plays, it waits a tick and is not lost.

Reads before `PIPOS.CFG` is parsed are counted and wait for the setting. After each boot stage the kernel prints what the stage added:

```
@RC stage=pipos ev=disk at=mount calls=4 blocks=4 owed=8
@RC stage=pipos ev=disk at=config calls=3 blocks=3 owed=6
@RC stage=pipos ev=disk at=text calls=0 blocks=0 owed=9
@RC stage=pipos ev=disk at=holo calls=2 blocks=2 owed=4
@RC stage=pipos ev=disk at=rom calls=4 blocks=4 owed=8
```

`at` is `mount`, `config`, `holo`, `rom` or `text`, one `text` line after each boot line written with typing on. The `mount` and `config` lines print after the `ev=config` summary, once the setting is known. `owed` is what the stage added before the cap, so the clicks never outnumber the sum of `owed`. A boot without a ROM keeps paying its debt after `ev=ready`.

At the jump to a ROM the debt drops to 8 and the kernel plays no more clicks. A click that still plays at the jump ends first. So a click never meets a sound of the ROM.

A click starts from the tick at any moment. On the trace backend its two records, `op=pcm` and `op=pcm_end`, wait in RobCo-Core's queue of 64 until the main context polls. So until the jump the kernel polls wherever it waits or works long: after each typed character, every 100 ms of the line delay, after each card or slot call, and before each `ev=disk`, `ev=jump` and `ev=ready` marker. At the jump it waits for a click that still plays, then prints the queue. The fastest pace is one click per 30 ms, so 100 ms adds at most 8 records and the queue never fills. Order rule: every record of a click prints before the next `ev=disk`, `ev=jump` or `ev=ready` marker.

Two waits do not poll. A frame sent to the SPI panel after each boot line takes 128 ms at the default rate, but up to 1.23 s at the 1 MHz minimum: in theory about 37 clicks, 74 records, more than the queue holds. A panic halts with its queued records unprinted. On a board the PWM backend has no queue, and the poll returns at once.

With the system sound off, the default, the wrapper only forwards each call. No tick callback runs, and the boot prints no `ev=disk` and plays no click: the same UART bytes as before. Without a card there is no `PIPOS.CFG`, so the sound is off.

Nobody has heard the clicks on a board yet.

```
*************** PIP-OS(R) V7.1.0.8 ***************
COPYRIGHT 2075 ROBCO IND.
**************************************************

LOADER V1.1
EXEC VERSION 41.10
64K RAM SYSTEM
38911 BYTES FREE
NO HOLOTAPE FOUND
LOAD ROM(1): DEITRIX 3.0.3

**************************************************
PIP-OS KERNEL INITIALIZED
PIP-OS READY
**************************************************
```

The version numbers, `64K RAM SYSTEM` and `38911 BYTES FREE` are fixed presentation text. Two lines report real checks:

| Line | Rule |
|---|---|
| Holotape line | `NO HOLOTAPE FOUND` when `/PIPBOY/HOLO/` holds no file ending in `.HTP`. Otherwise `1 HOLOTAPE FOUND` or `<n> HOLOTAPES FOUND`. The count skips a name that starts with a dot, such as a Mac's `._CULVERT.HTP`, and a hidden or system file. |
| ROM line | `LOAD ROM(1): <NAME> <version>` with the name and version from the ROM's header, only after the ROM passed every check. Otherwise `ROM(1) NOT LOADED: <reason>`. |

| ROM result | Reason on screen |
|---|---|
| No card | `NO SD CARD` |
| `nofile` | `NO FILE /PIPBOY/ROM1.ROM` |
| `io` | `READ ERROR` |
| `magic` | `NOT A ROM IMAGE` |
| `version` | `UNKNOWN HEADER VERSION` |
| `size` | `FILE SIZE MISMATCH` |
| `bounds` | `DOES NOT FIT THE ROM SLOT` |
| `abi` | `WRONG ABI VERSION` |
| `crc` | `CHECKSUM MISMATCH` |

## ROM check

The kernel loads `/PIPBOY/ROM1.ROM` with RobCo-Core's stage loader into the ROM slot, `0x00400000` to `0x007FFFFF`. The loader accepts only the `ROM1` magic with ABI major 1, and runs the validation steps of [IMAGE_FORMAT.md](https://github.com/RobCo-Industries/RobCo-Core/blob/815afabebf6baa728f69e2c2b016a188c5fd1119/docs/spec/IMAGE_FORMAT.md#validation): magic, header version, file size, slot bounds, ABI and CRC.

## Running the ROM

After the boot text, the kernel runs a ROM that passed the check. It uses RobCo-Core's [`umode.h`](https://github.com/RobCo-Industries/RobCo-Core/blob/815afabebf6baa728f69e2c2b016a188c5fd1119/docs/guide/api/umode.md) with the MMU on.

1. It writes the startup block, `pipos_startup_t`, at `0x017F0000`, the bottom of the ROM stack. The umode return stub goes right after it, at `0x017F0080`. The heap is not zeroed.
2. It prints `ev=jump entry=0x00400000` and enters the ROM in USR mode with IRQs on.
3. The ROM runs until it calls `exit`, returns from its entry function or faults. System calls are listed in [API.md](API.md).
4. The kernel prints how the ROM ended, then enters the main loop and prints `ev=ready rom=1`.

| Register or field | Value at entry |
|---|---|
| pc | `0x00400000` |
| sp | `0x01800000` |
| r0 | `0x017F0000`, the startup block |
| r1 to r3 | 0 |
| lr | `0x017F0080`, the return stub. Returning acts like `exit(r0)`. |
| `abi_version`, `size` | `0x00010004`, 128 |
| `heap_base`, `heap_size` | `0x01000000`, `0x007F0000` |
| `stack_top`, `kind`, `flags`, `args` | `0x01800000`, 0, 0, empty |

The ROM can touch two regions, both read-write: its slot, `0x00400000` to `0x007FFFFF`, and its heap and stack, `0x01000000` to `0x017FFFFF`. Any other access faults.

| End | Marker | Screen |
|---|---|---|
| `exit(n)` or return `n` | `ev=rom_exit status=<s>`, where `<s>` is the low 8 bits of `n` | nothing |
| Fault | `ev=rom_kill reason=<undef\|prefetch_abort\|data_abort> pc=0x<pc>`, plus `dfar dfsr` or `ifar ifsr` for aborts | `ROM(1) <fault> AT 0x<pc>` |
| `umode_run()` refused the task | `ev=error code=umode rc=<n>` | nothing |

`pc` is the address of the faulting instruction. A privileged instruction in USR mode is an undefined instruction.

Limits:

- While the ROM runs, the kernel does its main-loop work, such as script commands and input, only inside `sleep_ms`, the input calls, `file_read` and `holo_enumerate`. A ROM that calls none of them blocks every maintenance command, `kill` included.
- A ROM that never ends never reaches the main loop, so `ev=ready` does not appear.
- On the ARM1176 boards in QEMU, `raspi0` and `raspi1ap`, a jump into kernel memory does not fault. The ROM runs the kernel code in USR mode until its first privileged instruction, which faults as `undef`. Data accesses to kernel memory do fault. QEMU 10.1 does not check execute permission on ARMv6 CPUs. Real hardware is not verified yet. The `jmpkern` test asserts this `undef` outcome on these boards.

## Main loop

The main loop waits for input events with `wfi` between ticks. It prints one `ev=key` marker per event and prints queued audio trace records. The same work runs inside the blocking calls listed in [Running the ROM](#running-the-rom). Every byte on the serial console is input, control characters included.

## Maintenance commands

The kernel answers its own commands on the mini UART script channel, next to the RobCo-Core commands such as `key` and `dial`. It registers them with `input_script_set_handler()`. Each output line reaches the host with `| ` in front, and the answer ends with a status line, `ok` or `err <reason>`.

| Command | Reply |
|---|---|
| `dump` | The screen as text: an `ev=screen` line, one `\|`-prefixed line per row, an `ev=screen_end` line, then `ok` |
| `state` | One line, `ready=<0\|1> rom=<0\|1> heap_free=<bytes> files=<n> holo=<0\|1> slot=<state>`, then `ok`. `ready=1` once the main loop runs. `files` counts the open file handles of the ROM and the holotape. `holo=1` while a holotape runs. `slot` is `off`, `empty`, `settling`, `mounted` or `bad`. |
| `rtc` | One line, `rc=0 time=<ISO 8601>` with the time the RTC chip holds, or `rc=<error> time=none`, then `ok`. It reads the chip, not the software clock. `rc=-11` means no chip answered at boot. |
| `kill` | Ends the innermost running image: the holotape if one runs, else the ROM. It prints `ev=holo_kill` or `ev=rom_kill` with `reason=killed code=1`. |
| `panic <reason>` | Takes the panic path with `<reason>` as the message. No status line follows. |

Test builds add `tftdump`, see [TESTING.md](TESTING.md#spi-panel), and `holo`, see [TESTING.md](TESTING.md#holotape-slot). Any other line that is not a RobCo-Core command gets `err unknown command`.

## Panic

`pipos_panic(why)` masks interrupts, prints `ev=panic msg=<why>` with spaces turned into `_`, writes `KERNEL PANIC: <why>` and `SYSTEM HALTED` on the screen and halts. A panel that `power` blanked is woken first, see [ARCHITECTURE.md](ARCHITECTURE.md#display-path). CPU exceptions print `ev=exception` through RobCo-Core and halt.

## Markers

Every marker is one line on the PL011 that starts with `@RC stage=pipos`. The format is in [BOOT_PROTOCOL.md](https://github.com/RobCo-Industries/RobCo-Core/blob/815afabebf6baa728f69e2c2b016a188c5fd1119/docs/spec/BOOT_PROTOCOL.md#serial-markers).

| Event | Keys | Meaning |
|---|---|---|
| `boot` | `midr soc base board_rev mem entry_mode` | First marker |
| `mmu` | `on` | MMU state |
| `heap` | `base size` | Kernel heap |
| `tick` | `hz` | Tick rate |
| `input` | `gpio serial mini script repeat` | Return codes of the input backends, `0` for success |
| `audio_init` | `rc backend probe` | Audio backend, `pwm` or `trace` |
| `clock` | `rtc time` | `rtc=present` or `absent`. `time` in ISO form, or `unset`. |
| `card` | `rc`, then `sd`, `part` or `fs why` | `ok`, `nocard`, `nopart` or `nomount`. See [The card](#the-card). |
| `mount` | `part type lba blocks marker` | The mounted volume. `part` is the MBR slot `0` to `3`, or `whole` for a card without a table. `marker` is `1` when it holds a Pip-OS file. |
| `config` | `rc key line` or `rc path line scheme line_delay_ms volume beep char_delay_ms key_click` | A configuration problem, or the summary. `key_click` holds the resolved `system_sound`. See [CONFIG.md](CONFIG.md). |
| `disk` | `at calls blocks owed`, or `rc=tickfull` | With the system sound on, what a boot stage added to the click debt. `tickfull`: no tick slot was free, so no click plays. See [The disk click](#the-disk-click). |
| `sound` | `rc` | The rest ramp of the system sound failed with this library error. The boot goes on as with the system sound off. See [The system sound](#the-system-sound). |
| `syscalls` | `served` | The calls the kernel serves: a 128-bit hex number, bit n set for call n |
| `holo` | `count` | `.HTP` files in `/PIPBOY/HOLO/`, without dot names and hidden or system files |
| `load` | `path size crc addr name` | The ROM passed every check |
| `error` | `code path` | The ROM failed. `code` is a `rimg` error name. |
| `rom` | `rc name version size crc` | ROM result. `rc` is `ok` or a `rimg` error name. `name` and `version` are `-` on failure. |
| `gfx` | `bb size user heap_end` | The back buffer. `user=0` when no image can reach it. |
| `display` | `backend tft rc hz asked bad` | The SPI panel at boot, `backend = tft` or `both` only. See [The display backend](#the-display-backend). |
| `display_error` | `op rc` | The first panel call that failed after boot. `op` is `present`, `blank` or `wake`. |
| `tft_send` | `why x y w h bytes crc` | Test builds only, one per send to the panel. See [TESTING.md](TESTING.md#spi-panel). |
| `audio` | `op hz vol t_ms`, or `op rate len vol crc t_ms` and `op n last t_ms` for a sample | One sound change, trace backend only. Printed by RobCo-Core. See [API.md](API.md#audio). |
| `audio_drop` | `n` | Trace records the audio driver dropped. Printed by RobCo-Core. |
| `jump` | `entry` | The kernel enters the ROM |
| `rom_exit` | `status` | The ROM called `exit` or returned. `status` is 0 to 255. |
| `rom_kill` | `reason pc` and `dfar dfsr` or `ifar ifsr`, or `reason=killed code pc` | The ROM faulted, or the `kill` command ended it |
| `holo_start` | `name` | The kernel enters a holotape |
| `holo_exit` | `status` | The holotape called `exit` or returned. `status` is 0 to 255. |
| `holo_kill` | As `rom_kill`, or `reason=eject pc` | The holotape faulted, the `kill` command ended it, or its slot card left. For `eject`, `pc` is the tape's program counter at the system call where Pip-OS ended it, as `0x` and 8 hex digits like a fault's. Not shown on the screen. |
| `holo_error` | `code name` | The holotape failed to load. `code` is a `rimg` error name. |
| `holo_slot` | `backend rc` | `[holotape] slot = on` only: the slot backend at boot. `backend` is `holo`, or `ram` in a test build. |
| `holo_slot` | `state bits` | One slot change: the new state and the `PIP_HOLO_EV_*` bits it set |
| `holo_mount` | `rc` | The slot card's volume did not mount. `rc` is an `fs_strerror()` name. |
| `holo_slot_error` | `rc` | A slot card read failed in `holo_enumerate`. `rc` is an `fs_strerror()` name. |
| `holo_attach_start` | `t_ms` | Test builds only: a stepped attach began, at `tick_ms()`. |
| `holo_attach` | `rc ms polls steps t0_ms t1_ms pcm0 pos0 pcm1 pos1` | Test builds only, one per attach, at its result. `polls` counts the work passes after the start's pass and `steps` the step calls. `t0_ms` and `t1_ms` are `tick_ms()` at its start and end, within one tick of the audio trace's `t_ms`. `pcm0` and `pcm1` are `audio_busy()` at its start and end. `pos0` and `pos1` are the samples the current PCM play has output then. See [TESTING.md](TESTING.md#holotape-slot). |
| `reboot` | none | `power(0)`, right before the watchdog reset |
| `halt` | none | `power(1)`, right before the halt |
| `error` | `code=umode rc` | `umode_run()` refused the ROM or holotape task |
| `ready` | `rom` | The kernel is in its main loop. `rom=1` when the ROM passed the check. |
| `key` | `type src code value` | One input event, also while an image runs. `type` is `key`, `dial` or `char`. `src` is `gpio`, `serial` or `script`. |
| `screen`, `screen_end` | `cols rows cx cy scheme` | Screen dump |
| `panic` | `msg` | Fatal kernel error |
| `exception` | `type pc lr spsr ...` | A CPU exception in the kernel. Printed by RobCo-Core. |

A boot without a card:

```
@RC stage=pipos ev=card rc=nocard sd=-13
@RC stage=pipos ev=config rc=nocard path=/PIPBOY/PIPOS.CFG line=0 scheme=green line_delay_ms=0 volume=100 beep=1 char_delay_ms=0 key_click=0
@RC stage=pipos ev=holo count=0
@RC stage=pipos ev=error code=io path=/PIPBOY/ROM1.ROM
@RC stage=pipos ev=rom rc=io name=- version=- size=0 crc=0x00000000
@RC stage=pipos ev=ready rom=0
```

## Memory map

The kernel follows the Pip-Boy chain map of RobCo-Core's [MEMORY_MAP.md](https://github.com/RobCo-Industries/RobCo-Core/blob/815afabebf6baa728f69e2c2b016a188c5fd1119/docs/spec/MEMORY_MAP.md#pip-boy-chain).

| Range | Owner | Use today |
|---|---|---|
| `0x00000000`-`0x00007FFF` | Firmware | ATAGS and firmware data |
| `0x00008000`-`0x003FFFFF` | Pip-OS | Kernel image, stacks, MMU table and heap. The link fails if the image and stacks pass `0x00400000`. |
| `0x00400000`-`0x007FFFFF` | ROM slot | `ROM1.ROM` after a successful check. The ROM can write it. |
| `0x00800000`-`0x00BFFFFF` | Holotape slot | The running holotape, loaded by `holo_launch`. It can write it. |
| `0x00C00000`-`0x00FFFFFF` | Pip-OS | Kernel-only. The 320x240 back buffer at `0x00C00000`, the panel's last frame at `0x00C80000`, the panel console frame at `0x00D00000`. Test builds keep the panel transcript from `0x00D80000` to the end. |
| `0x01000000`-`0x017FFFFF` | ROM | ROM heap and stack. The startup block and the return stub sit at `0x017F0000`. |
| `0x01800000`-`0x01FFFFFF` | Holotape | Holotape heap and stack. Its startup block and return stub sit at `0x01FF0000`. |

The framebuffer lives in GPU memory, mapped non-cacheable.

## Lore numbers

`64K RAM SYSTEM` and `38911 BYTES FREE` come from the games. They are presentation only, and no code uses them.
