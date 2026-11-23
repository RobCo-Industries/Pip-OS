### Added

- The audio calls `tone`, `audio_stop`, `volume`, `audio_status` and `play_seq` (`0x20`, `0x22` to `0x25`). One voice plays in the background; a new sound replaces the old one.
- `play_seq` takes up to 256 notes and rests. Pip-OS checks and copies them before it returns.
- The volume starts at the `PIPOS.CFG` setting and stays until the next power-on.
- Pip-OS stops a ROM's sound when the ROM starts and when it ends.
- `sample` (`0x21`) still returns `-PIP_ENOSYS`: RobCo-Core cannot play PCM data yet.
