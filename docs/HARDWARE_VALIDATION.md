# Hardware validation

Nothing in Pip-OS has been validated on a real Raspberry Pi yet. The tests run in QEMU only. This page tells you how to run the conformance ROM on a real board and what to send back. The ROM itself is described in [TESTING.md](TESTING.md#conformance-rom).

## What you need

- A Raspberry Pi Zero, 1, 2 or 3, and a micro SD card.
- An HDMI screen.
- Buttons and a rotary dial wired to the pins in `kernel/robco_config.h`, if you have them. Without them, the prompts time out and their checks show SKIP.
- A 3.3 V USB serial adapter, if you want the serial log. Connect its RX to GPIO 14 (pin 8), its TX to GPIO 15 (pin 10) and GND to pin 6. The port runs at 115200 baud, 8N1.

## Write the card

1. Format the card as FAT32 and mount it.
2. Build and copy the boot files and the conformance images:

```sh
make conformance
make bootdir DIR=/path/to/card
cp build/conformance/rom/CONFORM.ROM /path/to/card/PIPBOY/ROM1.ROM
cp build/conformance/tape/CONFTAPE.HTP build/conformance/CONFDATA.HTP /path/to/card/PIPBOY/HOLO/
```

`make bootdir` downloads the firmware. `build/conformance.img` from `make conformance-card` has no firmware, so it boots only in QEMU.

For a run with no prompts, also write a file `PIPBOY/CONFORM.CFG` that holds the word `auto`.

## Run it

1. If you use the serial adapter, open a terminal on it at 115200 baud and start logging to a file.
2. Put the card in the Pi and power it on.
3. The automatic checks run first. The screen shows which call runs. This takes a few seconds.
4. Answer the prompts. Each one waits 30 s, then moves on and marks its check SKIP.
   - "Press and hold UP" and the other buttons: press that button and keep it down until the screen says "Now let go", then let go. Other buttons are ignored. A tap that ends too soon shows "Too quick" and asks again.
   - "Turn the dial clockwise" and "counter-clockwise": turn it a few clicks that way.
   - "Did you hear it?" after a tone, a tune and a buzz: SELECT for yes, LEFT for no.
   - "Is the picture right?": SELECT for yes, LEFT for no. You should see a frame around the screen, a circle on the left, a filled square in the middle, a triangle on the right, and a line under them.
   - The serial terminal works as buttons too: arrow keys for UP, DOWN, LEFT and RIGHT, Enter for SELECT. It has no dial. Its keys cannot be held, so the hold checks of the buttons stay SKIP with it.
5. Read the results pages. DOWN turns the page, UP goes back, SELECT jumps to the summary.
6. The summary shows four counts. It stays for 10 minutes or until you press a button.

## What the results mean

| Result | Means |
|---|---|
| PASS | The call answered as the spec says |
| FAIL | It did not. The row under it shows the value the call returned and the one expected. |
| SKIP | Nobody answered the prompt in time, or the run was in auto mode |
| ABSENT | The board lacks that hardware and the call said so correctly, for example a sensor or a battery gauge. This is not a failure. |

A sound check answered "no" is a FAIL. A FAIL on a real board is what we want to hear about.

## What to send back

- A photo of the summary screen.
- A photo of each results page that shows a FAIL.
- The serial log, if you captured it. Every check is one line that starts with `@RC stage=conform ev=check`.
- The board model and which buttons, dial and screen you used.
