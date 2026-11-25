# Changelog
All notable changes to PIP-OS will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- The ILI9341 SPI panel. `[display] backend = hdmi | tft | both` in `PIPOS.CFG` and `tft_cs`, `tft_dc`, `tft_reset`, `tft_backlight`, `tft_hz`, `tft_rotation`, `tft_bgr`, `tft_invert`. Pins the input layer, audio or a bus uses are refused by name. `ev=display` reports the panel at boot. A panel that fails to start leaves HDMI in use.
- `present` on the panel sends only the smallest rectangle that changed since the last frame sent. At `tft_rotation = 0` or `180` it looks only at the left 240 columns and top 240 rows the panel shows. `power` drives the panel's display and sleep and the backlight. A panic wakes a blanked panel before it shows.
- With the panel in use, boot text and the panic text also show on it, as 15 lines of 40 characters.
- `gfx_info` reports `backend` 1 while the panel takes frames.
- A test build with `-DPIPOS_TFT_TEST`, the `tftdump` script command in it, and `tests/test_tft.py`, which compares the panel picture rebuilt by RobCo-Core's ILI9341 model with HDMI.
- `make kernel` builds only the kernel image and its ELF, for a ROM project that calls Pip-OS's Makefile from another repository.
- `CARD_DIR=<dir>` copies a folder tree onto the card for `make sdimage`, `run` and `bootdir`. A file at the path of a standard card file stops the build. `TAPE` takes several files.
- "Using Pip-OS from another repository" in `docs/ROM_DEVELOPMENT.md`, with a complete Makefile, and `tests/host/outside_project_test.sh`.
- The test helpers are an importable package, `sdk/testing/pipos_testing`, with a pytest plugin. A ROM project's `conftest.py` needs only the Pip-OS root. `tests/pipos_helpers.py` re-exports it.
- `booted(..., wait="jump")` waits for the jump into the ROM, for a ROM that never exits. `check_golden`, `script` and `press` helpers.
- `pipos/selftest.h` in `libpip.a`: `pip_check`, `pip_check_eq`, `pip_checks_done` and the marker functions the test ROMs used from `rt.c`.
- `pipos/ini.h` and `pipos/crc32.h` in `libpip.a`: RobCo-Core's INI parser as `pip_ini_*` and CRC-32/ISO-HDLC as `pip_crc32` and `pip_crc32_update`.
- ABI 1.2: Back is button 6. Images get its press, release and repeat events with `code` 6 and bit 6 of `read_buttons`, `PIP_BTN_BACK` and `PIP_BTN_BACK_BIT` in `abi.h`. The hello example ends on Back, and the conformance ROM asks for it.
- The `rtc` script command prints the time the RTC chip holds, and `ev=rtc_set` reports each chip write.
- `sample_queue` (0x26) plays a sample right after the one that plays, with no gap. One sample can wait; a second call returns `-PIP_EBUSY`. With no sample playing it acts as `sample`.
- `pip_sample_queue()` in `libpip.a` and `PIP_SYS_SAMPLE_QUEUE` in `abi.h`.
- "Playing long audio" in `docs/ROM_DEVELOPMENT.md`: a streaming loop for a ROM.
- "Hardware results" in `docs/TESTING.md`: the first board run, Pip-OS `2d10c2c` with Deitrix on a Pi Zero W 1.1, and a hardware checklist.

### Changed
- RobCo-Core moved to `b2774bf` for the ILI9341 driver. Its audio trace now prints one `op=stop` for every sound cut by another one, so the audio tests expect it. Its console scrolls and clears only its grid, and memory copies run a word at a time.
- A command-line `EXTRA_CFLAGS` now adds to the kernel flags instead of replacing `-Isdk/include`.
- The kernel finds the card's volume with RobCo-Core's `fs_mount_boot()`. It prefers the first FAT volume that holds `/PIPBOY/PIPOS.CFG` or `/PIPBOY/ROM1.ROM`, and falls back to the first that mounts. `ev=mount` gains `marker`. `ev=card rc=nomount` gains `why`. A card with an empty partition table now reports `rc=nomount why=nofat` instead of `rc=nopart`.
- `PIP_ABI_VERSION` is `0x00010002`. Images sealed for 1.0 and 1.1 still run.
- RobCo-Core moved to `f5bba36` for `fs_mount_boot()`, `board_model_name()`, `rimg_self_check()` and a console backend that leaves bytes in the UART while the input queue is full.
- `make bootdir` writes the same files as `make sdimage`, the example ROM and tape included, plus the firmware. It builds only the kernel and the files it copies, not `all`.
- With `ROM=<file>`, `make sdimage`, `run` and `bootdir` no longer build the hello example. The tape example builds only when `TAPE` is left at its default.
- `SDIMAGE_DIR` stops the build: use `CARD_DIR`.
- The tests build subprocess environments with RobCo-Core's `minimal_env()` instead of a copy of the host environment, so a failing test cannot print it. `tests/test_env_hygiene.py` guards the rule.
- `make check-pins` uses RobCo-Core's shared `tools/check_doc_links.sh` through the new `make check-doc-links`. A short hash in a RobCo-Core link is now an error. Pip-OS's own copy is gone.
- `set_time` writes the RTC chip when one answers, so the time survives a reboot. A failed chip write returns `-PIP_EIO`; the clock keeps the new value. A time outside 2000 to 2099 sets the clock only.
- ABI 1.1: `PIP_ABI_VERSION` is `0x00010001`. The SDK seals images with it, and `sys_info` and the startup block report it. Images sealed for 1.0 still run.
- `tone`, `sample`, `play_seq`, `audio_stop` and image start and end drop a waiting sample too. `audio_status` is 1 while one waits.
- `docs/API.md` is the full system-call reference. The `docs/pending/` fragments are gone.
- RobCo-Core moved to `d6f756d` for the `-` flag, precision and `*` in `k_printf` and the printf functions of `libpip.a`, and the updated `set_time` spec.
- RobCo-Core moved to `222f4e4` for queued PCM playback and the `sample_queue` spec (ABI 1.1).
- RobCo-Core moved to `06316cd` for the updated Pip-OS ABI spec.
- RobCo-Core moved to `891b3a2` for 8-bit PCM playback with `audio_play_pcm()`.
- RobCo-Core moved to `78c9d9e` for nested user tasks, dial totals, a drop-oldest input queue, and a test harness that keeps QEMU awake and dumps guest state when a wait times out.
- The kernel serves system calls from one table per group, in `kernel/sys_<group>.c`. `kernel/syscall.c` keeps the dispatcher. A number listed twice or by the wrong group stops the boot.
- The pure pointer checks for system calls are shared helpers in `kernel/sysdispatch.c`.
- `sleep_ms` returns `-PIP_ERANGE` at once for `ms` above `0x7FFFFFFF`. Before, a value near `UINT32_MAX` slept forever.
- The `pip_*` syscall wrappers are functions in `libpip.a` instead of `static inline` functions in `syscall.h`.
- The test ROMs build through `pipos-app.mk` into `build/roms/<name>/<name>.rom`.
- `ev=ready` now means the kernel reached its main loop. With a ROM, it comes after the ROM ends.
- RobCo-Core moved to `1e0fb65` for user-mode tasks and the full Pip-OS ABI spec.
- Rebuilt the kernel on the RobCo-Core library, added as a git submodule in `external/robco-core`.
- One 32-bit image for the Pi Zero, Pi 1, Pi 2 and Pi 3. The `BCM=` option and the AArch64 build are gone.
- New layout: a top-level `Makefile`, `kernel/` for the sources, `tests/` for the tests.
- The boot text now appears on the HDMI framebuffer and the serial console. The holotape and ROM lines report real checks.
- CI calls the RobCo-Core reusable workflow and tests all four QEMU boards.
- RobCo-Core moved to `061f224`. The host tests, the SD image, the target test setup, `make run` and the pin check now use the library's facilities instead of Pip-OS copies.
- The screen dump and the panic trigger moved from Ctrl-D and Ctrl-P on the console to the `dump` and `panic` commands on the script channel.

### Added
- The ILI9341 SPI panel. `[display] backend = hdmi | tft | both` in `PIPOS.CFG` and `tft_cs`, `tft_dc`, `tft_reset`, `tft_backlight`, `tft_hz`, `tft_rotation`, `tft_bgr`, `tft_invert`. Pins the input layer, audio or a bus uses are refused by name. `ev=display` reports the panel at boot. A panel that fails to start leaves HDMI in use.
- `present` on the panel sends only the smallest rectangle that changed since the last frame sent. `power` drives the panel's display and sleep and the backlight.
- With the panel in use, boot text and the panic text also show on it, as 15 lines of 40 characters.
- `gfx_info` reports `backend` 1 while the panel takes frames.
- A test build with `-DPIPOS_TFT_TEST`, the `tftdump` script command in it, and `tests/test_tft.py`, which compares the panel picture rebuilt by RobCo-Core's ILI9341 model with HDMI.
- `SUBTYPE` in `sdk/pipos-app.mk` seals a holotape as a game, 0, or a utility, 1. It refuses data tapes and any non-zero subtype for a ROM.
- Graphics calls `0x01` to `0x0F`: lines, points, text, rectangles, polylines, arcs, 1-bit and RGB blits, clip, colours, font and `present`. They draw on a 320x240 back buffer in kernel-only memory at `0x00C00000`, and `present` scales it by 2 onto the 640x480 framebuffer.
- The console redraws the boot screen when a ROM ends, so the ROM's last frame never stays on screen.
- Input calls `read_buttons`, `read_dial`, `poll_event` and `wait_event`. Events come from a 32-event queue that drops the oldest event when full. Input that happens before a ROM starts never reaches it.
- `wait_event` waits with a timeout or forever, and the kernel keeps answering script commands meanwhile.
- Audio calls `tone`, `sample`, `audio_stop`, `volume`, `audio_status` and `play_seq` (`0x20` to `0x25`). One voice plays in the background, and a new sound replaces the old one. Pip-OS copies notes and samples before it returns.
- `sample` plays up to 65536 bytes of 8-bit unsigned mono PCM at 8000 Hz. Not yet tried on real hardware.
- The volume starts at the `PIPOS.CFG` setting and stays until the next power-on. Pip-OS stops an image's sound when the image starts and when it ends.
- Time and sensor calls `read_sensor`, `sensor_available`, `get_time`, `get_battery` and `set_time`. The clock starts from the RTC chip when one answers. `set_time` sets the software clock only and never writes the chip.
- `power`: reboot through the watchdog, halt, screen off and screen on, with the `ev=reboot` and `ev=halt` markers.
- Saves: `read_save`, `write_save`, `save_info` and `save_delete` keep up to 64 KiB per save in `/PIPBOY/SAVE` with RobCo-Core's atomic write, and recover after a power cut.
- Files: `file_open`, `file_read`, `file_close` and `dir_list` read the card with strict 8.3 paths and 8 handles per image. No spelling of a path reaches the save folder, which returns `-PIP_EPERM`.
- Holotape calls `holo_enumerate`, `holo_launch`, `holo_last_status` and `holo_eject_event` (`0x60` to `0x63`). `holo_launch` runs a holotape from `/PIPBOY/HOLO` inside the ROM's call, in the holotape sandbox. A fault ends the holotape only.
- A holotape gets its own file handles, event queue, sound and drawing state. The ROM gets its own back when the holotape ends.
- The `kill` script command ends the running holotape, or the ROM when none runs. `state` reports `files=` and `holo=`.
- `make check-pins` also fails when a doc links to RobCo-Core at another commit than the submodule's.
- A conformance ROM in `conformance/` that runs every system call and shows a PASS/FAIL table on the screen and the serial port. `make conformance-card` builds its card. `docs/HARDWARE_VALIDATION.md` explains how to run it on a real board.
- A sample holotape in `sdk/examples/tape`. The default card carries it as `/PIPBOY/HOLO/BEACON.HTP`, and the hello example lists the holotapes, runs the one you pick and shows its exit status. `TAPE=<file>` and `TAPE=none` change the card's holotape.
- `abi.h` has every struct, constant and limit of the ABI, with build-time size and offset checks, and the `PIP_SYSCALLS` table.
- `libpip.a` has a `pip_<name>()` wrapper for every call, plus `pip_rect_xywh()` and `pip_set_clip_xywh()`.
- The `ev=syscalls` marker lists the calls the kernel serves.
- A target test checks that every call the kernel does not serve returns `-PIP_ENOSYS`.
- `tests/roms/rt.h` has named checks for test ROMs.
- `libpip.a`: syscall wrappers, `memcpy` and the other string functions, `pip_snprintf` and `pip_printf`, and a heap with `pip_malloc` and `pip_free`.
- `sdk/pipos-app.mk` builds, seals and verifies a ROM or a holotape. `sdk/holotape.ld` links at `0x00800000`.
- `sdk/examples/hello` and the ROM developer guide, [docs/ROM_DEVELOPMENT.md](docs/ROM_DEVELOPMENT.md).
- `make sdimage` and `make run` put the hello example on the card. `ROM=<file>` picks another image, `ROM=none` none.
- The kernel runs the ROM in user mode after the boot screen and reports `ev=jump`, `ev=rom_exit` or `ev=rom_kill`. A fault shows on screen.
- System calls `ticks_ms`, `sleep_ms`, `exit`, `sys_info`, `mem_info`, `log` and `rand`. Other numbers return `-PIP_ENOSYS`.
- `sdk/`: `include/pipos/abi.h`, `include/pipos/syscall.h`, `crt0.S` and `rom.ld`.
- Test ROMs in `tests/roms/` and `tests/test_rom.py`.
- `make run WINDOW=1` opens the QEMU display window.
- MMU and caches, interrupts, a 100 Hz tick and a kernel heap.
- Configuration from `/PIPBOY/PIPOS.CFG`. See [docs/CONFIG.md](docs/CONFIG.md).
- ROM check: the kernel loads and validates `/PIPBOY/ROM1.ROM`. It does not run it.
- Input from GPIO buttons, a dial, the serial console and the mini UART script channel.
- The `state` command on the script channel: ready flag, ROM state and free heap.
- A test for a ROM without the META flag.
- Power-on beep, the RTC and the software clock.
- A panic path, `pipos_panic()`.
- Host unit tests and QEMU target tests with the RobCo-Core harness.
- `make sdimage`, `make run` and `make bootdir`.

### Fixed
- After a holotape, the ROM keeps its open files and its input events.
- `mem_info` reports the caller's own heap.
- `read_buttons` stays right when RobCo-Core's input queue overflows, and an event's `time_ms` is the time it came in.
- `ev=rom_exit` prints the exit status as 0 to 255, the low 8 bits, like `holo_launch` returns it.

### Removed
- The old kernel sources in `src/` and `include/`, the old build in `build/` and the old test scripts.
- The stub system calls, power management and audio code.

## [7.1.0.8] - 2025-11-09

Note: this entry overstated the state of the code. Most listed items are stubs. See the Status section in [README.md](README.md#status).

### Added
- The ILI9341 SPI panel. `[display] backend = hdmi | tft | both` in `PIPOS.CFG` and `tft_cs`, `tft_dc`, `tft_reset`, `tft_backlight`, `tft_hz`, `tft_rotation`, `tft_bgr`, `tft_invert`. Pins the input layer, audio or a bus uses are refused by name. `ev=display` reports the panel at boot. A panel that fails to start leaves HDMI in use.
- `present` on the panel sends only the smallest rectangle that changed since the last frame sent. `power` drives the panel's display and sleep and the backlight.
- With the panel in use, boot text and the panic text also show on it, as 15 lines of 40 characters.
- `gfx_info` reports `backend` 1 while the panel takes frames.
- A test build with `-DPIPOS_TFT_TEST`, the `tftdump` script command in it, and `tests/test_tft.py`, which compares the panel picture rebuilt by RobCo-Core's ILI9341 model with HDMI.
- Complete PIP-OS V7.1.0.8 implementation from scratch
- Matrix-style boot display with cascading characters
- Boot audio sequence (power-on beep, data stream, relay clicks, ready chime)
- ROM loading system with header verification and CRC32 checksum
- Holotape support with detection and loading framework
- Comprehensive system call interface (13 syscalls)
  - Display operations (draw_line, draw_point, draw_text, clear_screen)
  - Input operations (read_buttons, read_dial)
  - Audio operations (play_tone, play_sample)
  - Sensor operations (read_sensor for RTC, battery, Geiger, GPS, accel, temp, heart rate)
  - System operations (get_time, get_battery)
  - Storage operations (read_save, write_save)
- Power management system with battery monitoring
  - Battery voltage reading and percentage calculation
  - Low battery detection
  - Power modes (Active, Idle, Sleep, Deep Sleep)
- Audio system framework for tone generation
- Memory layout following development plan specifications
- Support for three hardware tiers (BCM2835, BCM2836, BCM2837)

### Documentation
- Comprehensive README with PIP-OS overview and architecture
- API reference for ROM and holotape developers
- Hardware guide with pin assignments for three Pip-Boy tiers
- ROM development guide with examples and best practices
- CHANGELOG for version tracking

### Fixed
- Build system for modern gcc-arm-none-eabi toolchain
- Makefile paths for ARM toolchain (GCC 13.2.1)
- Added k_memcmp to kernel libc for ROM verification

### Changed
- RobCo-Core moved to `b2774bf` for the ILI9341 driver. Its audio trace now prints one `op=stop` for every sound cut by another one, so the audio tests expect it. Its console scrolls and clears only its grid, and memory copies run a word at a time.
- A command-line `EXTRA_CFLAGS` now adds to the kernel flags instead of replacing `-Isdk/include`.
- Updated version from V0.1.0.0 to V7.1.0.8 (matching Fallout canon)
- Boot messages now display authentic PIP-OS format
  - LOADER V1.1
  - EXEC VERSION 41.10
  - 64K RAM SYSTEM
  - 38911 BYTES FREE
- Main kernel now initializes all subsystems before entering main loop
- Replaced placeholder boot with proper subsystem initialization

### Technical Details
- Kernel size: ~40KB (within target specifications)
- Text section: 7KB
- BSS: 97KB (stack and buffers)
- Total ROM space for Deitrix: 64KB at 0x00010000

## [0.1.0.0] - Previous

### Initial Release
- Basic kernel implementation
- UART initialization
- Mailbox communication
- Simple framebuffer support
- Build system for multiple BCM variants

---

## Version Numbering

PIP-OS follows the canonical Fallout versioning:
- Major: 7 (PIP-OS 7.x series)
- Minor: 1 (Feature set)
- Patch: 0 (Bug fixes)
- Build: 8 (Build iteration)

Current stable: **V7.1.0.8**

---

*RobCo Industries - "Personal Information at Your Fingertips"*
