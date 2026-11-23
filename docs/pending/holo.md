# Holotapes

The rules come from the spec's [Holotapes](https://github.com/RobCo-Industries/RobCo-Core/blob/06316cd5179009832fc164921bd04bf5016950a1/docs/spec/PIPOS_ABI.md#holotapes) and [Sandbox](https://github.com/RobCo-Industries/RobCo-Core/blob/06316cd5179009832fc164921bd04bf5016950a1/docs/spec/PIPOS_ABI.md#sandbox) sections. This page lists what Pip-OS does on top of them.

| Number | Name | Arguments | Result | Errors |
|---|---|---|---|---|
| `0x60` | `holo_enumerate` | `pip_holo_info_t *out, uint32 max` | Entries written | `EINVAL`, `EFAULT`, `EIO` |
| `0x61` | `holo_launch` | `const char *name, const char *args` | The holotape's exit status, 0 to 255 | `EPERM`, `EINVAL`, `EFAULT`, `ENOENT`, `ENOEXEC`, `EIO` |
| `0x62` | `holo_last_status` | none | What the last `holo_launch` returned | `ENOENT` before the first launch |
| `0x63` | `holo_eject_event` | none | 0 | none |

## Listing

- `holo_enumerate` reads `/PIPBOY/HOLO` in directory order. A missing folder gives 0 entries.
- `max` is 1 to 32. Other values return `-PIP_EINVAL`. Pip-OS checks the whole `max` entries of `out` before it reads the card, so a bad pointer returns `-PIP_EFAULT` and writes nothing.
- A file is listed when its 8.3 name is `NAME.HTP` and its header passes the magic, version and size checks. The CRC is not checked, so a damaged holotape is listed and fails at launch.
- A card error while Pip-OS reads the folder or a header returns `-PIP_EIO`. The entries in `out` are then undefined. A file too short to hold a header is skipped, like any file that is not a holotape.
- `file` is the 8.3 name. A file with a long name is listed under its 8.3 alias, such as `LONGNA~1.HTP`.
- `name`, `title` and `icon` are copied from the header and the META block as they are.

## Launch

- Only the ROM can launch. A holotape gets `-PIP_EPERM`, and `holo_last_status` keeps its value.
- `name` is a plain 8.3 name: 1 to 8 characters from `A-Z`, `0-9` and ``! # $ % & ' ( ) - @ ^ _ ` { } ~``, then `.HTP`. Lower case works and is upper-cased. A folder, a drive prefix, `.`, `..`, a wildcard, a space or a longer name returns `-PIP_EINVAL`.
- `args` must end with a NUL within 100 bytes, so 0 to 99 characters. A longer string returns `-PIP_EINVAL`. A `NULL` pointer returns `-PIP_EFAULT`.
- Pip-OS copies both strings in before it checks anything else.
- The name must be the one `holo_enumerate` lists. Pip-OS finds the entry the card would open for that name, by long name or alias. If that entry is a folder or a file listed under another name, the launch returns `-PIP_ENOENT`.
- A missing file returns `-PIP_ENOENT`. A card error while Pip-OS loads the file returns `-PIP_EIO`. Any other failed load step returns `-PIP_ENOEXEC`, a file shorter than 0x60 bytes included. A data holotape, subtype 2, fails the bounds step.
- The holotape runs inside the ROM's call. `exit` and a return from `pip_main` end the holotape only. A fault or a kill ends it with `-PIP_EFAULT`, and the ROM goes on.

## While a holotape runs

- Every call sees the holotape as the caller: kind 1, its own memory for pointer checks and its own heap in `mem_info`.
- Input pointers may point into the ROM slot, which the holotape can read. Output pointers must point into the holotape's own slot or heap.
- The holotape has its own 8 file handles. The ROM's handles stay open but do not work for the holotape.
- Before the holotape starts, Pip-OS stops the sound, empties the event queue, resets the drawing state and clears the back buffer. When it ends, Pip-OS closes the holotape's files, stops the sound, empties the queue and gives the ROM its drawing state back. The back buffer keeps what the holotape drew.
- The volume a holotape sets stays after it ends.

## Serial markers and script commands

- `ev=holo_start name=<file>` before the jump, `ev=holo_exit status=<n>` after an exit or a return.
- `ev=holo_kill reason=<r> pc=<hex>` after a fault, with `dfar`/`dfsr` or `ifar`/`ifsr` like `ev=rom_kill`. A kill prints `reason=killed code=<n>`. A holotape fault is not shown on the screen.
- `ev=holo_error code=<step> name=<file>` when the load fails.
- The `state` script command adds `holo=1` while a holotape runs. `files=` counts the ROM's and the holotape's open files.
- The `kill` script command ends the innermost running image: the holotape if one runs, else the ROM.
