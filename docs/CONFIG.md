# Configuration

Pip-OS reads `/PIPBOY/PIPOS.CFG` from the SD card at boot. The file is optional. Every key in it is optional too.

## Format

The file is INI text, parsed by RobCo-Core's `ini.h`. The syntax rules are in the [configuration files reference](https://github.com/RobCo-Industries/RobCo-Core/blob/ec1677971cbb5261d0eacec92b2af8fe91325495/docs/guide/api/buses.md#configuration-files-inih). Section and key names are case-sensitive. The file may hold at most 4096 bytes.

```
[display]
scheme = amber
line_delay_ms = 150

[audio]
volume = 80
beep = yes
```

`boot/PIPOS.CFG` is the sample that `make sdimage` and `make bootdir` put on the card.

## Keys

| Section | Key | Values | Default | Effect |
|---|---|---|---|---|
| `display` | `scheme` | `green`, `amber`, `blue`, `white`, any case | `green` | Colour scheme of the screen console |
| `display` | `line_delay_ms` | 0 to 2000 | `0` | Pause between two boot lines, in milliseconds |
| `display` | `backend` | `hdmi`, `tft`, `both`, any case | `hdmi` | Where frames go, see [SPI panel](#spi-panel) |
| `audio` | `volume` | 0 to 100 | `100` | Audio volume in percent |
| `audio` | `beep` | `yes`, `no`, `true`, `false`, `on`, `off`, `1`, `0`, any case | `yes` | Beep once at power-on |

Numbers are unsigned decimal. A `+` sign is accepted.

## SPI panel

These `[display]` keys set up the ILI9341 panel. The kernel reads them only when `backend` is `tft` or `both`. The defaults are RobCo-Core's `TFT_CONFIG_DEFAULT`. [HARDWARE.md](HARDWARE.md#spi-panel) has the wiring.

```
[display]
backend = both
tft_rotation = 270
```

| Key | Values | Default | Effect |
|---|---|---|---|
| `tft_cs` | `0` or `1` | `0` | SPI0 chip select, CE0 or CE1 |
| `tft_dc` | A free GPIO | `25` | Data/command line |
| `tft_reset` | A free GPIO or `none` | `4` | Reset line, active low |
| `tft_backlight` | A free GPIO or `none` | `none` | Backlight line, high when lit |
| `tft_hz` | 1000000 to 64000000 | `10000000` | SPI clock asked for, in Hz. The rate set is never higher. |
| `tft_rotation` | `0`, `90`, `180`, `270` | `90` | Panel rotation in degrees |
| `tft_bgr` | A boolean, as `beep` | `yes` | Blue-green-red colour order |
| `tft_invert` | A boolean, as `beep` | `no` | Inverted colours |

A free GPIO is one of 0, 1, 4, 12, 13, 19, 20, 21 and 25. D/C, reset and backlight need three different pins.

| Backend | HDMI | Panel |
|---|---|---|
| `hdmi` | Boot text, frames, panic text | Not used |
| `both` | Boot text, frames, panic text | The same frames, and a 40 by 15 copy of the boot text and panic text |
| `tft` | Boot text and panic text. It keeps the console after boot and never shows a frame. `power` screen off turns it black. | Frames, boot text and panic text |

## Errors

No configuration problem stops the boot. The kernel prints one marker per problem, then one summary marker with the settings in use.

| Case | What happens | Marker |
|---|---|---|
| No card | Defaults | `ev=config rc=nocard` |
| No file | Defaults | `ev=config rc=nofile` |
| File larger than 4096 bytes | Defaults | `ev=config rc=toobig` |
| Read error | Defaults | `ev=config rc=io` |
| A line that is not INI | Defaults for every key. `line` names the bad line. | `ev=config rc=syntax line=<n>` |
| Unknown section or key | The key is skipped | `ev=config rc=unknown key=<section>.<key> line=<n>` |
| Bad value | The key keeps its default. `tft_hz` out of range is clamped to 1000000 or 64000000. | `ev=config rc=badvalue key=<section>.<key> line=<n>` |
| A panel pin the buttons or the dial use | The key keeps its default | `ev=config rc=pin_input key=display.<key> line=<n>` |
| A panel pin audio uses: 18, 40, 41 or 45 | The key keeps its default | `ev=config rc=pin_audio key=display.<key> line=<n>` |
| A panel pin of I2C, SPI0 or the PL011: 2, 3, 7 to 11, 14, 15 | The key keeps its default | `ev=config rc=pin_bus key=display.<key> line=<n>` |
| Two panel lines on one pin | The backlight goes to `none`. A clash with D/C turns the panel off: `backend` goes to `hdmi`. | `ev=config rc=pin_twice key=display.<key> line=0` |

`ev=display` counts the refused `backend` and `tft_*` values in `bad`, see [BOOT.md](BOOT.md#the-display-backend).

The summary marker:

```
@RC stage=pipos ev=config rc=ok path=/PIPBOY/PIPOS.CFG line=0 scheme=amber line_delay_ms=150 volume=80 beep=1
```

On a syntax error, keys before the bad line may already have printed `unknown` or `badvalue` markers. None of them applies.
