# Lore versus real

Pip-OS borrows the look of the wrist computer from the Fallout games. This page lists each borrowed item that has a counterpart in the code, and what the code does.

| Fiction | Pip-OS | Where |
|---|---|---|
| A boot screen with a version, a copyright line, a loader and an exec version | Fixed text. `V7.1.0.8`, `LOADER V1.1` and `EXEC VERSION 41.10` are not computed. The kernel version is 7.1.0, in `PIPOS_VERSION_*`. | `pipos_boot_head` in `kernel/bootlines.c`, `kernel/robco_config.h` |
| `64K RAM SYSTEM` | Fixed text. The Pip-OS region is 4 MiB, `0x00000000`-`0x003FFFFF`. 4 MiB divided by 64 gives 64 KiB, the rule RobCo-Core's memory map uses for lore numbers. | `kernel/bootlines.c`, [MEMORY_MAP.md](https://github.com/RobCo-Industries/RobCo-Core/blob/245590ed9c7f3db92ea98c4d66e01d5e7ca98500/docs/spec/MEMORY_MAP.md#lore-vs-real) |
| `38911 BYTES FREE` | Fixed text with no real counterpart. The `state` script command reports the real free kernel heap. | `kernel/bootlines.c`, `script_command()` in `kernel/main.c` |
| A ROM slot the system loads at start-up | One file, `/PIPBOY/ROM1.ROM`, checked and loaded into the 4 MiB slot at `0x00400000`. The boot screen shows `LOAD ROM(1): <NAME> <version>` from its header. | `kernel/card.c`, `kernel/bootlines.c` |
| Holotapes, small programs on tape cartridges | `.HTP` files in `/PIPBOY/HOLO/` on the SD card. The ROM lists them and launches one at a time into the 4 MiB slot at `0x00800000`. Ejecting is not modelled: `holo_eject_event` always returns 0. | `kernel/sys_holo.c` |
| The tuning dial | A rotary encoder on GPIO 23 and 24 with a push button on GPIO 16. A ROM reads its position in degrees, 15 per detent. | `kernel/robco_config.h`, `kernel/sys_input.c` |
| A small monochrome screen | A 320 by 240 drawing surface, scaled by 2 onto a 640 by 480 HDMI framebuffer. The boot console has four colour schemes: green, amber, blue and white. | `kernel/sys_gfx.c`, `kernel/settings.c` |
| Sensors and a battery gauge | Only the clock answers, from an RTC module when one is fitted. The other six sensors and the battery report nothing. | `kernel/sys_time.c` |
| A save on the device | `/PIPBOY/SAVE/<NAME>.SAV` files of at most 64 KiB. | `kernel/sys_storage.c` |

RobCo-Core's MEMORY_MAP.md says the boot screen shows real addresses divided by 64. The current boot screen shows no address: only the fixed lines above.

## What is original

The boot lines in `kernel/bootlines.c` reproduce the wording of the boot screen in the games, as a reference to them. The code that prints them is original. The kernel, the SDK, the example ROM and the sample holotape are also original work for this project. Names from the games, such as Pip-Boy, Pip-OS, RobCo and holotape, are used as references only. The project uses no art, logos, music or dialogue from the games.
