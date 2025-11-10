# Contributing to PIP-OS

Thank you for your interest in contributing to PIP-OS! This document provides guidelines and instructions for contributing to the project.

## Code of Conduct

### Our Standards

- Be respectful and inclusive
- Accept constructive criticism gracefully
- Focus on what's best for the community
- Show empathy towards other community members

## How to Contribute

### Reporting Bugs

Before submitting a bug report:
1. Check existing issues to avoid duplicates
2. Collect relevant information (hardware, PIP-OS version, build configuration)
3. Create a minimal reproducible example if possible

Bug reports should include:
- Clear description of the issue
- Steps to reproduce
- Expected vs actual behavior
- Hardware configuration (BCM model, peripherals)
- Build configuration (BCM=?, toolchain version)
- Serial output or error messages

### Suggesting Features

Feature suggestions are welcome! Please:
1. Check if the feature aligns with PIP-OS goals
2. Describe the use case clearly
3. Consider compatibility with existing ROMs
4. Provide examples of how it would work

### Pull Requests

#### Before You Start

1. **Discuss major changes** - Open an issue first for significant features
2. **Follow coding style** - Match existing code conventions
3. **Test thoroughly** - Test on real hardware if possible
4. **Update documentation** - Include relevant docs updates

#### Development Workflow

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Make your changes
4. Test your changes
5. Commit with clear messages
6. Push to your fork
7. Open a Pull Request

#### Commit Messages

Follow these guidelines:
```
Short summary (50 chars or less)

More detailed explanation if needed. Wrap at 72 characters.
Explain what and why, not how.

- Bullet points are okay
- Use present tense ("Add feature" not "Added feature")
- Reference issues: "Fixes #123"
```

Example:
```
Add Geiger counter driver for radiation detection

Implements GPIO-based pulse counting for radiation detection.
Supports interrupt-driven counting for accurate CPM measurement.
Includes calibration for different tube types.

Fixes #456
```

## Development Guidelines

### Coding Style

#### C Code
```c
// Use descriptive names
void power_init(void);
uint8_t power_get_battery_percentage(void);

// Indent with 4 spaces (no tabs)
if (condition) {
    do_something();
} else {
    do_something_else();
}

// Comment non-obvious code
// Calculate battery percentage from voltage
// Assumes 3.0V = 0%, 4.2V = 100% for LiPo
uint8_t percentage = (voltage - 3000) * 100 / 1200;

// Use stdint types
uint32_t value;  // Not: unsigned long
uint8_t byte;    // Not: unsigned char
```

#### Assembly Code
```asm
; Use meaningful labels
.global _start
_start:
    ; Comment complex operations
    ; Setup stack pointer for kernel
    ldr sp, =_stack_top
    
    ; Branch to C code
    bl kernel_main
```

#### File Organization
```
src/kernel/
├── subsystem.h      # Public interface
├── subsystem.c      # Implementation
└── subsystem_impl.h # Private definitions (if needed)
```

### Testing

#### Build Testing
```bash
# Test all configurations
for BCM in 2835 2836 2837; do
    export BCM=$BCM
    make clean
    make || exit 1
done
```

#### QEMU Testing
```bash
# Test with QEMU
cd build
export BCM=2836
make run

# Expected output:
# PIP-OS V7.1.0.8 boot sequence
# Subsystem initialization
# ROM/holotape check
# Main loop
```

#### Hardware Testing
Test on actual hardware when possible:
- Raspberry Pi Zero (BCM2835)
- Raspberry Pi 2 (BCM2836)
- Raspberry Pi 3 (BCM2837)

### Documentation

Update documentation for any user-facing changes:

- **README.md** - For major features or changes
- **docs/API.md** - For new system calls or API changes
- **docs/HARDWARE.md** - For hardware-related changes
- **docs/ROM_DEVELOPMENT.md** - For ROM developer-facing changes
- **CHANGELOG.md** - For all changes

## Areas for Contribution

### High Priority

1. **Hardware Drivers**
   - SPI TFT display drivers (ILI9341, ST7789)
   - I2S audio driver (PCM5102A)
   - GPIO input handling
   - I2C sensor drivers (RTC, IMU, etc.)

2. **Graphics System**
   - Bresenham line drawing
   - Text rendering engine
   - Sprite system
   - Framebuffer management

3. **Audio System**
   - PWM audio output
   - Tone generation
   - Sample playback
   - Audio mixing

4. **Input System**
   - Button debouncing
   - Rotary encoder driver
   - Touch screen support
   - Input event queue

### Medium Priority

1. **Power Management**
   - ADC for battery monitoring
   - CPU frequency scaling
   - Display backlight control
   - Sleep/wake implementation

2. **Sensors**
   - GPS UART driver
   - Geiger counter pulse counting
   - Accelerometer driver
   - Temperature sensor

3. **Storage**
   - SD card access
   - FAT filesystem support
   - Save data management
   - ROM loading from storage

### Low Priority

1. **Network** (Pi 3/4)
   - Wi-Fi support
   - Bluetooth
   - Network stack

2. **Advanced Features**
   - Multi-tasking
   - DMA support
   - USB support

## Project Structure

```
Pip-OS/
├── src/
│   ├── kernel/          # Core kernel
│   │   ├── boot_display.c
│   │   ├── rom_loader.c
│   │   ├── syscall.c
│   │   ├── power.c
│   │   ├── audio.c
│   │   └── k_libc/      # Kernel libc
│   ├── aarch32/         # 32-bit ARM boot
│   ├── aarch64/         # 64-bit ARM boot (future)
│   └── libc/            # User-space libc
├── include/             # Public headers
├── build/               # Build system
│   ├── Makefile
│   └── linker.ld
├── docs/                # Documentation
│   ├── API.md
│   ├── HARDWARE.md
│   └── ROM_DEVELOPMENT.md
└── boot/                # Boot configuration
```

## System Call Development

When adding new system calls:

1. **Define in syscall.h**
```c
#define SYSCALL_NEW_FEATURE  0x60
int32_t sys_new_feature(uint32_t param);
```

2. **Implement in syscall.c**
```c
int32_t sys_new_feature(uint32_t param) {
    // Implementation
    return result;
}
```

3. **Register in syscall_init()**
```c
void syscall_init(void) {
    // ...
    syscall_table[SYSCALL_NEW_FEATURE] = 
        (syscall_handler_t)sys_new_feature;
}
```

4. **Document in API.md**
```markdown
#### new_feature
```c
int32_t new_feature(uint32_t param);
```
**System Call**: 0x60
**Description**: Does something useful
```

5. **Update CHANGELOG.md**

## Hardware Driver Development

### Driver Template

```c
// src/kernel/driver_name.h
#ifndef DRIVER_NAME_H
#define DRIVER_NAME_H

#include <stdint.h>

void driver_init(void);
int32_t driver_operation(uint32_t param);

#endif
```

```c
// src/kernel/driver_name.c
#include "driver_name.h"
#include "io.h"

// Hardware registers
#define DRIVER_BASE  0x3F200000
#define DRIVER_REG1  (DRIVER_BASE + 0x00)

void driver_init(void) {
    // Initialize hardware
    mmio_write(DRIVER_REG1, 0x00);
}

int32_t driver_operation(uint32_t param) {
    // Perform operation
    return 0;
}
```

## Review Process

### Pull Request Checklist

- [ ] Code compiles without warnings
- [ ] Code follows project style guidelines
- [ ] All tests pass
- [ ] Documentation is updated
- [ ] CHANGELOG is updated
- [ ] Commit messages are clear
- [ ] No unrelated changes included

### Review Criteria

Reviewers will check:
1. **Correctness** - Does it work as intended?
2. **Style** - Does it follow coding standards?
3. **Efficiency** - Is it reasonably optimized?
4. **Compatibility** - Does it break existing code?
5. **Documentation** - Is it well documented?

## Getting Help

- **Questions**: Open a GitHub Discussion
- **Bugs**: Open a GitHub Issue
- **Chat**: (If applicable, add Discord/IRC info)

## Resources

- [ARM Architecture Reference](https://developer.arm.com/documentation/)
- [BCM2835 Datasheet](https://www.raspberrypi.org/documentation/)
- [OSDev Wiki](https://wiki.osdev.org/)
- [Raspberry Pi Bare Bones](https://wiki.osdev.org/Raspberry_Pi_Bare_Bones)

## License

By contributing to PIP-OS, you agree that your contributions will be licensed under the same license as the project.

---

**Thank you for contributing to PIP-OS!**

*RobCo Industries - Building the Future Together*
