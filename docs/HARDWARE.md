# PIP-OS Hardware Guide
## Supported Configurations and Pin Assignments

## Overview

PIP-OS supports three tiers of Pip-Boy hardware configurations, each targeting different Raspberry Pi models and feature sets.

## Tier 1: Basic Pip-Boy (Raspberry Pi Zero/1)

### Specifications
- **SoC**: BCM2835 (ARMv6)
- **CPU**: Single-core ARM1176JZF-S @ 700MHz
- **RAM**: 512MB
- **Display**: 320x240 SPI TFT (ILI9341 or ST7789)
- **Input**: 5 buttons + rotary encoder
- **Audio**: PWM through GPIO
- **Sensors**: RTC module (DS3231 via I2C)
- **Power**: 2x AA batteries or single 18650

### GPIO Pin Assignment

```
Display (SPI0):
  MOSI  - GPIO 10
  MISO  - GPIO 9
  SCLK  - GPIO 11
  CS    - GPIO 8
  DC    - GPIO 24
  RST   - GPIO 25

Buttons:
  UP    - GPIO 17
  DOWN  - GPIO 27
  LEFT  - GPIO 22
  RIGHT - GPIO 23
  SELECT- GPIO 4

Rotary Encoder:
  A     - GPIO 5
  B     - GPIO 6
  PRESS - GPIO 13

Audio (PWM):
  OUT   - GPIO 18 (PWM0)

I2C (RTC):
  SDA   - GPIO 2
  SCL   - GPIO 3

Power:
  BATTERY_SENSE - GPIO 26 (ADC via voltage divider)
```

### Display Connection

For ILI9341 320x240 TFT:
```
TFT Pin → Pi Pin
VCC     → 3.3V
GND     → GND
CS      → GPIO 8
RESET   → GPIO 25
DC/RS   → GPIO 24
MOSI    → GPIO 10
SCK     → GPIO 11
LED     → 3.3V (with 100Ω resistor)
MISO    → GPIO 9 (optional)
```

### Power Circuit

```
Battery (3-4.2V) → LDO 5V → Pi
                         ↓
                  Voltage Divider → GPIO 26
                  (R1: 2kΩ, R2: 1kΩ)
```

## Tier 2: Standard Pip-Boy (Raspberry Pi 2/3)

### Specifications
- **SoC**: BCM2836 (Pi 2) or BCM2837 (Pi 3)
- **CPU**: Quad-core Cortex-A7 @ 900MHz (Pi 2) or Cortex-A53 @ 1.2GHz (Pi 3)
- **RAM**: 1GB
- **Display**: 480x320 SPI TFT or 640x480 HDMI
- **Input**: Full button set (10 buttons) + rotary encoder + optional touch
- **Audio**: I2S DAC (PCM5102A or similar)
- **Sensors**: RTC, GPS (UART), Accelerometer (I2C), Geiger counter
- **Power**: 18650 Li-Ion with charging circuit

### GPIO Pin Assignment

```
Display (SPI0):
  (Same as Tier 1)

Buttons:
  UP     - GPIO 17
  DOWN   - GPIO 27
  LEFT   - GPIO 22
  RIGHT  - GPIO 23
  SELECT - GPIO 4
  A      - GPIO 16
  B      - GPIO 20
  X      - GPIO 21
  Y      - GPIO 12
  START  - GPIO 19

Rotary Encoder:
  (Same as Tier 1)

Audio (I2S):
  LRCLK - GPIO 19 (conflicts with START, use separate IC)
  DIN   - GPIO 21
  BCK   - GPIO 18

I2C (Sensors):
  SDA   - GPIO 2
  SCL   - GPIO 3

GPS (UART):
  TX    - GPIO 14
  RX    - GPIO 15

Geiger Counter:
  PULSE - GPIO 7 (interrupt input)

Power Management:
  BATTERY_SENSE - GPIO 26
  CHARGE_STATUS - GPIO 24
  POWER_GOOD    - GPIO 25
```

### I2S Audio Connection

For PCM5102A DAC:
```
DAC Pin → Pi Pin
VCC     → 3.3V
GND     → GND
BCK     → GPIO 18
DIN     → GPIO 21
LRCK    → GPIO 19
SCK     → Not connected
FLT     → GND
DEMP    → GND
XSMT    → 3.3V
```

### GPS Module Connection

For NEO-6M or similar:
```
GPS Pin → Pi Pin
VCC     → 3.3V
GND     → GND
TX      → GPIO 15 (Pi RX)
RX      → GPIO 14 (Pi TX)
```

## Tier 3: Deluxe Pip-Boy (Raspberry Pi 4)

### Specifications
- **SoC**: BCM2711
- **CPU**: Quad-core Cortex-A72 @ 1.5GHz
- **RAM**: 2-8GB
- **Display**: 800x480 HDMI touchscreen or large TFT
- **Input**: Capacitive touch + full button set + encoder
- **Audio**: High-quality I2S DAC + Bluetooth
- **Sensors**: Full sensor suite (RTC, GPS, IMU, Geiger, temp, heart rate)
- **Power**: Dual 18650 with advanced power management

### Additional Features
- Bluetooth for wireless holotape sync
- Wi-Fi for online features (when available)
- USB-C charging with fast charge support
- RGB LED indicators
- Haptic feedback motor

### Extended GPIO Assignment

```
Touch Controller (I2C):
  SDA   - GPIO 2
  SCL   - GPIO 3
  INT   - GPIO 27

IMU (I2C):
  (Shared with touch)
  
Heart Rate Sensor (I2C):
  (Shared with touch)

Haptic Motor (PWM):
  CTRL  - GPIO 13 (PWM1)

RGB LEDs (WS2812B):
  DATA  - GPIO 10

USB-C Power:
  PD_INT - GPIO 26
```

## Display Options

### SPI TFT Displays

#### ILI9341 (320x240)
- Speed: Up to 40 MHz SPI
- Colors: 262K (18-bit)
- Touch: Optional resistive overlay
- Power: ~100mA @ 3.3V

#### ST7789 (240x240 / 320x240)
- Speed: Up to 62.5 MHz SPI
- Colors: 262K (18-bit)
- Touch: Optional capacitive
- Power: ~80mA @ 3.3V

### HDMI Displays

#### 480x320 DSI/HDMI
- Official 3.5" touchscreen
- Capacitive touch (10-point)
- Power via Pi

#### 800x480 HDMI
- 7" HDMI display
- Capacitive or resistive touch
- External power recommended

## Sensor Modules

### RTC Module (DS3231)
```
Module  → Pi
VCC     → 3.3V
GND     → GND
SDA     → GPIO 2
SCL     → GPIO 3
32K     → Not connected
SQW     → GPIO 4 (optional alarm)
```
- I2C Address: 0x68
- Battery backup: CR2032
- Accuracy: ±2ppm

### GPS Module (NEO-6M)
- Update rate: 1-10 Hz
- Accuracy: 2.5m CEP
- Cold start: ~30s
- Warm start: ~1s
- UART: 9600 baud (default)

### Accelerometer (MPU6050/LSM303)
- I2C Address: 0x68 or 0x19
- Range: ±2g to ±16g
- Update rate: Up to 1kHz
- Features: 3-axis accel + gyro

### Geiger Counter Interface
```
Counter Pin → Pi Pin
VCC         → 5V
GND         → GND
PULSE       → GPIO 7
```
- Pulse: Active high, ~100μs width
- Count pulses to calculate CPM
- Use interrupt for accurate counting

## Power Management

### Battery Configuration

#### Single 18650 (Tier 1/2)
```
18650 → Protection Board → Boost Converter (5V) → Pi
  ↓
Voltage Divider → GPIO 26 (monitor)
```

#### Dual 18650 (Tier 3)
```
2x 18650 (Series) → BMS → Buck Converter (5V) → Pi
  ↓
Fuel Gauge (I2C) → Monitor
```

### Charging

For single-cell LiPo/18650:
```
USB-C → TP4056 Charge Controller → Battery
          ↓
     CHRG/DONE → GPIO (status LEDs)
```

For dual-cell:
```
USB-C → BMS with charging → Batteries
          ↓
     Status signals → I2C/GPIO
```

### Power Consumption

| Mode | Tier 1 | Tier 2 | Tier 3 |
|------|--------|--------|--------|
| Active | 400mA | 800mA | 1.2A |
| Idle | 250mA | 500mA | 800mA |
| Sleep | 100mA | 150mA | 200mA |
| Deep Sleep | 50mA | 50mA | 50mA |

### Battery Life Estimates

With 3000mAh 18650:
- Tier 1 Active: ~7 hours
- Tier 2 Active: ~3.5 hours
- Tier 3 Active (dual): ~5 hours

## Assembly Notes

### Enclosure Considerations
- Waterproofing: IP54 recommended
- Button placement: Ergonomic for wrist mount
- Display viewing angle: 45° optimal
- Weight: <300g for wearability

### Thermal Management
- Pi 3/4 may need heatsink
- Passive cooling preferred
- Vents for air circulation
- Keep away from battery

### Mechanical
- Shock absorption for display
- Strain relief for wiring
- Mounting holes for PCB
- Easy access to SD card

## Testing and Calibration

### Display Calibration
1. Check SPI clock speed
2. Verify orientation
3. Test touch accuracy (if applicable)
4. Adjust brightness

### Input Testing
```bash
# Test buttons
evtest /dev/input/event0

# Test rotary encoder
evtest /dev/input/event1
```

### Sensor Calibration
1. **RTC**: Set time, verify drift over 24h
2. **Accelerometer**: Six-position calibration
3. **GPS**: Wait for fix, check accuracy
4. **Battery**: Calibrate voltage divider

## Troubleshooting

### Display Issues
- Black screen: Check power, SPI connections
- Scrambled display: Verify SPI speed, check wiring
- Touch not working: Test I2C, check driver

### Input Issues
- Buttons not responding: Test with multimeter, check pull-ups
- Encoder not working: Verify both channels, check debouncing
- Ghost inputs: Add debouncing capacitors

### Power Issues
- Won't boot: Check 5V rail, verify current capacity
- Random resets: Add decoupling capacitors
- Low battery voltage: Check voltage divider, calibrate ADC

### Audio Issues
- No sound: Verify I2S/PWM config, check amplifier
- Distorted audio: Check sample rate, reduce volume
- Noise: Add filtering, improve power supply

## Parts List

### Tier 1 Basic Kit
- Raspberry Pi Zero W
- 320x240 SPI TFT
- 5x tactile buttons
- Rotary encoder with button
- DS3231 RTC module
- Passive buzzer
- 18650 battery holder
- TP4056 charger module
- 5V boost converter
- Resistors, capacitors, wire

Estimated cost: $60-80

### Tier 2 Standard Kit
- Raspberry Pi 2 or 3
- 480x320 SPI TFT
- 10x tactile buttons
- Rotary encoder
- DS3231 RTC
- PCM5102A I2S DAC
- NEO-6M GPS
- MPU6050 IMU
- Geiger counter module (optional)
- 18650 battery + BMS
- Buck converter
- Enclosure

Estimated cost: $120-180

### Tier 3 Deluxe Kit
- Raspberry Pi 4 (4GB)
- 800x480 HDMI touchscreen
- Touch controller
- 10x buttons + encoder
- Full sensor suite
- High-quality DAC
- 2x 18650 + BMS
- USB-C PD
- RGB LEDs
- Haptic motor
- Premium enclosure

Estimated cost: $250-350

## Resources

- [Raspberry Pi Pinout](https://pinout.xyz/)
- [BCM2835 Datasheet](https://www.raspberrypi.org/documentation/)
- [ILI9341 Driver](https://github.com/notro/fbtft/wiki)
- [PCM5102A Datasheet](http://www.ti.com/product/PCM5102A)

---

**RobCo Industries - Building the Future of Personal Computing**
