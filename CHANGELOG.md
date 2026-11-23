# Changelog
All notable changes to PIP-OS will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed
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
