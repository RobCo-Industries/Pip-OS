# Hardware

Pip-OS runs on the Pi Zero, Zero W, Pi 1, Pi 2 and Pi 3 in 32-bit mode. Every test runs in QEMU. Nothing is validated on real hardware yet.

## What the kernel drives

| Part | Driver | Notes |
|---|---|---|
| HDMI framebuffer, 640x480x32 | RobCo-Core `fb.h` | The 320x240 surface is scaled by 2. See [API.md](API.md#graphics). |
| Serial console, PL011 at 115200 baud | RobCo-Core `uart.h` | GPIO 14 and 15 |
| Script channel, mini UART at 115200 baud | RobCo-Core `uart_mini.h` | The second serial port in QEMU. The mini UART muxes no pin, and on a board its pins are the PL011's GPIO 14 and 15. |
| Buttons and dial | RobCo-Core `input.h` | Pins below |
| Audio | RobCo-Core `audio.h` | PWM. On QEMU, a trace on the UART. |
| RTC chip | RobCo-Core `rtc.h` | DS1307, DS1338 or DS3231 at I2C address `0x68`, on BSC1: SDA GPIO 2, SCL GPIO 3 |
| SD card | RobCo-Core `sd.h` and `fs.h` | FAT on the first partition that mounts. See [BOOT.md](BOOT.md#the-card). |

## Pins

The button and dial pins are BCM numbers from [`kernel/robco_config.h`](../kernel/robco_config.h), the example mapping of RobCo-Core's `examples/input`. Each button ties its pin to ground and uses the internal pull-up. Override a pin with `-D`, see [BUILD.md](BUILD.md#button-pins).

| Input | GPIO |
|---|---|
| UP | 5 |
| DOWN | 6 |
| LEFT | 17 |
| RIGHT | 22 |
| SELECT | 26 |
| BACK | 27 |
| Dial A | 23 |
| Dial B | 24 |
| Dial push | 16 |

The GPIO backend samples the pins at 100 Hz with a 20 ms debounce. The dial counts 4 A/B transitions per detent, `PIPOS_DIAL_PER_DETENT`.

RobCo-Core picks the audio pins from the board:

| Board | PWM output |
|---|---|
| Pi Zero, Zero W | GPIO 18, PWM0 at ALT5, mono |
| Pi 1 and Pi 2 | GPIO 40 and 45 at ALT0, the audio jack |
| Pi 3 | GPIO 40 and 41 at ALT0, the audio jack |

## Not supported

- The SPI display. `gfx_info` always reports `backend` 0, HDMI.
- Battery monitoring: `get_battery` returns `-PIP_ENODEV`.
- Sensors other than the clock: sensors 1 to 6 return `-PIP_ENODEV`.
- PCM streaming. `sample` plays one buffer of at most 65536 bytes at 8000 Hz.
- The Raspberry Pi 4 and 64-bit mode.
