# Changelog
All notable changes to PIP-OS will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- `make sdk-package` writes `build/pipos-1.4.0.zip`, the SDK package of RobCo-Core's `docs/spec/SDK_PACKAGE.md`, with `sdk.json`: the headers, `libpip.a`, `crt0.S`, the linker scripts, `flags.json`, the kernel and its ELF, the runner ROM as `boot/RUNNER.ROM`, the firmware stub, `boot.json` and the firmware pins. No firmware file. The same commit and compiler give the same archive bytes. It stops unless the compiler prints the version `SDK_GCC_RELEASE` names. `tools/sdk_package.py check` checks the archive rules and searches every file for the build folder, the home folder, the user name as `/home/NAME`, `/Users/NAME`, `\Users\NAME` or `NAME@`, and the host name. It checks the JSON type of every `sdk.json` field it reads, so `true` or `1.0` is not an integer and a number is not a string. It refuses a forbidden text under 4 characters, and reports an entry it cannot read as a failure. The headers come from `git ls-files`, and an untracked file in an input folder stops the build. `make sdk-boot-test SDK_ZIP=<zip>` boots the package's own kernel, firmware stub, runner ROM and `boot.json` on the simulator card, and the workflow runs it on the four boards. `PATH_MAPS` in `pipos-app.mk` passes `-ffile-prefix-map` to every compile. The `sdk-package` workflow builds it with xPack 15.2.1-1.1 and attaches it to the release of a `pipos-*` tag. See `docs/SDK_PACKAGE.md`.
- `ABI := MAJOR.MINOR` in `pipos-app.mk`, the lowest ABI an image needs. It passes `-DPIPOS_ABI_TARGET` to `crt0.S` and every source, `crt0.S` writes it into `abi_version`, and the headers hide calls added after it: below 1.1, `sample_queue`. A target above 1.4 or a bad value fails. Without `ABI` the image bytes do not change. `tests/host/sdk_abi_target_test.sh` checks it.
- `make flags-json` in `pipos-app.mk` writes `flags.json`, the build flags of a project, from the variables the build rules use. `tests/host/flags_json_test.sh` builds the example tape and ROM from it and from `make` and compares their SHA-256.
- `libpip.a` is archived with `ar rcsD`, so two builds give the same archive.
- The runner ROM, `sdk/examples/runner`, built as `build/examples/runner/RUNNER.ROM`. `robco run` and `robco test` boot it as `/PIPBOY/ROM1.ROM`. It reads the tape's name from `/PIPBOY/RUNNER.CFG`, launches the tape and prints `@RC stage=runner` lines signed with a key made by `rand` and kept on its stack, so a tape cannot print a line that passes for the runner's. Its build stops when `main.c` has a static object. `tests/test_runner.py` boots it on the simulator card's layout.
- ABI 1.4, removable holotapes. `[holotape] slot = on | off` in `PIPOS.CFG`, `off` by default, which touches no slot pin and keeps ABI 1.3 behaviour. `holo_enumerate` lists the slot card's tapes first with `PIP_HOLO_SLOT` in the new `flags` field, and a slot file shadows a boot card tape of the same name. `holo_launch` looks on the slot card first and returns `-PIP_ENODEV` when the card of the running tape leaves, with `ev=holo_kill reason=eject`. `holo_eject_event` returns `PIP_HOLO_EV_*` bits to the ROM, and each change queues `PIP_EV_TAPE`. `sys_info` reports `0x00010004` and `PIP_HW_TAPE`. The slot module is `kernel/slot.c` on a pure machine in `kernel/slot_rules.c`. A test build with `-DPIPOS_HOLO_TEST` serves a card from RAM, driven by the `holo` script command, for `tests/test_slot.py`. The SDK seals images for 1.4. Images sealed for 1.0 to 1.3 still run.
- RobCo-Core moved to `ec16779`. It brings the ABI 1.4 spec, the holotape slot driver `holo.h`, removable volumes in `fs.h`, GPIO input injection for QEMU tests, a shared host timing helper and dial decoder fixes. `ROBCO_FS_VOLUMES` is 2 by default: the second slot costs 592 bytes of BSS.
- ABI 1.3: `sys_info` reports `hw_flags` at offset `0x18`. `PIP_HW_RTC`, bit 0, is set when a clock chip answered at boot. `PIP_HW_AUDIO`, bit 2, is set when the audio driver started, also on the trace backend after a PWM error. The conformance ROM checks the flags, and a new conformance run uses QEMU's clock chip.
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
- A second Zero W 1.1 run in "Hardware results": the save read back at the next boot and a holotape that ran and returned to the ROM.
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

### Changed
- RobCo-Core moved to `2e79ee0`. Its test harness stops QEMU processes a dead run left, writes `boot.json` with `make boot-json`, and removes a passing test's `tmp_path` when the test ends, `ROBCO_KEEP_TMP=1` keeps them. `mk/robco.mk` rebuilds once when the spelling of `BUILD` changes. The SDK example tapes are byte for byte the same.
- The holotape slot attaches a card in steps, with RobCo-Core's `holo_attach_start()` and `holo_attach_step()`, one step per work pass. A slow card no longer stops the main loop: script commands, input, streamed sound and the ROM's own drawing go on while it attaches. One step holds the loop for about 1.3 ms at most. The ROM sees the same ABI 1.4 states and events, only later in time. A step that ends in `SD_ERR_SLOW` makes the card bad. A card whose CSD or CID data token comes later than the library's step budget ends as a bad card. The budget is `SDSPI_STEP_TOKEN_BYTES` in `sdspi.h`: 29 bytes, token included, about 0.58 ms at 400 kHz, for each of `SDSPI_TOKEN_TRIES`, 4, sends of CMD9 or CMD10. Then the step returns `SD_ERR_SLOW`. The blocking attach of the first ABI 1.4 kernel waited up to 100 ms for that token. No real card has been tried, so nobody knows yet whether such cards exist. The cure belongs in RobCo-Core: a stepped wait for the token by time, up to 100 ms. Tested with the RAM card backend in QEMU only, never on a real card.
- RobCo-Core moved to `cab5f69`. It brings the stepped attach, `SD_PENDING` and `SD_ERR_SLOW` in `sd.h`, and new spec pages that ask nothing of Pip-OS yet. The CI image tag stays `ba6372158e4b`.
- The hardware checklist in `docs/TESTING.md` has an open item for ABI 1.3 `hw_flags` on a board, and says that every recorded run used `2d10c2c`.
- The holotape count at boot, `holo_enumerate`, `holo_launch` and `dir_list` skip entries whose long name starts with a dot, and hidden, system and volume label entries. A card written from a Mac no longer counts its `._<name>.HTP` side files as holotapes, and `dir_list` no longer shows them or `.Spotlight-V100` as `_CULVE~1.HTP` or `SPOTLI~1`.
- The docs describe RobCo-Core's progress-based SPI rule: a send fails when no byte moves for 20 ms plus four byte times, not after a total time. The kernel's one whole-frame retry of a failed send is unchanged.
- RobCo-Core moved to `79cbd82`. Its harness parks the three spare cores of QEMU's `raspi3b` for every boot. Its SPI waits fail only when no byte moves for 20 ms plus four byte times. It adds a watchdog API, which the kernel does not use yet. Its host warning check now also compiles at CI's flags, so `tests/host/libpip_names.h` drops a host libc's macros of the same names before it renames them.
- RobCo-Core's harness parks cores 1 to 3 of `raspi3b` for every boot, so the panel tests no longer park them with `park_args()`. Both together would point two loaders at the same core. The cores spun in an exception loop and made a full present take 8.8 s on a GitHub runner.
- Tests with a time limit judge their claim only when the run's own numbers show the host kept up. Otherwise they run again, up to 4 runs, then skip with a `host timing:` reason. The session fails on more than one such skip per board. `--timing-skips-max` sets the limit, and `make test-target` prints skip reasons.
- A command-line `EXTRA_CFLAGS` now adds to the kernel flags instead of replacing `-Isdk/include`.
- The kernel finds the card's volume with RobCo-Core's `fs_mount_boot()`. It prefers the first FAT volume that holds `/PIPBOY/PIPOS.CFG` or `/PIPBOY/ROM1.ROM`, and falls back to the first that mounts. `ev=mount` gains `marker`. `ev=card rc=nomount` gains `why`. A card with an empty partition table now reports `rc=nomount why=nofat` instead of `rc=nopart`.
- `PIP_ABI_VERSION` is `0x00010003`. Images sealed for 1.0, 1.1 and 1.2 still run.
- `pip_sys_info_t.reserved` is one word, not an array of two. Code that read `reserved[0]` reads `hw_flags` now.
- RobCo-Core moved to `796607f` for ABI 1.3 in the spec, the ILI9341 panel driver, screen adopt calls and `ini_parse_sections()`. Its audio trace now prints one `op=stop` line when a start replaces a playing sound, so the audio tests expect it. Its console scrolls and clears only its grid, and memory copies run a word at a time.
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
