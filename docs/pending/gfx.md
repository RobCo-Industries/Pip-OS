# Graphics calls

The rules are in the [ABI spec](../../external/robco-core/docs/spec/PIPOS_ABI.md#display). This page lists what Pip-OS does on top of them.

| Number | Name | Arguments | Result | Errors |
|---|---|---|---|---|
| `0x01` | `draw_line` | `int32 x0, y0, x1, y1` | 0 | none |
| `0x02` | `draw_point` | `int32 x, y`, `uint32 color` | 0 | none |
| `0x03` | `draw_text` | `int32 x, y`, `const char *text`, `uint32 scale` | Width in pixels: 8 x `scale` per byte | `-PIP_EINVAL` for `scale` outside 1 to 8 or more than 255 bytes, `-PIP_EFAULT` |
| `0x04` | `clear` | `uint32 color` | 0 | none |
| `0x05` | `gfx_info` | `pip_gfx_info_t *out` | 0 | `-PIP_EFAULT` |
| `0x06` | `rect` | `int32 x, y`, `uint32 wh`, `uint32 fill` | 0 | `-PIP_EINVAL` for `fill` above 1 |
| `0x07` | `polyline` | `const int16 *xy`, `uint32 count`, `uint32 closed` | 0 | `-PIP_EINVAL` for `count` outside 2 to 256 or `closed` above 1, `-PIP_EFAULT` |
| `0x08` | `arc` | `const pip_arc_t *a` | 0 | `-PIP_EINVAL` for `radius` outside 1 to 512, `start_deg` above 359, `end_deg` above 360 or `fill` above 1, `-PIP_EFAULT` |
| `0x09` | `blit_1bpp` | `const pip_blit_t *b` | 0 | `-PIP_EINVAL` for `w` outside 1 to 320, `h` outside 1 to 240, `stride` below one row or `flags` not 0, `-PIP_EFAULT` |
| `0x0A` | `blit_rgb` | `const pip_blit_t *b` | 0 | As `blit_1bpp` |
| `0x0B` | `set_clip` | `int32 x, y`, `uint32 wh` | 0 | `-PIP_EINVAL` when `wh` is not 0 but `w` or `h` is 0 |
| `0x0C` | `set_color` | `uint32 fg, bg` | 0 | none |
| `0x0D` | `text_metrics` | `const char *text`, `uint32 scale` | `(width << 16) \| height` | As `draw_text` |
| `0x0E` | `present` | none | 0 | `-PIP_ENODEV` with no framebuffer, or one smaller than 320x240 |
| `0x0F` | `set_font` | `uint32 font_id` | 0 | `-PIP_EINVAL` for any font but 0 |

`-PIP_EINVAL` is -22, `-PIP_EFAULT` is -14 and `-PIP_ENODEV` is -41.

## Colours

Colours are `0x00RRGGBB`. Pip-OS ignores the top byte of every colour argument. The one exception is a `set_color` background of exactly `PIP_COLOR_NONE` (`0x01000000`), which means transparent.

`gfx_info` reports `fg_default` as the normal text colour of the screen scheme from `PIPOS.CFG`. Green is `0x33E066`.

## Checks

- `draw_text` and `text_metrics` check `scale` before the text pointer.
- `polyline` and `arc` check their limits, then copy the points or the struct in.
- A blit copies its struct in, checks it, then checks the whole pixel range: `stride * (h - 1)` plus one row from `data`. A range that leaves the ROM's regions returns `-PIP_EFAULT` and draws nothing.

## Pixel rules

- Lines: Bresenham. Along the longer axis each step moves one pixel. The other axis moves `round(i * minor / major)` at step `i`, with halves rounded away from the start point. Both end points are drawn. A line from a point to itself draws that point.
- Rectangles: the outline is the first and last row and column. A rectangle 1 or 2 pixels wide or high is filled.
- Arcs: the disk is every pixel with `dx^2 + dy^2 <= r^2 + r`, the pixels whose centre lies within `r + 1/2`. The outline is the disk pixels with at least one of their four neighbours outside the disk. Then a pixel is drawn when its angle lies between the start and end rays, both rays included. The rays come from a table of `round(65536 * sin(d))` for whole degrees, and the test uses exact integer cross products. A filled slice includes the centre.
- Text: bytes outside 0x20 to 0x7E draw as `?`. With a background colour, each glyph cell is filled first. Each font pixel becomes a `scale` x `scale` block.
- `set_clip` cuts the rectangle to the surface. A rectangle entirely outside leaves an empty clip, and nothing draws until the next `set_clip`.

## The display

- The back buffer is 320x240 words at `0x00C00000`, in the kernel-only back-buffer range of the memory map. The boot prints `ev=gfx bb=0x00c00000 size=307200 user=0 heap_end=...` and stops if a ROM could reach that memory or the kernel heap overlaps it.
- `present` scales the back buffer by the largest whole factor that fits the framebuffer and centres it on black. On the 640x480 framebuffer the factor is 2. Each `present` takes about 1.4 ms in QEMU.
- When a ROM starts, the back buffer is cleared to black and the drawing state is reset. The boot screen stays until the ROM's first `present`.
- When the ROM ends, by exit, return or fault, the console redraws the whole boot screen and the kernel prints its exit or fault line below it.
- A holotape gets a fresh drawing state when it starts. The ROM's state comes back when the holotape ends.
- `pipos_gfx_blank(1)` shows black and keeps `present` updating the back buffer only. `pipos_gfx_blank(0)` shows the last presented frame again, or the console when no ROM has presented.
