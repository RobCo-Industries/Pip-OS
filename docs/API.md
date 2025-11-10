# PIP-OS API Reference
## System Call Interface for ROM and Holotape Development

### Overview

PIP-OS provides a comprehensive system call interface for ROM (Read-Only Memory) applications like Deitrix and holotape programs. This API abstracts hardware access and provides a stable interface across different Pip-Boy hardware configurations.

## System Call Numbers

| Number | Function | Description |
|--------|----------|-------------|
| 0x01 | draw_line | Draw line between two points |
| 0x02 | draw_point | Draw single pixel |
| 0x03 | draw_text | Render text string |
| 0x04 | clear_screen | Clear display |
| 0x10 | read_buttons | Read button states |
| 0x11 | read_dial | Read rotary encoder |
| 0x20 | play_tone | Play audio tone |
| 0x21 | play_sample | Play audio sample |
| 0x30 | read_sensor | Read sensor value |
| 0x40 | get_time | Get RTC time |
| 0x41 | get_battery | Get battery level |
| 0x50 | read_save | Read save data |
| 0x51 | write_save | Write save data |

## Display API

### draw_line(x1, y1, x2, y2)
Draw a line using Bresenham algorithm.

### draw_point(x, y, color)
Draw a single point/pixel.

### draw_text(x, y, text, size)
Render text at specified position.

### clear_screen(color)
Clear entire screen to specified color.

## Input API

### read_buttons()
Returns bitmask of pressed buttons:
- Bit 0: UP
- Bit 1: DOWN
- Bit 2: LEFT
- Bit 3: RIGHT
- Bit 4: SELECT
- Bit 5: DIAL PRESS

### read_dial()
Returns current rotary encoder position.

## Audio API

### play_tone(frequency, duration)
Play tone at specified frequency (Hz) for duration (ms).

### play_sample(data, length)
Play 8-bit PCM audio sample.

## Sensor API

### read_sensor(sensor_type, value_ptr)
Read sensor value:
- 0: RTC (timestamp)
- 1: Battery (millivolts)
- 2: Geiger counter (CPM)
- 3: GPS (coordinates)
- 4: Accelerometer (mg)
- 5: Temperature (Celsius * 100)
- 6: Heart rate (BPM)

## System API

### get_time(time_ptr)
Get current time from RTC.

### get_battery_level()
Returns battery percentage (0-100).

## Storage API

### read_save(offset, buffer, size)
Read from save data area.

### write_save(offset, buffer, size)
Write to save data area.

---

For detailed examples and usage, see full API documentation.

**RobCo Industries - "Personal Information at Your Fingertips"**
