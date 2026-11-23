# Saves and files

The rules come from the spec's [Save files and Files](https://github.com/RobCo-Industries/RobCo-Core/blob/78c9d9e91c78292ccad668b9593602e4c9f467ff/docs/spec/PIPOS_ABI.md#save-files) sections. This page lists what Pip-OS does on top of them.

| Number | Name | Arguments | Result | Errors |
|---|---|---|---|---|
| `0x50` | `read_save` | `const char *name, void *buf, uint32 size` | Bytes copied: the save's size, or `size` if smaller | `EINVAL`, `EFAULT`, `ENOENT`, `ERANGE`, `EIO` |
| `0x51` | `write_save` | `const char *name, const void *buf, uint32 size` | `size` | `EINVAL`, `EFAULT`, `ERANGE`, `ENOSPC`, `EIO` |
| `0x52` | `save_info` | `const char *name, uint32 *size` | 0. Stores the save's size. | `EINVAL`, `EFAULT`, `ENOENT`, `ERANGE`, `EIO` |
| `0x53` | `save_delete` | `const char *name` | 0 | `EINVAL`, `EFAULT`, `ENOENT`, `EIO` |
| `0x54` | `file_open` | `const char *path` | Handle 0 to 7 | `EINVAL`, `EFAULT`, `EPERM`, `ENOENT`, `ENOMEM`, `EIO` |
| `0x55` | `file_read` | `int32 handle, void *buf, uint32 size` | Bytes read, 0 at the end | `EBADF`, `EFAULT`, `EIO` |
| `0x56` | `file_close` | `int32 handle` | 0 | `EBADF` |
| `0x57` | `dir_list` | `const char *path, pip_dirent_t *out, uint32 max, uint32 start` | Entries written, 0 past the end | `EINVAL`, `EFAULT`, `EPERM`, `ENOENT`, `EIO` |

## Saves

- A save is `/PIPBOY/SAVE/<NAME>.SAV`. `NAME` is 1 to 8 characters from `A-Z`, `0-9` and `_`. Lower case works and is upper-cased. The name must end with a NUL within 9 bytes.
- `write_save` takes 0 to 65536 bytes. A larger `size` returns `-PIP_ERANGE`. Pip-OS copies the data first, creates `/PIPBOY/SAVE` if it is missing, then writes with RobCo-Core's atomic write.
- `read_save`, `save_info` and `save_delete` first recover from the work files `~NAME.SAV` and `#NAME.SAV` that a power cut can leave. See the [recovery table](https://github.com/RobCo-Industries/RobCo-Core/blob/78c9d9e91c78292ccad668b9593602e4c9f467ff/docs/guide/api/storage.md#whole-files).
- `read_save` checks the whole `size` bytes of `buf` before it reads.
- A file over 64 KiB is not a valid save, even when a PC put it on the card. `read_save` and `save_info` return `-PIP_ERANGE` for it and write nothing; `save_delete` removes it and `write_save` replaces it.
- `save_delete` removes the save and both work files.
- `size` 0 needs no buffer.

## Files

File calls are read only.

- A path is absolute and at most 127 bytes plus the NUL. Each component is an 8.3 name: 1 to 8 characters, then an optional dot and 1 to 3 characters. The characters are `A-Z`, `0-9` and ``! # $ % & ' ( ) - @ ^ _ ` { } ~``. Lower case works and is upper-cased.
- These return `-PIP_EINVAL`: a relative path, `//`, `.` or `..`, a trailing `/`, a trailing dot, a space anywhere, `\`, `:`, a byte outside the set above. Only `dir_list` accepts `/` alone.
- `/PIPBOY/SAVE` and everything in it return `-PIP_EPERM`, in any case.
- FatFs also finds a folder by its 8.3 alias, and a PC tool can give `PIPBOY` or `SAVE` an alias such as `SAVE~1`. So Pip-OS also checks the card. It looks up the first component in the root by long or short name. If that entry is the `PIPBOY` folder by either name, it looks up the second component in it the same way. If that entry is the `SAVE` folder by either name, the call returns `-PIP_EPERM`.
- That check costs one scan of the root for every path with two or more components, and one more scan of `/PIPBOY` when the first component is that folder. Each scan stops at the matching entry. A path with one component costs no scan.
- Each ROM has 8 handles, 0 to 7. `file_open` takes the lowest free one. A directory returns `-PIP_EINVAL`. Pip-OS closes every open file when the ROM ends.
- `file_read` takes any `size`. It checks the whole buffer first, then reads through a 4 KiB kernel buffer. `size` 0 returns 0. A refused read does not move the file position.
- `dir_list` takes `max` 1 to 64. It checks the whole `out` range for `max` entries first. It skips `start` entries, then writes entries in directory order. `name` is the 8.3 name, never the long name. An entry whose long name does not fit 8.3 shows its alias, such as `LONGFO~1`, and `file_open` or `dir_list` accepts that name. The save folder shows under the name that returns `-PIP_EPERM`. A file path returns `-PIP_EINVAL`, a missing one `-PIP_ENOENT`. Work files and hidden entries are listed too.

## Errors

A missing card, a card that failed or any file system error returns `-PIP_EIO`. A full card returns `-PIP_ENOSPC`. On a missing card each call can take about 3 seconds before it fails: that is the SD driver's time-out.

## Read-only card

- `read_save` and `save_info` change nothing and read whichever file holds the save, by the recovery table.
- `save_delete` returns `-PIP_EIO` and changes nothing. The save stays readable.
- `write_save` returns `-PIP_EIO`.

## Choices where the spec is silent

- A file over 64 KiB in the save folder is not a valid save. `save_info` returns `-PIP_ERANGE` for it, like `read_save`.
- A delete that cannot write, on a read-only card, returns `-PIP_EIO`.

## Limits

- Pip-OS keeps a 64 KiB static buffer for saves and file reads, 8 file handles of 640 bytes each, and a 1280-byte buffer for `dir_list`.
- `file_read` runs the kernel's main-loop work between chunks, like `sleep_ms`, so script commands keep working during long reads.
- In QEMU a 64 KiB `write_save` takes 20 to 120 ms, depending on the board.

## Maintenance

The `state` script command adds `files=<n>`: the open file handles of the running ROM. It is 0 after the ROM ends.
