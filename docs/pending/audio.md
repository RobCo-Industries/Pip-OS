# Audio calls

The rules are in the [ABI spec](../../external/robco-core/docs/spec/PIPOS_ABI.md#audio). This page lists what Pip-OS does on top of them.

| Number | Name | Arguments | Result | Errors |
|---|---|---|---|---|
| `0x20` | `tone` | `uint32 freq_hz, duration_ms` | 0 | `-PIP_EINVAL` for `freq_hz` outside 20 to 20000 or `duration_ms` outside 1 to 60000 |
| `0x21` | `sample` | `const uint8 *data`, `uint32 length` | none | Always `-PIP_ENOSYS`: not served yet |
| `0x22` | `audio_stop` | none | 0, also when nothing plays | none |
| `0x23` | `volume` | `uint32 level` | The previous level | `-PIP_EINVAL` for `level` above 100 |
| `0x24` | `audio_status` | none | 1 while a sound the ROM started plays, else 0 | none |
| `0x25` | `play_seq` | `const pip_note_t *notes`, `uint32 count` | 0 | `-PIP_EINVAL` for `count` outside 1 to 256 or any bad note, `-PIP_EFAULT` |

`-PIP_EINVAL` is -22, `-PIP_EFAULT` is -14 and `-PIP_ENOSYS` is -38.

## Playback

There is one voice. `tone` and `play_seq` return at once and the sound plays in the background. A new sound replaces the one that plays.

`play_seq` checks every note before anything plays. A note is a rest (`freq_hz` 0) or 20 to 20000 Hz, and lasts 1 to 60000 ms. One bad note refuses the whole sequence. Pip-OS copies the notes before it returns, so the ROM can reuse the array at once.

`audio_status` only reports sounds the ROM started. The power-on beep does not count.

When a ROM starts and when it ends, Pip-OS stops any sound the ROM started. A ROM that exits during a long tone leaves the speaker quiet. The power-on beep is not cut.

## Volume

The level starts at the `volume` setting in `PIPOS.CFG`, 100 without one. See [CONFIG.md](../CONFIG.md). The level is 0 to 100 percent of the square wave's duty cycle. At 0 the speaker is silent, but tones still play and `audio_status` still reports them.

The level belongs to the device, not to the ROM. A ROM that changes it changes it until the next power-on. A ROM that wants its own level sets it and restores the old one before it exits.

A new level applies from the next note.

## Samples

`sample` is not served and returns `-PIP_ENOSYS`. RobCo-Core plays square waves only: it has no way to play PCM data. Pip-OS does not fake samples with tones.

## Boards without PWM

QEMU has no PWM model. There Pip-OS uses RobCo-Core's trace backend: each change of sound prints one line on the UART instead of playing it.

```
@RC stage=pipos ev=audio op=tone hz=1050 vol=40 t_ms=408
@RC stage=pipos ev=audio op=stop hz=0 vol=40 t_ms=508
```

`t_ms` is the audio driver's millisecond clock. A rest in a sequence prints `op=stop`.
