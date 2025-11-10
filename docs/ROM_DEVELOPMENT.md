# ROM Development Guide for PIP-OS
## Creating Custom ROMs and Applications

## Overview

ROMs (Read-Only Memory packages) are the primary application format for PIP-OS. The official Deitrix ROM provides the complete Pip-Boy user interface, but developers can create custom ROMs for specialized applications.

## ROM Structure

### ROM Header

Every ROM must begin with a standard header:

```c
typedef struct {
    char magic[4];           // Must be "ROM1"
    char name[16];           // ROM name (e.g., "DEITRIX")
    char version[8];         // Version string (e.g., "303")
    uint32_t load_address;   // Where to load (0x00010000)
    uint32_t entry_point;    // Where to start (e.g., 0x00010100)
    uint32_t size;           // Size in bytes (max 64KB)
    uint32_t checksum;       // CRC32 of ROM data
} __attribute__((packed)) rom_header_t;
```

### ROM Layout

```
+-------------------+  0x00010000
| ROM Header (32B)  |
+-------------------+  0x00010020
| Initialization    |
+-------------------+  
| Code Section      |
+-------------------+
| Data Section      |
+-------------------+
| BSS Section       |
+-------------------+  < 0x00020000
```

## Memory Map

Your ROM has access to the following memory regions:

```
0x00010000 - 0x0001FFFF : ROM Space (64KB) - Your code
0x00020000 - 0x0002FFFF : Application RAM (64KB) - Read/Write
0x00030000 - 0x0003FFFF : Graphics Buffer (64KB) - Display
0x00040000 - [LIMIT]    : User Heap - malloc/free
```

**Important**: Do not write to ROM space (0x00010000-0x0001FFFF) after loading.

## Creating a Simple ROM

### Step 1: Setup Project

```
my_rom/
├── src/
│   ├── main.c
│   ├── startup.S
│   └── syscalls.c
├── include/
│   └── pipos.h
├── linker.ld
└── Makefile
```

### Step 2: ROM Entry Point

```c
// src/main.c
#include <stdint.h>
#include "pipos.h"

// ROM entry point
void rom_main(void) {
    // Initialize your ROM
    clear_screen(0x000000);
    draw_text(10, 10, "Hello from Custom ROM!", 2);
    
    // Main loop
    while (1) {
        uint32_t buttons = read_buttons();
        
        if (buttons & BUTTON_SELECT) {
            // Handle input
        }
        
        // Update display, etc.
    }
}
```

### Step 3: System Call Interface

```c
// include/pipos.h
#ifndef PIPOS_H
#define PIPOS_H

#include <stdint.h>
#include <stdbool.h>

// System call wrappers
int32_t draw_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2);
int32_t draw_point(int32_t x, int32_t y, uint32_t color);
int32_t draw_text(int32_t x, int32_t y, const char* text, uint32_t size);
int32_t clear_screen(uint32_t color);

uint32_t read_buttons(void);
int32_t read_dial(void);

int32_t play_tone(uint16_t frequency, uint16_t duration);

uint8_t get_battery_level(void);

// Button definitions
#define BUTTON_UP      (1 << 0)
#define BUTTON_DOWN    (1 << 1)
#define BUTTON_LEFT    (1 << 2)
#define BUTTON_RIGHT   (1 << 3)
#define BUTTON_SELECT  (1 << 4)

#endif // PIPOS_H
```

### Step 4: System Call Implementation

```c
// src/syscalls.c
#include "pipos.h"

// System call numbers
#define SYS_DRAW_LINE    0x01
#define SYS_DRAW_POINT   0x02
#define SYS_DRAW_TEXT    0x03
#define SYS_CLEAR_SCREEN 0x04
#define SYS_READ_BUTTONS 0x10
#define SYS_READ_DIAL    0x11
#define SYS_PLAY_TONE    0x20
#define SYS_GET_BATTERY  0x41

// Inline assembly for system calls
static inline int32_t syscall4(uint32_t num, uint32_t a, uint32_t b, 
                                uint32_t c, uint32_t d) {
    register uint32_t r0 asm("r0") = a;
    register uint32_t r1 asm("r1") = b;
    register uint32_t r2 asm("r2") = c;
    register uint32_t r3 asm("r3") = d;
    register uint32_t r7 asm("r7") = num;
    
    asm volatile("svc #0" 
                 : "+r"(r0) 
                 : "r"(r1), "r"(r2), "r"(r3), "r"(r7)
                 : "memory");
    
    return r0;
}

int32_t draw_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    return syscall4(SYS_DRAW_LINE, x1, y1, x2, y2);
}

int32_t draw_text(int32_t x, int32_t y, const char* text, uint32_t size) {
    return syscall4(SYS_DRAW_TEXT, x, y, (uint32_t)text, size);
}

int32_t clear_screen(uint32_t color) {
    return syscall4(SYS_CLEAR_SCREEN, color, 0, 0, 0);
}

uint32_t read_buttons(void) {
    return syscall4(SYS_READ_BUTTONS, 0, 0, 0, 0);
}
```

### Step 5: Linker Script

```ld
/* linker.ld */
MEMORY
{
    ROM (rx)  : ORIGIN = 0x00010000, LENGTH = 64K
    RAM (rwx) : ORIGIN = 0x00020000, LENGTH = 64K
}

SECTIONS
{
    . = 0x00010000;
    
    .header : {
        KEEP(*(.rom_header))
    } > ROM
    
    .text : {
        *(.text.start)
        *(.text*)
        *(.rodata*)
    } > ROM
    
    .data : {
        *(.data*)
    } > RAM AT > ROM
    
    .bss : {
        *(.bss*)
        *(COMMON)
    } > RAM
    
    /DISCARD/ : {
        *(.comment)
        *(.note*)
    }
}
```

### Step 6: ROM Header in Assembly

```asm
; src/startup.S
.section .rom_header, "a"
.global rom_header

rom_header:
    .ascii "ROM1"              ; Magic
    .ascii "MYROM\0\0\0\0\0\0\0\0\0\0\0"  ; Name (16 bytes)
    .ascii "001\0\0\0\0\0"     ; Version (8 bytes)
    .word 0x00010000           ; Load address
    .word _start               ; Entry point
    .word _rom_size            ; Size
    .word 0x00000000           ; Checksum (calculated later)

.section .text.start
.global _start

_start:
    ; Setup stack
    ldr sp, =_stack_top
    
    ; Clear BSS
    ldr r0, =_bss_start
    ldr r1, =_bss_end
    mov r2, #0
.bss_clear_loop:
    cmp r0, r1
    bge .bss_clear_done
    str r2, [r0], #4
    b .bss_clear_loop
.bss_clear_done:

    ; Call ROM main
    bl rom_main
    
    ; Should never return
.hang:
    b .hang

; Stack (use application RAM)
.section .bss
.align 4
_stack:
    .space 4096
_stack_top:
```

### Step 7: Makefile

```makefile
# Makefile
TARGET = myrom
ARMGNU = arm-none-eabi

CC = $(ARMGNU)-gcc
AS = $(ARMGNU)-as
LD = $(ARMGNU)-ld
OC = $(ARMGNU)-objcopy

CFLAGS = -Wall -O2 -nostdlib -nostartfiles -ffreestanding
CFLAGS += -march=armv7-a -mtune=cortex-a7
CFLAGS += -I./include

LDFLAGS = -T linker.ld

OBJS = src/startup.o src/main.o src/syscalls.o

all: $(TARGET).rom

$(TARGET).elf: $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

$(TARGET).bin: $(TARGET).elf
	$(OC) -O binary $< $@

$(TARGET).rom: $(TARGET).bin
	./create_rom.sh $(TARGET).bin $(TARGET).rom

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET).elf $(TARGET).bin $(TARGET).rom
```

### Step 8: ROM Creation Script

```bash
#!/bin/bash
# create_rom.sh - Calculate checksum and create final ROM

INPUT=$1
OUTPUT=$2

if [ ! -f "$INPUT" ]; then
    echo "Input file not found"
    exit 1
fi

# Calculate CRC32 (simplified - use real CRC32 tool)
SIZE=$(stat -f%z "$INPUT")
CHECKSUM=$(cksum "$INPUT" | awk '{print $1}')

# Update checksum in header (offset 28)
# In practice, use a proper tool to update the binary

cp "$INPUT" "$OUTPUT"
echo "ROM created: $OUTPUT (size: $SIZE bytes, checksum: $CHECKSUM)"
```

## Example: Simple Menu ROM

```c
// A simple menu-based ROM
#include <stdint.h>
#include <stdbool.h>
#include "pipos.h"

#define MENU_ITEMS 4
const char* menu[] = {
    "Stats",
    "Inventory",
    "Data",
    "Map"
};

int selected = 0;

void draw_menu(void) {
    clear_screen(0x000000);
    
    // Title
    draw_text(80, 10, "PIP-BOY 3000", 2);
    
    // Menu items
    for (int i = 0; i < MENU_ITEMS; i++) {
        uint32_t color = (i == selected) ? 0x00FF00 : 0xFFFFFF;
        
        if (i == selected) {
            draw_text(40, 60 + i * 30, ">", 2);
        }
        
        draw_text(60, 60 + i * 30, menu[i], 2);
    }
    
    // Battery indicator
    uint8_t battery = get_battery_level();
    draw_text(10, 220, "BAT:", 1);
    // Draw battery bar...
}

void handle_input(void) {
    static uint32_t last_buttons = 0;
    uint32_t buttons = read_buttons();
    uint32_t pressed = buttons & ~last_buttons;
    
    if (pressed & BUTTON_UP) {
        play_tone(440, 50);
        selected = (selected - 1 + MENU_ITEMS) % MENU_ITEMS;
        draw_menu();
    }
    
    if (pressed & BUTTON_DOWN) {
        play_tone(440, 50);
        selected = (selected + 1) % MENU_ITEMS;
        draw_menu();
    }
    
    if (pressed & BUTTON_SELECT) {
        play_tone(880, 100);
        // Handle selection
    }
    
    last_buttons = buttons;
}

void rom_main(void) {
    draw_menu();
    
    while (1) {
        handle_input();
        
        // Small delay
        for (volatile int i = 0; i < 100000; i++);
    }
}
```

## Best Practices

### 1. Memory Management
- Keep ROM code under 64KB
- Use application RAM for runtime data
- Don't assume heap size
- Check malloc return values

### 2. Display
- Clear screen before major updates
- Use double buffering if available
- Limit draw calls in loops
- Test on minimum resolution (320x240)

### 3. Input
- Debounce buttons in software
- Use edge detection (button press vs hold)
- Provide visual feedback for input
- Support both buttons and dial

### 4. Audio
- Keep tones short
- Don't block on audio
- Provide option to mute
- Use standard note frequencies

### 5. Power
- Check battery regularly
- Warn user at low battery
- Support sleep mode
- Minimize busy loops

### 6. Compatibility
- Test on all hardware tiers
- Handle missing sensors gracefully
- Use fallbacks for unavailable features
- Document hardware requirements

## Testing

### QEMU Testing
```bash
qemu-system-arm -M raspi2 -kernel myrom.img -serial stdio
```

### Hardware Testing
1. Copy ROM to SD card
2. Configure PIP-OS to load your ROM
3. Boot and test functionality
4. Check serial output for errors

## Distribution

### ROM Package Structure
```
myrom-v1.0/
├── myrom.rom          # The ROM binary
├── README.txt         # Documentation
├── LICENSE.txt        # License
└── HARDWARE.txt       # Hardware requirements
```

### ROM Metadata (optional external file)
```json
{
    "name": "My Custom ROM",
    "version": "1.0",
    "author": "Developer Name",
    "description": "A custom ROM for PIP-OS",
    "requirements": {
        "min_pipos_version": "7.1.0.8",
        "min_memory": 65536,
        "sensors": ["RTC"],
        "hardware_tier": 1
    }
}
```

## Advanced Topics

### Multi-file ROMs
For ROMs larger than 64KB, implement overlay system:
1. Keep core in ROM space
2. Load additional data from storage
3. Swap overlays as needed

### Save Data
Use save API for persistent storage:
```c
// Save high score
uint32_t score = 12345;
write_save(0, &score, sizeof(score));

// Load high score
uint32_t loaded_score;
read_save(0, &loaded_score, sizeof(loaded_score));
```

### Graphics Optimization
```c
// Use dirty rectangles to minimize redraws
typedef struct {
    int x, y, w, h;
    bool dirty;
} rect_t;
```

## Troubleshooting

### ROM Won't Load
- Check header magic "ROM1"
- Verify load address (0x00010000)
- Check ROM size (< 64KB)
- Validate checksum

### Crashes on Startup
- Check stack setup in startup.S
- Verify BSS clearing
- Check for uninitialized variables
- Review linker script

### Display Issues
- Verify coordinates in bounds
- Check string null termination
- Test on emulator first
- Add debug output via UART

## Resources

- [PIP-OS API Reference](API.md)
- [Hardware Guide](HARDWARE.md)
- [Deitrix ROM Source](https://github.com/RobCo-Industries/Deitrix)
- [ARM Assembly Reference](https://developer.arm.com/documentation/)

## Example ROMs

### Minimal ROM Template
Available at: `examples/minimal_rom/`

### Game ROM Template
Available at: `examples/game_rom/`

### Utility ROM Template
Available at: `examples/utility_rom/`

---

**Happy ROM Development!**

*RobCo Industries - Empowering Developers*
