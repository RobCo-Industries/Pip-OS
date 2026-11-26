# User manual

This page is for a person with a Raspberry Pi who wants to run Pip-OS. It does not need kernel knowledge.

Pip-OS has run on one real board so far: commit `2d10c2c` on a Pi Zero W 1.1, with serial keys as the controls. The screen, GPIO buttons, the dial, sound and the RTC chip have not been tried on a board. See [Hardware results](TESTING.md#hardware-results).

## What you need

- A Raspberry Pi Zero, Zero W, Pi 1, Pi 2 or Pi 3. The Pi 4 is not supported.
- A micro SD card, or a full-size one for the Pi 1.
- An HDMI display. Pip-OS draws at 640 by 480.
- Optional: a 320 by 240 ILI9341 SPI panel instead of HDMI, or next to it. See [Choosing the display](#choosing-the-display).
- Optional: six buttons and a rotary dial with a push button, on GPIO pins.
- Optional: a speaker or amplifier on the PWM audio output.
- Optional: an RTC module on I2C, address `0x68`, to keep the time.
- Optional: a 3.3 V USB serial adapter on GPIO 14 and 15, to see the boot log.

[HARDWARE.md](HARDWARE.md) covers the parts. The pins the kernel uses are in `kernel/robco_config.h`.

| Control | GPIO (BCM) |
|---|---|
| UP, DOWN | 5, 6 |
| LEFT, RIGHT | 17, 22 |
| SELECT, BACK | 26, 27 |
| Dial A, B, push | 23, 24, 16 |

Each button connects its pin to ground. [BUILD.md](BUILD.md#button-pins) shows how to build with other pins.

## Get a card

There is no released card image yet. You build one with the tools in [BUILD.md](BUILD.md#tools).

```sh
git clone --recurse-submodules https://github.com/RobCo-Industries/Pip-OS.git
cd Pip-OS
make bootdir DIR=card
```

`card/` then holds the Pi firmware, `PIPOS.IMG`, `config.txt`, `PIPBOY/PIPOS.CFG`, the example ROM as `PIPBOY/ROM1.ROM` and the example holotape `PIPBOY/HOLO/BEACON.HTP`. Format the card as FAT32 and copy everything in `card/` to it. On a card with several FAT partitions, Pip-OS uses the first one that holds `PIPBOY/PIPOS.CFG` or `PIPBOY/ROM1.ROM`. For another ROM, see [Your own ROM and holotapes](#your-own-rom-and-holotapes), or pass `ROM=<file>` to `make bootdir`. Run it again into the same folder with other settings and it may stop and list old files. Use a new folder, or see [Running bootdir again](BUILD.md#running-bootdir-again).

## Try it in QEMU

No hardware needed:

```sh
make sdimage
make run BOARD=raspi2b
```

`BOARD` is `raspi0`, `raspi1ap`, `raspi2b` or `raspi3b`. `WINDOW=1` also opens the display window. Type in the terminal that runs `make run`, not in the window. Press Ctrl-A, then X, to quit.

## What you see at boot

The screen shows these lines, in green unless the configuration picks another colour:

```
*************** PIP-OS(R) V7.1.0.8 ***************
COPYRIGHT 2075 ROBCO IND.
**************************************************

LOADER V1.1
EXEC VERSION 41.10
64K RAM SYSTEM
38911 BYTES FREE
1 HOLOTAPE FOUND
LOAD ROM(1): HELLO 1.0.0

**************************************************
PIP-OS KERNEL INITIALIZED
PIP-OS READY
**************************************************
```

The first eight lines never change. [LORE.md](LORE.md) explains them. The speaker beeps once unless the configuration turns the beep off.

The holotape line counts the `.HTP` files in `PIPBOY/HOLO/`:

| Line | Meaning |
|---|---|
| `NO HOLOTAPE FOUND` | No `.HTP` file, or no card |
| `1 HOLOTAPE FOUND`, `<n> HOLOTAPES FOUND` | That many `.HTP` files |

The ROM line says whether `PIPBOY/ROM1.ROM` passed its checks:

| Line | Meaning |
|---|---|
| `LOAD ROM(1): <NAME> <version>` | The ROM is good. It starts after the last line. |
| `ROM(1) NOT LOADED: NO SD CARD` | No card, or no volume on it mounts as FAT |
| `ROM(1) NOT LOADED: NO FILE /PIPBOY/ROM1.ROM` | The file is missing |
| `ROM(1) NOT LOADED: READ ERROR` | The card failed during the read, or the file is too short |
| `ROM(1) NOT LOADED: NOT A ROM IMAGE` | The file is not a ROM, for example a holotape |
| `ROM(1) NOT LOADED: UNKNOWN HEADER VERSION` | The ROM was built for a newer image format |
| `ROM(1) NOT LOADED: FILE SIZE MISMATCH` | The file is cut short or has extra bytes |
| `ROM(1) NOT LOADED: DOES NOT FIT THE ROM SLOT` | The ROM is larger than 4 MiB |
| `ROM(1) NOT LOADED: WRONG ABI VERSION` | The ROM was built for another Pip-OS ABI |
| `ROM(1) NOT LOADED: CHECKSUM MISMATCH` | The file is damaged |

When the ROM starts, it owns the screen. When it ends, the boot text comes back. If the ROM crashes, the screen shows a line such as `ROM(1) DATA ABORT AT 0x00400120`. The ROM runs once per boot. To run it again, restart the Pi.

## Controls

| Control | Key in the serial console |
|---|---|
| UP, DOWN, LEFT, RIGHT | Arrow keys |
| SELECT | Enter |
| BACK | Backspace, or Escape alone |
| Dial | None |

The dial reports 15 degrees per detent. A ROM reads the controls only while it runs. BACK reaches the ROM like the other buttons. The example ROM ends on LEFT or BACK.

## The example ROM and holotape

The default card runs the SDK's hello example. It prints system and memory information on the serial console, then lists the holotapes on the card with their titles and icons.

| Key | Effect |
|---|---|
| UP, DOWN or the dial | Pick a holotape |
| SELECT | Run it |
| LEFT | End the ROM |

The sample holotape, `BEACON.HTP`, shows its title, draws rings and plays a short tune. UP and DOWN change its exit status, SELECT leaves. Back in the ROM, the screen shows `Last run: exit status <n>`.

## Your own ROM and holotapes

| File | Place | Rule |
|---|---|---|
| The ROM | `PIPBOY/ROM1.ROM` | Only this name. Pip-OS loads one ROM. |
| Holotapes | `PIPBOY/HOLO/<NAME>.HTP` | 1 to 8 characters, then `.HTP`. Up to 32 are listed. |
| Configuration | `PIPBOY/PIPOS.CFG` | Optional |

ROMs and holotapes are `rimg` images built with the SDK. [ROM_DEVELOPMENT.md](ROM_DEVELOPMENT.md) explains how to build one. `make sdimage ROM=<file> TAPE=<file>` puts yours on the QEMU card.

## Make a tape card

Since ABI 1.4 a Pip-Boy with a holotape slot can run tapes from a second card. The slot is off by default: add this to `PIPBOY/PIPOS.CFG` on the boot card.

```ini
[holotape]
slot = on
```

To make a tape card:

1. Format a card as FAT, with one partition or none.
2. Copy each tape to the root folder as `<NAME>.HTP`, 1 to 8 characters, then `.HTP`. Folders, hidden files and other names are ignored.
3. Insert it. A slow card takes longer before its tapes show. The Pip-Boy keeps running meanwhile. The ROM lists its tapes first. A tape on the tape card hides a tape of the same name on the boot card until you pull the card.

Pip-OS never writes to the tape card. Saves still go to the boot card. A card that cannot be read is marked bad until you pull it. Pulling the card while one of its tapes runs ends that tape.

## Configuration

`PIPBOY/PIPOS.CFG` is an INI file. These are the common keys. [CONFIG.md](CONFIG.md) has the full table and the error rules.

```
[display]
scheme = amber
line_delay_ms = 150

[audio]
volume = 80
beep = yes
```

| Key | Values | Default |
|---|---|---|
| `scheme` in `[display]` | `green`, `amber`, `blue`, `white` | `green` |
| `line_delay_ms` in `[display]` | 0 to 2000, pause between boot lines | `0` |
| `volume` in `[audio]` | 0 to 100 | `100` |
| `beep` in `[audio]` | `yes` or `no` | `yes` |
| `char_delay_ms` in `[display]` | 0 to 100, pause after each boot text character | `0` |
| `system_sound` in `[audio]` | `yes` or `no`, the system sound: a soft beep at power-on. `key_click` is its old name. | `no` |

A bad value keeps the default. A line that is not INI makes Pip-OS ignore the whole file.

## Choosing the display

`backend` in `[display]` picks where the picture goes:

| `backend` | What you get |
|---|---|
| `hdmi` | HDMI only. The default. |
| `both` | The same picture on HDMI and on the SPI panel |
| `tft` | The SPI panel. HDMI shows only the boot text. |

```
[display]
backend = tft
tft_rotation = 270
```

Wire the panel as in [HARDWARE.md](HARDWARE.md#spi-panel). If the picture is upside down, try `tft_rotation = 270` or `90`. If red and blue are swapped, set `tft_bgr = no`. If the colours look inverted, set `tft_invert = yes`. [CONFIG.md](CONFIG.md#spi-panel) lists every panel key.

The panel shows the last 15 boot lines, 40 characters wide, and the panic text. If the panel fails to start, Pip-OS uses HDMI and shows `SPI DISPLAY FAILED, USING HDMI`. If it starts but never answers, Pip-OS sends the picture to both, so HDMI still works. The panel has not been tried on real hardware.

## Saves

A ROM's saves live in `PIPBOY/SAVE/` as `<NAME>.SAV` files of at most 64 KiB. To back them up, copy that folder from the card to a computer. Files whose names start with `~` or `#` are work files from a save in progress. Leave them in place: Pip-OS uses them to finish or undo an interrupted save.

## Clock

With an RTC module that holds a valid time, Pip-OS starts its clock from it at boot. Without one, the clock is unset until the ROM sets it, and the ROM gets no time. A time the ROM sets is written to the RTC module too, so it survives a reboot. Without a module, it is lost at power-off. The module holds years 2000 to 2099 only.

## Sound

Pip-OS plays one sound at a time: tones, short tunes and short recorded clips. `volume` in the configuration sets the start level, and a ROM can change it. On a real board, sound goes out through the PWM output. In QEMU there is no sound: the serial log shows each tone instead.

## Turning it off

A ROM can halt the system. The screen then shows `PIP-OS HALTED. SAFE TO POWER OFF.` Pip-OS has no shutdown key of its own. Avoid pulling the power while a ROM writes a save.

## Troubleshooting

| What you see | Likely cause | What to do |
|---|---|---|
| Nothing on the screen | Firmware, `PIPOS.IMG` or `config.txt` missing from the card | Check the card has every file from `make bootdir` |
| `KERNEL PANIC: <reason>` and `SYSTEM HALTED` | The kernel could not start the MMU, the display, the heap or the tick | Note the reason and report it |
| `ROM(1) NOT LOADED: NO SD CARD` | No card, or no volume mounts as FAT | Reformat the card as FAT32 |
| `ROM(1) NOT LOADED: NO FILE /PIPBOY/ROM1.ROM` | The ROM is missing or misnamed | Copy the ROM as `PIPBOY/ROM1.ROM` |
| `ROM(1) NOT LOADED: CHECKSUM MISMATCH` or `FILE SIZE MISMATCH` | The copy is damaged | Copy the ROM again |
| `ROM(1) NOT LOADED: WRONG ABI VERSION` | The ROM targets another Pip-OS | Rebuild it with this SDK |
| `NO HOLOTAPE FOUND` with tapes on the card | Wrong folder or extension | Put them in `PIPBOY/HOLO/` with the `.HTP` extension |
| A holotape is missing from the ROM's list | The name is longer than 8 characters, or the file is not a valid holotape | Rename it to 8.3 form and rebuild it |
| `ROM(1) <FAULT> AT 0x<address>` | The ROM crashed | Report it to the ROM's author |
| Panel stays dark | Wiring, or `backend` left at `hdmi` | Check the wiring in [HARDWARE.md](HARDWARE.md#spi-panel). The serial log has an `ev=display` line when `backend` asks for the panel. |
| Colours or delay not applied | A bad value or a syntax error in `PIPOS.CFG` | Check the file against [CONFIG.md](CONFIG.md). The serial log names the bad line. |
| The time is unset or wrong | No RTC module, or its time is not valid | Fit an RTC module and set it |
| The buttons do nothing | Wrong pins, or the button does not connect the pin to ground | Check the pins above |

To check that your Pi runs every system call right, run the conformance ROM. [HARDWARE_VALIDATION.md](HARDWARE_VALIDATION.md) gives the steps.

The serial console shows the full boot log. Each `@RC stage=pipos` line is one step. [BOOT.md](BOOT.md#markers) lists them.

## Report a problem

Open an issue on the [Pip-OS repository](https://github.com/RobCo-Industries/Pip-OS/issues). Include the board, the screen text and, if you can, the serial log.
