# Hardware

Pip-OS runs on the Pi Zero, Zero W, Pi 1, Pi 2 and Pi 3 in 32-bit mode. The automated tests run in QEMU. One board run so far: Pip-OS `2d10c2c` on a Pi Zero W 1.1, with only a serial adapter wired. See [Hardware results](TESTING.md#hardware-results).

## What the kernel drives

| Part | Driver | Notes |
|---|---|---|
| HDMI framebuffer, 640x480x32 | RobCo-Core `fb.h` | The 320x240 surface is scaled by 2. See [API.md](API.md#graphics). |
| ILI9341 SPI panel, 320x240 | RobCo-Core `tft.h` | Off by default. See [SPI panel](#spi-panel). |
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

## SPI panel

The kernel can drive an ILI9341 panel on SPI0 with RobCo-Core's `tft.h`. `[display] backend` in `PIPOS.CFG` turns it on, see [CONFIG.md](CONFIG.md#spi-panel). The default wiring is RobCo-Core's, from the "SPI display" part of [BOARD_SUPPORT.md](https://github.com/RobCo-Industries/RobCo-Core/blob/815afabebf6baa728f69e2c2b016a188c5fd1119/docs/spec/BOARD_SUPPORT.md#spi-display):

| Panel pin | Pin | Key |
|---|---|---|
| SCK, SDI, SDO | GPIO 11, 10, 9: SPI0 at ALT0 | none |
| CS | GPIO 8, CE0 | `tft_cs = 0`. `1` is CE1, GPIO 7. |
| D/C | GPIO 25 | `tft_dc` |
| RESET | GPIO 4 | `tft_reset`, or `none` |
| LED | 3.3 V | `tft_backlight`, a GPIO that is high when lit, or `none` |

GPIO 0, 1, 4, 12, 13, 19, 20, 21 and 25 are the header pins that the buttons, the dial, audio, I2C, SPI0 and the PL011 leave free. The kernel refuses any other pin for D/C, reset and backlight, see [CONFIG.md](CONFIG.md#errors).

The SPI clock is 10 MHz by default, the fastest write clock in the ILI9341 data sheet. The divider rounds it down: 9.62 MHz with a 250 MHz core clock. `tft_hz` accepts 1 to 64 MHz. A rate above 10 MHz is outside the data sheet. RobCo-Core fails a send when no byte moves for 20 ms plus four byte times. The kernel then sends the whole frame once more, see [ARCHITECTURE.md](ARCHITECTURE.md#display-path).

How the picture sits on the glass depends on the module. `tft_rotation` turns it, `tft_bgr` swaps red and blue, and `tft_invert` inverts the colours. At 90 and 270 degrees, the default, the panel is 320x240 and shows the whole surface. At 0 and 180 degrees it is 240x320 and shows the left 240 columns and the top 240 rows. The rest of the surface never reaches the panel, and changes there send nothing.

What is validated: nothing on hardware. In QEMU the SPI controller works but no panel answers, so the kernel reports the panel `absent`. The tests rebuild the panel picture from the bytes sent with RobCo-Core's ILI9341 model, see [TESTING.md](TESTING.md#spi-panel). Orientation, colour order, timing and the backlight are not checked.

## Not supported

- Battery monitoring: `get_battery` returns `-PIP_ENODEV`.
- Sensors other than the clock: sensors 1 to 6 return `-PIP_ENODEV`.
- The Raspberry Pi 4 and 64-bit mode.
