# Hardware validation

Nothing in Pip-OS has been validated on a real Raspberry Pi yet. The tests run in QEMU only. This page tells you how to run the conformance ROM on a real board and what to send back. The ROM itself is described in [TESTING.md](TESTING.md#conformance-rom).

## What you need

- A Raspberry Pi Zero, 1, 2 or 3, and a micro SD card.
- An HDMI screen.
- Buttons and a rotary dial wired to the pins in `kernel/robco_config.h`, if you have them. Without them, the prompts time out and their checks show SKIP.
- A 3.3 V USB serial adapter, if you want the serial log. Connect its RX to GPIO 14 (pin 8), its TX to GPIO 15 (pin 10) and GND to pin 6. The port runs at 115200 baud, 8N1.

## Write the card

1. Build the card's files into a new folder, here `card`:

```sh
make conformance-bootdir DIR=card
```

If `card` already holds files from another `bootdir` run, the target may stop and list them. Use a new folder, or see [Running bootdir again](BUILD.md#running-bootdir-again).

2. Format the card as FAT32 and copy everything in `card/` to it.

`card/` then holds the Pi firmware, `PIPOS.IMG`, `config.txt`, `PIPBOY/PIPOS.CFG`, the conformance ROM as `PIPBOY/ROM1.ROM`, and `PIPBOY/HOLO/CONFTAPE.HTP` and `CONFDATA.HTP`. The make target downloads the firmware. `build/conformance.img` from `make conformance-card` has the same files without the firmware, so it boots only in QEMU.

For a run with no prompts, also write a file `PIPBOY/CONFORM.CFG` that holds the word `auto`.

The `set_time` check writes the real-time clock chip. If the chip held a valid time, the ROM puts it back within about a second. If it held none, the chip keeps the test date 2023-11-14: set the time again after the run.

## Run it

1. If you use the serial adapter, open a terminal on it at 115200 baud and start logging to a file.
2. Put the card in the Pi and power it on.
3. The automatic checks run first. The screen shows which call runs. This takes a few seconds.
4. A screen then says what comes next. Press SELECT to start the checks by hand, or LEFT to skip them all. With no answer in 10 minutes, they are all SKIP.
5. Answer the prompts. Each one waits 30 s, then moves on and marks its check SKIP.
   - "Press and hold UP" and the other buttons, BACK last: press that button and keep it down until the screen says "Now let go", then let go. Other buttons are ignored. A tap that ends too soon shows "Too quick" and asks again.
   - "Turn the dial clockwise" and "counter-clockwise": turn it a few clicks that way.
   - "Did you hear it?" for a tone, a tune, a buzz and a buzz in two halves with no click between: the sound starts once the prompt shows. UP plays it again. SELECT for yes, LEFT for no.
   - "Is the picture right?": SELECT for yes, LEFT for no. You should see a frame around the screen, a circle on the left, a filled square in the middle, a triangle on the right, and a line under them.
   - The serial terminal works as buttons too: arrow keys for UP, DOWN, LEFT and RIGHT, Enter for SELECT, Backspace for BACK. It has no dial. Its keys cannot be held, so the hold checks of the buttons stay SKIP with it.
6. Read the results pages. If a check failed, the pages open on the first page with a FAIL. DOWN or RIGHT turns the page, UP or LEFT goes back, SELECT jumps to the summary. A page waits for a button, at most 10 minutes.
7. The summary shows four counts and, after a FAIL, the pages that hold one. UP or LEFT goes back to the last page. DOWN and RIGHT do nothing. SELECT ends the run. With no button pressed, the run ends after 10 minutes.

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
