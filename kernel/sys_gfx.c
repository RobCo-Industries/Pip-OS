/*
 * The graphics group: system calls 0x01-0x0F. The handlers check the
 * arguments and the caller's pointers, then call the pure drawing code in
 * gfx_draw.c on the back buffer. present copies the back buffer to the
 * framebuffer. See docs/pending/gfx.md.
 */
#include <robco/alloc.h>
#include <robco/console.h>
#include <robco/fb.h>
#include <robco/font.h>
#include <robco/klibc.h>
#include <robco/mmu.h>

#include "gfx_draw.h"
#include "pipos.h"
#include "sys.h"

/* Kernel-only RAM from the Pip-Boy memory map, "Back buffers". */
#define GFX_BB_BASE  0x00C00000u
#define GFX_BB_BYTES ((uint32_t)GFX_W * GFX_H * 4u)

static gfx_surface_t surf;
static gfx_state_t rom_state;   /* the ROM's state while a holotape runs */
static gfx_screen_t screen;

static uint32_t fg_default(void)
{
    const console_scheme_t *sc = console_scheme_info(console_scheme());

    return (uint32_t)sc->normal.r << 16 | (uint32_t)sc->normal.g << 8 | sc->normal.b;
}

/* Copies the back buffer to the framebuffer: 0, or -PIP_ENODEV. */
static int32_t show_back_buffer(void)
{
    const fb_info_t *fb = fb_get();

    if (fb == NULL) {
        return -PIP_ENODEV;
    }
    if (gfx_scale_copy(surf.px, GFX_W, GFX_H, fb->addr, fb->width, fb->height, fb->pitch,
                       fb->order == FB_ORDER_RGB) == 0) {
        return -PIP_ENODEV;
    }
    return 0;
}

/* The console's full redraw: background, then every cell. */
static void show_console(void)
{
    console_set_scheme(console_scheme());
}

static void show(int what)
{
    switch (what) {
    case GFX_SHOW_BLACK:
        if (fb_get() != NULL) {
            fb_clear(fb_rgb(0, 0, 0));
        }
        break;
    case GFX_SHOW_BACK:
        (void)show_back_buffer();
        break;
    case GFX_SHOW_CONSOLE:
        show_console();
        break;
    default:
        break;
    }
}

void pipos_gfx_blank(int on)
{
    show(gfx_screen_blank(&screen, on));
}

static int32_t sys_draw_line(const pipos_sys_ctx_t *ctx, uint32_t a0, uint32_t a1, uint32_t a2,
                             uint32_t a3)
{
    (void)ctx;
    gfx_line(&surf, (int32_t)a0, (int32_t)a1, (int32_t)a2, (int32_t)a3);
    return 0;
}

static int32_t sys_draw_point(const pipos_sys_ctx_t *ctx, uint32_t x, uint32_t y,
                              uint32_t color, uint32_t a3)
{
    (void)ctx, (void)a3;
    gfx_point(&surf, (int32_t)x, (int32_t)y, color);
    return 0;
}

/* Scale first, then the string: a bad scale is -PIP_EINVAL whatever the pointer. */
static int32_t text_in(const pipos_sys_ctx_t *ctx, uint32_t ptr, uint32_t scale, char *buf)
{
    long len;

    if (gfx_text_metrics(0, scale) < 0) {
        return -PIP_EINVAL;
    }
    len = pipos_user_str(ctx, buf, ptr, PIP_TEXT_MAX + 1);
    return (int32_t)len;
}

static int32_t sys_draw_text(const pipos_sys_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t text,
                             uint32_t scale)
{
    char buf[PIP_TEXT_MAX + 1];
    int32_t len = text_in(ctx, text, scale, buf);

    if (len < 0) {
        return len;
    }
    return gfx_text(&surf, (int32_t)x, (int32_t)y, buf, (uint32_t)len, scale);
}

static int32_t sys_clear(const pipos_sys_ctx_t *ctx, uint32_t color, uint32_t a1, uint32_t a2,
                         uint32_t a3)
{
    (void)ctx, (void)a1, (void)a2, (void)a3;
    gfx_clear(&surf, color);
    return 0;
}

static int32_t sys_gfx_info(const pipos_sys_ctx_t *ctx, uint32_t out, uint32_t a1, uint32_t a2,
                            uint32_t a3)
{
    pip_gfx_info_t info;

    (void)a1, (void)a2, (void)a3;
    info.width = GFX_W;
    info.height = GFX_H;
    info.format = PIP_GFX_FORMAT_XRGB;
    info.backend = PIP_GFX_BACKEND_HDMI;
    info.fg_default = fg_default();
    return pipos_user_copy_out(ctx, out, &info, sizeof(info));
}

static int32_t sys_rect(const pipos_sys_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t wh,
                        uint32_t fill)
{
    (void)ctx;
    return gfx_rect(&surf, (int32_t)x, (int32_t)y, wh, fill);
}

static int32_t sys_polyline(const pipos_sys_ctx_t *ctx, uint32_t xy, uint32_t count,
                            uint32_t closed, uint32_t a3)
{
    int16_t pts[2 * PIP_POLY_MAX];
    int32_t rc = gfx_polyline_check(count, closed);

    (void)a3;
    if (rc == 0) {
        rc = pipos_user_copy_in(ctx, pts, xy, count * 2u * sizeof(int16_t));
    }
    if (rc == 0) {
        gfx_polyline(&surf, pts, count, closed);
    }
    return rc;
}

static int32_t sys_arc(const pipos_sys_ctx_t *ctx, uint32_t ptr, uint32_t a1, uint32_t a2,
                       uint32_t a3)
{
    pip_arc_t a;
    int32_t rc = pipos_user_copy_in(ctx, &a, ptr, sizeof(a));

    (void)a1, (void)a2, (void)a3;
    return rc != 0 ? rc : gfx_arc(&surf, &a);
}

/*
 * The descriptor is copied in. The pixel data is read in place once its
 * whole range, data to the end of the last row, is checked.
 */
static int32_t blit(const pipos_sys_ctx_t *ctx, uint32_t ptr, int rgb)
{
    pip_blit_t b;
    uint32_t len;
    int32_t rc = pipos_user_copy_in(ctx, &b, ptr, sizeof(b));

    if (rc == 0) {
        rc = gfx_blit_check(&b, rgb, &len);
    }
    if (rc == 0) {
        rc = pipos_user_check_in(ctx, b.data, len);
    }
    if (rc != 0) {
        return rc;
    }
    if (rgb) {
        gfx_blit_rgb(&surf, &b, (const uint8_t *)(uintptr_t)b.data);
    } else {
        gfx_blit_1bpp(&surf, &b, (const uint8_t *)(uintptr_t)b.data);
    }
    return 0;
}

static int32_t sys_blit_1bpp(const pipos_sys_ctx_t *ctx, uint32_t ptr, uint32_t a1, uint32_t a2,
                             uint32_t a3)
{
    (void)a1, (void)a2, (void)a3;
    return blit(ctx, ptr, 0);
}

static int32_t sys_blit_rgb(const pipos_sys_ctx_t *ctx, uint32_t ptr, uint32_t a1, uint32_t a2,
                            uint32_t a3)
{
    (void)a1, (void)a2, (void)a3;
    return blit(ctx, ptr, 1);
}

static int32_t sys_set_clip(const pipos_sys_ctx_t *ctx, uint32_t x, uint32_t y, uint32_t wh,
                            uint32_t a3)
{
    (void)ctx, (void)a3;
    return gfx_set_clip(&surf, (int32_t)x, (int32_t)y, wh);
}

static int32_t sys_set_color(const pipos_sys_ctx_t *ctx, uint32_t fg, uint32_t bg, uint32_t a2,
                             uint32_t a3)
{
    (void)ctx, (void)a2, (void)a3;
    gfx_set_color(&surf, fg, bg);
    return 0;
}

static int32_t sys_text_metrics(const pipos_sys_ctx_t *ctx, uint32_t text, uint32_t scale,
                                uint32_t a2, uint32_t a3)
{
    char buf[PIP_TEXT_MAX + 1];
    int32_t len = text_in(ctx, text, scale, buf);

    (void)a2, (void)a3;
    return len < 0 ? len : gfx_text_metrics((uint32_t)len, scale);
}

static int32_t sys_present(const pipos_sys_ctx_t *ctx, uint32_t a0, uint32_t a1, uint32_t a2,
                           uint32_t a3)
{
    const fb_info_t *fb = fb_get();

    (void)ctx, (void)a0, (void)a1, (void)a2, (void)a3;
    if (fb == NULL || gfx_scale_factor(GFX_W, GFX_H, fb->width, fb->height) == 0) {
        return -PIP_ENODEV;
    }
    return gfx_screen_present(&screen) == GFX_SHOW_BACK ? show_back_buffer() : 0;
}

static int32_t sys_set_font(const pipos_sys_ctx_t *ctx, uint32_t font, uint32_t a1, uint32_t a2,
                            uint32_t a3)
{
    (void)ctx, (void)a1, (void)a2, (void)a3;
    return gfx_set_font(&surf, font);
}

static const pipos_sys_entry_t gfx_calls[] = {
    PIPOS_SYS_ENTRY(GFX, DRAW_LINE, sys_draw_line),
    PIPOS_SYS_ENTRY(GFX, DRAW_POINT, sys_draw_point),
    PIPOS_SYS_ENTRY(GFX, DRAW_TEXT, sys_draw_text),
    PIPOS_SYS_ENTRY(GFX, CLEAR, sys_clear),
    PIPOS_SYS_ENTRY(GFX, GFX_INFO, sys_gfx_info),
    PIPOS_SYS_ENTRY(GFX, RECT, sys_rect),
    PIPOS_SYS_ENTRY(GFX, POLYLINE, sys_polyline),
    PIPOS_SYS_ENTRY(GFX, ARC, sys_arc),
    PIPOS_SYS_ENTRY(GFX, BLIT_1BPP, sys_blit_1bpp),
    PIPOS_SYS_ENTRY(GFX, BLIT_RGB, sys_blit_rgb),
    PIPOS_SYS_ENTRY(GFX, SET_CLIP, sys_set_clip),
    PIPOS_SYS_ENTRY(GFX, SET_COLOR, sys_set_color),
    PIPOS_SYS_ENTRY(GFX, TEXT_METRICS, sys_text_metrics),
    PIPOS_SYS_ENTRY(GFX, PRESENT, sys_present),
    PIPOS_SYS_ENTRY(GFX, SET_FONT, sys_set_font),
};

const pipos_sys_table_t pipos_gfx_table = PIPOS_SYS_TABLE(GFX, "gfx", gfx_calls);

/*
 * The back buffer must be kernel-only RAM outside the kernel image and
 * heap: the heap ends where the Pip-OS slot ends, and the MMU gives USR
 * mode no access to any of its sections.
 */
void pipos_gfx_init(void)
{
    void *heap = NULL;
    uint32_t heap_end = (uint32_t)robco_heap_slot(&heap);
    uint32_t a;
    int user = 0;

    heap_end += (uint32_t)(uintptr_t)heap;
    for (a = GFX_BB_BASE; a < GFX_BB_BASE + GFX_BB_BYTES; a += MMU_SECTION_SIZE) {
        user |= mmu_section_user(a);
    }
    user |= mmu_section_user(GFX_BB_BASE + GFX_BB_BYTES - 1);
    k_printf("@RC stage=" PIPOS_STAGE " ev=gfx bb=0x%08x size=%u user=%d heap_end=0x%08x\n",
             (unsigned)GFX_BB_BASE, (unsigned)GFX_BB_BYTES, user, (unsigned)heap_end);
    if (user != 0 || heap_end > GFX_BB_BASE) {
        pipos_panic("back buffer not kernel-only");
    }
    gfx_surface_init(&surf, (uint32_t *)(uintptr_t)GFX_BB_BASE, GFX_W, GFX_H, font8x16_glyph);
    gfx_state_reset(&surf, fg_default());
    gfx_clear(&surf, 0);
}

void pipos_gfx_image_start(int kind)
{
    gfx_image_begin(&surf, &rom_state, kind, fg_default());
    gfx_screen_start(&screen, kind);
}

void pipos_gfx_image_end(int kind)
{
    (void)gfx_image_end(&surf, &rom_state, kind);
    show(gfx_screen_end(&screen, kind));
}

void pipos_gfx_poll(void)
{
}
