
# PIP-OS V7.1.0.8 ![arch](https://img.shields.io/badge/Arch-armv6%20%7C%20armv7--A%20%7C%20armv8--A-blue) ![ver](https://img.shields.io/badge/Version-7.1.0.8-green)

**Personal Information Processor Operating System**  
*RobCo Industries - "Personal Information at Your Fingertips"*

This is the foundational operating system for all Pip-Boy devices, responsible for hardware initialization, ROM loading, and providing core services to the Deitrix ROM or holotape applications.

## Features

### Core System
- **Matrix-style boot display** - Authentic boot sequence with cascading characters
- **ROM loading system** - Automatic detection and chainloading of Deitrix ROM
- **Holotape support** - Hot-swappable game/application cartridges
- **System call interface** - Clean API for ROM/holotape programs
- **Power management** - Battery monitoring and power-saving modes

### Version Information
- **Version**: PIP-OS V7.1.0.8
- **Copyright**: 2075 RobCo Ind.
- **Loader**: V1.1
- **EXEC**: 41.10
- **Memory**: 64K RAM system, 38911 bytes free

### Boot Sequence
```
Power On → Matrix Display → Hardware Check → Memory Init → 
ROM/Holotape Detection → Load Deitrix/Holotape → Transfer Control
```

## Architecture

### Memory Layout
```
0x00000000 - 0x00000FFF : Exception Vectors (4KB)
0x00001000 - 0x00007FFF : PIP-OS Kernel (28KB)
0x00008000 - 0x0000FFFF : System Services (32KB)
0x00010000 - 0x0001FFFF : ROM Space (Deitrix) (64KB)
0x00020000 - 0x0002FFFF : Application Space (64KB)
0x00030000 - 0x0003FFFF : Graphics Buffer (64KB)
0x00040000 - [RAM_END]  : User Space
```

### Subsystems
- **Boot Display** - Matrix-style boot animation and messages
- **ROM Loader** - ROM detection, verification, and chainloading
- **System Calls** - Display, input, audio, sensor, and storage APIs
- **Power Management** - Battery monitoring and power modes
- **Audio System** - Tone generation and boot sound effects

## ROM Integration

### Deitrix ROM
The official ROM developed by RobCo Industries is **Deitrix**, the primary operating environment for Pip-Boy devices. PIP-OS serves as the bootloader and service layer for Deitrix.

Repository: [https://github.com/RobCo-Industries/Deitrix](https://github.com/RobCo-Industries/Deitrix)

### ROM Format
```c
ROM Header Structure:
- Magic: "ROM1" (4 bytes)
- Name: "DEITRIX" (16 bytes)
- Version: "303" (8 bytes)
- Load Address: 0x00010000
- Entry Point: 0x00010100
- Size: Up to 64KB
- Checksum: CRC32
```

## Holotape System

### Holotape Format
```c
Holotape Header Structure:
- Magic: "ROBCO78" (8 bytes)
- Title: Game/Program name (64 bytes)
- Type: 0=Game, 1=Utility, 2=Data
- Version: Program version
- Load Address: Memory location
- Entry Point: Start address
- Size: Program size
- Icon: 32x32 monochrome image
- Payload: Actual program data
```

## System Call API

PIP-OS provides a comprehensive API for ROM and holotape applications:

### Display Operations
- `draw_line()` - Bresenham line drawing
- `draw_point()` - Point plotting
- `draw_text()` - Text rendering
- `clear_screen()` - Screen clearing

### Input Operations
- `read_buttons()` - Button state reading
- `read_dial()` - Rotary encoder position

### Audio Operations
- `play_tone()` - Tone generation
- `play_sample()` - Sample playback

### Sensor Operations
- `read_sensor()` - Sensor value reading
- `sensor_available()` - Sensor detection

### System Operations
- `get_time()` - RTC time reading
- `get_battery()` - Battery level
- `sleep()` - Power-saving mode

### Storage Operations
- `read_save()` - Save data reading
- `write_save()` - Save data writing

## Supported Hardware

The kernel is compatible with the following Broadcom SoCs:

### Tier 1: BCM2835 (Raspberry Pi Zero, Pi 1)
- ARMv6 architecture
- Single-core ARM1176JZF-S
- Basic Pip-Boy configuration

### Tier 2: BCM2836 (Raspberry Pi 2)
- ARMv7-A architecture
- Quad-core Cortex-A7
- Standard Pip-Boy configuration

### Tier 3: BCM2837 (Raspberry Pi 3)
- ARMv8-A architecture (32-bit mode)
- Quad-core Cortex-A53
- Deluxe Pip-Boy configuration

### Future Support
- BCM2711 (Raspberry Pi 4) - Planned

## Building from Sources

### Requirements 
- ARM bare-metal toolchain: `gcc-arm-none-eabi`
- GNU Make
- Optional: QEMU for testing (`qemu-system-arm`)

### Build Instructions

```bash
cd build

# Build for Raspberry Pi 2 (BCM2836)
export BCM=2836
make clean
make

# Build for Raspberry Pi 3 (BCM2837, requires aarch64 toolchain)
export BCM=2837
make clean
make

# Run in QEMU (for testing)
make run
```

### Build Configurations

The Makefile supports different build targets:
- `BCM=2835` - Raspberry Pi Zero/1 (ARMv6)
- `BCM=2836` - Raspberry Pi 2 (ARMv7-A)
- `BCM=2837` - Raspberry Pi 3 (ARMv8-A in 32-bit mode)

### Output Files
- `kernel7.img` - Bootable kernel image (32-bit ARM)
- `kernel8.img` - Bootable kernel image (64-bit ARM, if applicable)
- `kernel7.elf` - ELF binary with debug symbols

## Boot Messages

Authentic PIP-OS boot sequence:
```
*************** PIP-OS(R) V7.1.0.8 ***************
COPYRIGHT 2075 ROBCO IND.
**************************************************

Initializing subsystems...
  [OK] Power management
  [OK] Audio system
  [OK] System call interface

Starting boot sequence...

LOADER V1.1
EXEC VERSION 41.10
64K RAM SYSTEM
38911 BYTES FREE
NO HOLOTAPE FOUND
LOAD ROM(1): DEITRIX 303

**************************************************
PIP-OS READY
Battery: 75%
**************************************************
```

## Related Projects

### RETROS-BIOS
BIOS for terminals, main bootloader  
Repository: [https://github.com/RobCo-Industries/RETROS-BIOS](https://github.com/RobCo-Industries/RETROS-BIOS)

### MFBootAgent
Second stage bootloader for RETROS-BIOS  
Repository: [https://github.com/RobCo-Industries/MFBootAgent](https://github.com/RobCo-Industries/MFBootAgent)

### UOS
Terminal OS, loaded after MFBootAgent  
Repository: [https://github.com/RobCo-Industries/UOS](https://github.com/RobCo-Industries/UOS)

### Deitrix
Official ROM and user environment for Pip-Boy devices  
Repository: [https://github.com/RobCo-Industries/Deitrix](https://github.com/RobCo-Industries/Deitrix)

## Performance

### Boot Performance
- **Cold Boot**: < 5 seconds to Deitrix
- **Matrix Display**: 3-5 seconds duration
- **ROM Loading**: < 1 second
- **Holotape Detection**: < 500ms

### Runtime Performance
- **Display Update**: 30 FPS minimum
- **Input Latency**: < 50ms
- **Audio Latency**: < 20ms
- **Sensor Reading**: 10Hz minimum

### Memory Usage
- **Kernel**: < 28KB
- **Services**: < 32KB
- **Free for ROM**: > 38KB

## Development

### System Architecture
PIP-OS follows a layered architecture:

1. **Boot Loader Layer** - Hardware initialization and boot display
2. **Kernel Layer** - Memory management, scheduling, device management
3. **Service Layer** - Display, input, audio, sensor services
4. **User Space** - ROM/holotape execution environment

### Adding Custom ROMs
ROMs must implement the ROM header format and use the PIP-OS system call interface. See the Deitrix repository for examples.

### Creating Holotapes
Holotapes are self-contained applications that run in a sandboxed environment. They must include a valid holotape header and use only the approved system call API.

## References

* Peripheral manual for BCM2835 ARM chip  
  [BCM2835 ARM Peripherals](https://www.raspberrypi.org/app/uploads/2012/02/BCM2835-ARM-Peripherals.pdf)

* Circle - A C++ bare metal environment for Raspberry Pi  
  [https://github.com/rsta2/circle](https://github.com/rsta2/circle)

* Raspberry Pi 3 tutorial  
  [https://github.com/bztsrc/raspi3-tutorial](https://github.com/bztsrc/raspi3-tutorial)

* OSDev's Raspberry Pi Bare Bones  
  [https://wiki.osdev.org/Raspberry_Pi_Bare_Bones](https://wiki.osdev.org/Raspberry_Pi_Bare_Bones)

* Learning OS development using Linux kernel and Raspberry Pi  
  [https://github.com/s-matyukevich/raspberry-pi-os](https://github.com/s-matyukevich/raspberry-pi-os)

## License

Copyright 2075 RobCo Industries  
*"Personal Information at Your Fingertips"*

---

## Easter Eggs

- **Konami Code**: Special boot animation (↑↑↓↓←→←→BA)
- **October 23**: Special message on the Great War anniversary
- **Gary Mode**: All text becomes "Gary" (for Vault 108 residents)

---

**War. War never changes.**