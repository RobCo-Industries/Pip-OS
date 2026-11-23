/*
 * Drawing on a 0x00RRGGBB surface, and the copy of a surface to a
 * framebuffer. Pure code: the host tests draw into an ordinary array.
 * sys_gfx.c checks the caller's pointers and calls these functions.
 */
#ifndef PIPOS_GFX_DRAW_H
#define PIPOS_GFX_DRAW_H

#include <stdint.h>

#include <pipos/abi.h>

#define GFX_W   PIP_SCREEN_W
#define GFX_H   PIP_SCREEN_H

/* 16 rows, bit 7 is the left pixel: font8x16_glyph() on the target. */
typedef const uint8_t *(*gfx_glyph_fn)(uint32_t ch);

/* What set_color, set_clip and set_font change. Clip ends are exclusive. */
typedef struct gfx_state {
    uint32_t fg, bg;                /* bg may be PIP_COLOR_NONE */
    int32_t cx0, cy0, cx1, cy1;
    uint32_t font;
} gfx_state_t;

typedef struct gfx_surface {
    uint32_t *px;                   /* w * h pixels, row after row */
    int32_t w, h;
    gfx_glyph_fn glyph;
    gfx_state_t st;
} gfx_surface_t;

void gfx_surface_init(gfx_surface_t *s, uint32_t *px, int32_t w, int32_t h, gfx_glyph_fn glyph);
/* fg, a transparent background, the whole surface as the clip, font 0. */
void gfx_state_reset(gfx_surface_t *s, uint32_t fg);

/*
 * Image hooks. begin: a holotape saves the ROM's state in *saved; then
 * the state is reset to fg and the surface cleared to black. end: a
 * holotape gets the ROM's state back. end returns 1 when the kernel takes
 * the screen back (a ROM ended), else 0.
 */
void gfx_image_begin(gfx_surface_t *s, gfx_state_t *saved, int kind, uint32_t fg);
int gfx_image_end(gfx_surface_t *s, const gfx_state_t *saved, int kind);

/*
 * Who owns the display. Each function returns what to show now:
 * GFX_SHOW_NONE leaves the display alone.
 */
enum { GFX_SHOW_NONE, GFX_SHOW_BLACK, GFX_SHOW_BACK, GFX_SHOW_CONSOLE };

typedef struct gfx_screen {
    int depth;      /* images running */
    int shown;      /* the ROM called present since it started */
    int blanked;
} gfx_screen_t;

void gfx_screen_start(gfx_screen_t *sc, int kind);
/* GFX_SHOW_CONSOLE when a ROM ended on a lit display. */
int gfx_screen_end(gfx_screen_t *sc, int kind);
/* GFX_SHOW_BACK, or GFX_SHOW_NONE while blanked. */
int gfx_screen_present(gfx_screen_t *sc);
/* On: black. Off: the presented frame of a running image, else the console. */
int gfx_screen_blank(gfx_screen_t *sc, int on);

/* The calls. Each returns what the system call returns. */
void gfx_set_color(gfx_surface_t *s, uint32_t fg, uint32_t bg);
int32_t gfx_set_clip(gfx_surface_t *s, int32_t x, int32_t y, uint32_t wh);
int32_t gfx_set_font(gfx_surface_t *s, uint32_t font);
void gfx_point(gfx_surface_t *s, int32_t x, int32_t y, uint32_t color);
void gfx_clear(gfx_surface_t *s, uint32_t color);
void gfx_line(gfx_surface_t *s, int32_t x0, int32_t y0, int32_t x1, int32_t y1);
int32_t gfx_rect(gfx_surface_t *s, int32_t x, int32_t y, uint32_t wh, uint32_t fill);
/* Checks count and closed only; xy holds count pairs. */
int32_t gfx_polyline_check(uint32_t count, uint32_t closed);
void gfx_polyline(gfx_surface_t *s, const int16_t *xy, uint32_t count, uint32_t closed);
int32_t gfx_arc(gfx_surface_t *s, const pip_arc_t *a);

/* 0 and the byte count from data to the end of the last row, or -PIP_EINVAL. */
int32_t gfx_blit_check(const pip_blit_t *b, int rgb, uint32_t *len);
void gfx_blit_1bpp(gfx_surface_t *s, const pip_blit_t *b, const uint8_t *data);
void gfx_blit_rgb(gfx_surface_t *s, const pip_blit_t *b, const uint8_t *data);

/* (width << 16) | height for len bytes, or -PIP_EINVAL for a bad scale. */
int32_t gfx_text_metrics(uint32_t len, uint32_t scale);
/* Draws len bytes; returns the width in pixels. scale must be valid. */
int32_t gfx_text(gfx_surface_t *s, int32_t x, int32_t y, const char *text, uint32_t len,
                 uint32_t scale);

/* The whole factor by which a w x h surface fits a dw x dh screen, 0 if it does not. */
uint32_t gfx_scale_factor(uint32_t w, uint32_t h, uint32_t dw, uint32_t dh);
/*
 * Copies src (w x h) to dst, scaled by the largest whole factor and
 * centred, the border black. swap_rb puts red in the low byte, as the
 * framebuffer's RGB order wants. Every word gets 0xFF in the top byte.
 * Returns the factor, or 0 and writes nothing.
 */
uint32_t gfx_scale_copy(const uint32_t *src, uint32_t w, uint32_t h, uint8_t *dst, uint32_t dw,
                        uint32_t dh, uint32_t pitch, int swap_rb);

/* A rectangle of the surface, in pixels. */
typedef struct gfx_rect {
    uint32_t x, y, w, h;
} gfx_rect_t;

/*
 * The smallest rectangle that holds every pixel where cur and last
 * differ, both w x h with no padding. Returns 1 and fills *r, or 0 when
 * the two are the same.
 */
int gfx_dirty_rect(const uint32_t *cur, const uint32_t *last, uint32_t w, uint32_t h,
                   gfx_rect_t *r);

/* Copies rectangle r of src into dst, both w pixels wide. */
void gfx_copy_rect(const uint32_t *src, uint32_t *dst, uint32_t w, const gfx_rect_t *r);

#endif
