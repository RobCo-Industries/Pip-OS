# Changelog
All notable changes to PIP-OS will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [7.1.0.8] - 2025-11-09

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
