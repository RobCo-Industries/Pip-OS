### Added

- The holotape calls `holo_enumerate`, `holo_launch`, `holo_last_status` and `holo_eject_event` (`0x60` to `0x63`).
- `holo_launch` runs a holotape from `/PIPBOY/HOLO` inside the ROM's call, in the holotape sandbox. A fault ends the holotape only.
- A holotape gets its own file handles, event queue, sound and drawing state. The ROM gets its own back when the holotape ends.
- The `kill` script command ends the running holotape, or the ROM when none runs. `state` shows `holo=`.

### Fixed

- After a nested image, the ROM keeps its open files and its input events. `mem_info` reports the caller's own heap.
