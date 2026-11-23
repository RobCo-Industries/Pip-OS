# Audio calls

The rules are in the [ABI spec](../../external/robco-core/docs/spec/PIPOS_ABI.md#audio). This page lists what Pip-OS does on top of them.

| Number | Name | Arguments | Result | Errors |
|---|---|---|---|---|
| `0x20` | `tone` | `uint32 freq_hz, duration_ms` | 0 | `-PIP_EINVAL` for `freq_hz` outside 20 to 20000 or `duration_ms` outside 1 to 60000 |
| `0x21` | `sample` | `const uint8 *data`, `uint32 length` | 0 | `-PIP_EINVAL` for `length` outside 1 to 65536, `-PIP_EFAULT`, `-PIP_EIO` if the audio driver refuses |
| `0x22` | `audio_stop` | none | 0, also when nothing plays | none |
| `0x23` | `volume` | `uint32 level` | The previous level | `-PIP_EINVAL` for `level` above 100 |
| `0x24` | `audio_status` | none | 1 while a sound the ROM started plays, else 0 | none |
| `0x25` | `play_seq` | `const pip_note_t *notes`, `uint32 count` | 0 | `-PIP_EINVAL` for `count` outside 1 to 256 or any bad note, `-PIP_EFAULT` |

`-PIP_EINVAL` is -22, `-PIP_EFAULT` is -14 and `-PIP_EIO` is -5.

## Playback

There is one voice. `tone`, `sample` and `play_seq` return at once and the sound plays in the background. A new sound replaces the one that plays.

`play_seq` checks every note before anything plays. A note is a rest (`freq_hz` 0) or 20 to 20000 Hz, and lasts 1 to 60000 ms. One bad note refuses the whole sequence. Pip-OS copies the notes before it returns, so the ROM can reuse the array at once.

`audio_status` only reports sounds the ROM started. The power-on beep does not count.

When a ROM starts and when it ends, Pip-OS stops any sound the ROM started. A ROM that exits during a long tone leaves the speaker quiet. The power-on beep is not cut.

## Volume

The level starts at the `volume` setting in `PIPOS.CFG`, 100 without one. See [CONFIG.md](../CONFIG.md). The level is 0 to 100 percent of the square wave's duty cycle. At 0 the speaker is silent, but tones still play and `audio_status` still reports them.

The level belongs to the device, not to the ROM. A ROM that changes it changes it until the next power-on. A ROM that wants its own level sets it and restores the old one before it exits.

A new level applies from the next note.

## Samples

`sample` plays 8-bit unsigned mono PCM at 8000 Hz. 128 is silence. A sample holds 1 to 65536 bytes, so it lasts at most 8.192 seconds.

Pip-OS checks the length, then the pointer. On an error nothing plays and the current sound goes on. Otherwise Pip-OS stops the current sound, copies the data into its own buffer and starts the sample. The ROM can reuse its buffer as soon as the call returns.

A sample replaces a tone, a sequence or another sample, and a tone or a sequence replaces a sample. `audio_stop`, `audio_status`, `volume` and the stop at ROM start and end work as for tones. The volume scales each sample around 128 and applies from the next sample.

Samples are not yet tried on real hardware.

## Boards without PWM

QEMU has no PWM model. There Pip-OS uses RobCo-Core's trace backend: each change of sound prints one line on the UART instead of playing it.

```
@RC stage=pipos ev=audio op=tone hz=1050 vol=40 t_ms=408
@RC stage=pipos ev=audio op=stop hz=0 vol=40 t_ms=508
```

A sample prints two lines. `crc` is the CRC-32 of the copied data. `op=pcm_end` marks the natural end. A stop or a replacement prints `op=stop` instead.

```
@RC stage=pipos ev=audio op=pcm rate=8000 len=800 vol=70 crc=eb779ef4 t_ms=179
@RC stage=pipos ev=audio op=pcm_end n=78 last=795 t_ms=280
```

`t_ms` is the audio driver's millisecond clock. A rest in a sequence prints `op=stop`. QEMU shows when a sample starts and ends and which data it plays. It does not show the sound. On some hosts QEMU delivers far fewer timer interrupts than samples, so `n` can be much smaller than `len`.
