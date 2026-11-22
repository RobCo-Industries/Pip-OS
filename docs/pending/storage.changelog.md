### Added

- Saves: `read_save`, `write_save`, `save_info` and `save_delete` keep up to 64 KiB per save in `/PIPBOY/SAVE` with RobCo-Core's atomic write, and recover after a power cut.
- Files: `file_open`, `file_read` and `file_close` read files from the card, with 8 handles per ROM that close when the ROM ends.
- `dir_list` lists a folder in pages of up to 64 entries with 8.3 names.
- Strict 8.3 paths: no spelling of a path reaches the save folder, which returns `-PIP_EPERM`.
- The `state` script command reports `files=<n>`, the open file handles.
