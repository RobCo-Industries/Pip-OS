### Added

- The graphics calls `0x01` to `0x0F`: lines, points, text, rectangles, polylines, arcs, 1-bit and RGB blits, clip, colours, font and `present`.
- A 320x240 back buffer in kernel-only memory at `0x00C00000`. `present` scales it by 2 onto the 640x480 framebuffer.
- The console redraws the boot screen when a ROM ends, so the ROM's last frame never stays on screen.
- `pipos_gfx_blank()` for the screen off and on power calls.
