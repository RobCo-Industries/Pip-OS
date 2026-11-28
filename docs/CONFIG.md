# Configuration

Pip-OS reads `/PIPBOY/PIPOS.CFG` from the SD card at boot. The file is optional. Every key in it is optional too.

## Format

The file is INI text, parsed by RobCo-Core's `ini.h`. The syntax rules are in the [configuration files reference](https://github.com/RobCo-Industries/RobCo-Core/blob/245590ed9c7f3db92ea98c4d66e01d5e7ca98500/docs/guide/api/buses.md#configuration-files-inih). Section and key names are case-sensitive. The file may hold at most 4096 bytes.

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
| `display` | `char_delay_ms` | 0 to 100 | `0` | Pause after each character of the boot text, in milliseconds. See [BOOT.md](BOOT.md#boot-text). |
| `display` | `backend` | `hdmi`, `tft`, `both`, any case | `hdmi` | Where frames go, see [SPI panel](#spi-panel) |
| `audio` | `volume` | 0 to 100 | `100` | Audio volume in percent. It is the master level: every sound follows it. |
| `audio` | `radio` | 0 to 100 | `100` | Level of the radio sounds a ROM marks as such, see [Sound levels](#sound-levels) |
| `audio` | `sfx` | 0 to 100 | `100` | Level of the effects: every sound Pip-OS plays itself and every sound of a ROM |
| `audio` | `recordings` | 0 to 100 | `100` | Level of the recordings a ROM marks as such |
| `audio` | `games` | 0 to 100 | `100` | Level of every sound a holotape plays |
| `holotape` | `slot` | `on`, `off`, any case | `off` | The holotape slot of ABI 1.4. `off` touches no slot pin. See [API.md](API.md#the-holotape-slot) and [Slot and panel](#slot-and-panel). |
| `audio` | `beep` | `yes`, `no`, `true`, `false`, `on`, `off`, `1`, `0`, any case | `yes` | Beep once at power-on |
| `audio` | `system_sound` | A boolean, as `beep` | `no` | The system sound. At power-on the kernel moves the audio rest level to the midpoint with the library's ramp, then plays the soft beep instead of the square beep if `beep` is on. Card work and typed boot lines then make disk clicks. See [BOOT.md](BOOT.md#the-system-sound) and [the disk click](BOOT.md#the-disk-click). |
| `audio` | `key_click` | A boolean, as `beep` | `no` | The old name of `system_sound`, still read. When both are in the file, a valid `system_sound` line wins, in either order. |

Numbers are unsigned decimal. A `+` sign is accepted.

For a real device we suggest `char_delay_ms = 20`, `line_delay_ms = 200` and `system_sound = yes`. The boot text then takes about 10 seconds. Every character costs `char_delay_ms`, spaces included, and each line but the first waits `line_delay_ms`. The standard boot text has 15 lines and about 363 characters, so 7.3 s of characters and 2.8 s between lines. Typing makes no sound of its own and waits for none. With the system sound on, each typed line adds disk clicks.

`char_delay_ms` and `system_sound` are off by default, so a card without them boots as before. The `ev=config` summary keeps the field name `key_click=` and prints the resolved value of `system_sound` there.

## Sound levels

Each sound has a kind, and each kind has a level. The four keys `radio`, `sfx`, `recordings` and `games` set them. `volume` stays the master and scales every kind.

- The power-on beep, the boot note, the line blip and the disk clicks are `sfx`.
- A ROM's sounds are `sfx` in this version. A ROM cannot mark a sound as radio or a recording yet, so `radio` and `recordings` change nothing today.
- A holotape's sounds are `games`. When the holotape ends, the ROM's sounds are `sfx` again.

A level is squared, the master is not. So `sfx = 50` plays the effects at a quarter of their height, 12 dB lower. The gain of a sound is `volume * level * level / 10000` percent, rounded. At 100 a level changes nothing, so a card without these keys sounds as before.

A sound keeps the level of its kind from its start to its end. A `volume` change moves the sound that plays to its new height within 20 ms. The rest ramp of the system sound has no level.

## Stored levels

The device keeps its levels in `/PIPBOY/PIPOS.SET` on the boot card. Nobody needs to write this file by hand. A later version of Pip-OS writes it when a ROM asks. This version only reads it.

```ini
[audio]
volume = 70
radio = 45
sfx = 100
recordings = 60
games = 55
```

- It holds one section, `[audio]`, and up to five keys: `volume`, `radio`, `sfx`, `recordings` and `games`. Each is a whole number from 0 to 100 in decimal digits. Leading zeros are fine. Comments and blank lines are fine.
- The kernel reads it right after `PIPOS.CFG` and before its first sound. The values come in this order: the built-in 100 for each, then `PIPOS.CFG`, then `PIPOS.SET`. A key in `PIPOS.SET` wins. A key it lacks keeps the value of `PIPOS.CFG`.
- The `ev=config` summary shows the values of `PIPOS.CFG` alone. The gain on the next `ev=audio` start line shows the stored levels.
- Delete the file to get the values of `PIPOS.CFG` back at the next boot.
- A missing file is not an error and prints nothing.

The file counts only as a whole. When it is not valid, the kernel keeps every value of `PIPOS.CFG`, even the keys that were good, and prints one line. The boot goes on.

```
@RC stage=pipos ev=levels result=bad reason=<reason>
```

| `reason` | Cause |
|---|---|
| `read` | A card error while reading the file |
| `size` | The file is larger than 512 bytes |
| `syntax` | A line that is not INI |
| `key` | Another section, another key, a key twice, or a key before `[audio]`. Names match with case, as in `PIPOS.CFG`. |
| `value` | A value that is not a whole number from 0 to 100 |

When several apply, the line names the first one in the table.



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

## Slot and panel

The holotape slot drives GPIO 7, 12, 13, 20 and 21. GPIO 7 is its card's chip select and SPI0's CE1 pin. It also runs SPI0 with chip select 1. With `backend` set to `tft` or `both`, the kernel refuses the slot in these cases:

| Key | Clash |
|---|---|
| `tft_cs` | `1`: the panel on CE1, the slot's chip select |
| `tft_dc` | 12, 13, 20 or 21 |
| `tft_reset` | 12, 13, 20 or 21 |
| `tft_backlight` | 12, 13, 20 or 21 |

The slot then stays off, `PIP_HW_TAPE` stays clear, and the panel keeps its settings. The kernel prints `ev=config rc=slot_clash key=display.<key> line=0` once per clashing key, and the boot goes on. With `hdmi`, the panel keys are not used and nothing clashes. The buttons, the dial, audio, I2C and the PL011 use no slot pin.

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
| `[holotape] slot = on` with a panel setting that clashes, see [Slot and panel](#slot-and-panel) | The slot stays off | `ev=config rc=slot_clash key=display.<key> line=0` |
| Two panel lines on one pin | The backlight goes to `none`. A clash with D/C turns the panel off: `backend` goes to `hdmi`. | `ev=config rc=pin_twice key=display.<key> line=0` |

`ev=display` counts the refused `backend` and `tft_*` values in `bad`, see [BOOT.md](BOOT.md#the-display-backend).

The summary marker:

```
@RC stage=pipos ev=config rc=ok path=/PIPBOY/PIPOS.CFG line=0 scheme=amber line_delay_ms=150 volume=80 beep=1 char_delay_ms=0 key_click=0 radio=100 sfx=100 recordings=100 games=100
```

The last four fields are the four sound levels.

On a syntax error, keys before the bad line may already have printed `unknown` or `badvalue` markers. None of them applies.
