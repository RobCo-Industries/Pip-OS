/*
 * The graphics calls: results and error codes, every limit at its edge,
 * bad pointers. Then the time present takes, as one "ev=present" marker.
 * tests/test_gfx.py lists the check names in order.
 */
#include "rt.h"

#define SLOT_END    (PIP_ROM_BASE + PIP_ROM_SIZE)
#define KERNEL_ADDR 0x00008000u
#define BACK_BUFFER 0x00C00000u
#define PRESENTS    200

#define CK(name, expr, expected) rt_check_eq(name, (int32_t)(expr), (int32_t)(expected))

static char text255[PIP_TEXT_MAX + 1];
static char text256[PIP_TEXT_MAX + 2];
static int16_t pts[2 * PIP_POLY_MAX + 2];
static uint8_t bits[PIP_BLIT_H_MAX];

static void *at(uint32_t addr)
{
    return (void *)(uintptr_t)addr;
}

static void info(void)
{
    pip_gfx_info_t gi;
    volatile uint8_t *top = (volatile uint8_t *)at(PIP_ROM_STACK_TOP - 8);
    uint8_t keep[8];
    int i, intact = 1;
    int32_t rc;

    CK("info_rc", pip_gfx_info(&gi), 0);
    CK("info_w", gi.width, 320);
    CK("info_h", gi.height, 240);
    CK("info_format", gi.format, PIP_GFX_FORMAT_XRGB);
    CK("info_backend", gi.backend, PIP_GFX_BACKEND_HDMI);
    CK("info_fg", gi.fg_default, 0x33e066);
    CK("info_kernel", pip_gfx_info(at(KERNEL_ADDR)), -PIP_EFAULT);
    /* 16 bytes into the last 8 of the stack region. */
    for (i = 0; i < 8; i++) {
        keep[i] = top[i];
        top[i] = 0x5a;
    }
    rc = pip_gfx_info((pip_gfx_info_t *)(uintptr_t)top);
    for (i = 0; i < 8; i++) {
        intact &= top[i] == 0x5a;
        top[i] = keep[i];
    }
    CK("info_straddle", rc, -PIP_EFAULT);
    CK("info_intact", intact, 1);
}

static void simple(void)
{
    CK("line", pip_draw_line(0, 0, 319, 239), 0);
    CK("line_extreme", pip_draw_line(INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX), 0);
    CK("point", pip_draw_point(5, 5, 0xffffff), 0);
    CK("point_far", pip_draw_point(INT32_MIN, INT32_MAX, 0xffffffffu), 0);
    CK("clear", pip_clear(0), 0);
    CK("color", pip_set_color(0x00ff00, PIP_COLOR_NONE), 0);
    CK("font0", pip_set_font(PIP_FONT_SYSTEM), 0);
    CK("font1", pip_set_font(1), -PIP_EINVAL);
}

static void text(void)
{
    char *end = (char *)at(SLOT_END - 4);
    int i;

    for (i = 0; i < PIP_TEXT_MAX; i++) {
        text255[i] = 'x';
    }
    for (i = 0; i < PIP_TEXT_MAX + 1; i++) {
        text256[i] = 'y';
    }
    for (i = 0; i < 4; i++) {
        end[i] = 'z';
    }
    CK("text_w", pip_draw_text(0, 0, "Hi", 1), 16);
    CK("text_scale8", pip_draw_text(0, 0, "Hi", 8), 128);
    CK("text_empty", pip_draw_text(0, 0, "", 1), 0);
    CK("text_scale0", pip_draw_text(0, 0, "Hi", 0), -PIP_EINVAL);
    CK("text_scale9", pip_draw_text(0, 0, "Hi", 9), -PIP_EINVAL);
    CK("text_255", pip_draw_text(0, 0, text255, 1), 255 * 8);
    CK("text_256", pip_draw_text(0, 0, text256, 1), -PIP_EINVAL);
    CK("text_kernel", pip_draw_text(0, 0, at(KERNEL_ADDR), 1), -PIP_EFAULT);
    CK("text_bb", pip_draw_text(0, 0, at(BACK_BUFFER), 1), -PIP_EFAULT);
    CK("text_slot_end", pip_draw_text(0, 0, end, 1), -PIP_EFAULT);
    CK("text_far", pip_draw_text(INT32_MAX, INT32_MIN, "far", 8), 3 * 64);
    CK("tm_ok", pip_text_metrics("abc", 2), (48 << 16) | 32);
    CK("tm_empty", pip_text_metrics("", 1), 16);
    CK("tm_255", pip_text_metrics(text255, 8), (255 * 64) << 16 | 128);
    CK("tm_scale9", pip_text_metrics("a", 9), -PIP_EINVAL);
    CK("tm_256", pip_text_metrics(text256, 1), -PIP_EINVAL);
    CK("tm_kernel", pip_text_metrics(at(KERNEL_ADDR), 1), -PIP_EFAULT);
}

static void shapes(void)
{
    int16_t *edge = (int16_t *)at(SLOT_END - 8);
    pip_arc_t a = { 160, 120, 50, 0, 360, PIP_ARC_OUTLINE };
    pip_arc_t *arc_end = (pip_arc_t *)at(SLOT_END - 8);
    int i;

    CK("rect_outline", pip_rect_xywh(10, 10, 20, 20, PIP_RECT_OUTLINE), 0);
    CK("rect_fill", pip_rect_xywh(10, 10, 20, 20, PIP_RECT_FILLED), 0);
    CK("rect_zero", pip_rect_xywh(10, 10, 0, 20, PIP_RECT_FILLED), 0);
    CK("rect_fill2", pip_rect_xywh(10, 10, 20, 20, 2), -PIP_EINVAL);
    CK("rect_far", pip_rect(INT32_MAX, INT32_MAX, 0xffffffffu, 1), 0);

    for (i = 0; i < PIP_POLY_MAX + 1; i++) {
        pts[2 * i] = (int16_t)(i * 7 - 300);
        pts[2 * i + 1] = (int16_t)(i & 1 ? INT16_MIN : INT16_MAX);
    }
    for (i = 0; i < 4; i++) {
        edge[i] = (int16_t)(i * 10);
    }
    CK("poly_2", pip_polyline(pts, 2, 0), 0);
    CK("poly_256", pip_polyline(pts, 256, 1), 0);
    CK("poly_1", pip_polyline(pts, 1, 0), -PIP_EINVAL);
    CK("poly_257", pip_polyline(pts, 257, 0), -PIP_EINVAL);
    CK("poly_closed2", pip_polyline(pts, 2, 2), -PIP_EINVAL);
    CK("poly_kernel", pip_polyline(at(KERNEL_ADDR), 2, 0), -PIP_EFAULT);
    CK("poly_edge", pip_polyline(edge, 2, 1), 0);
    CK("poly_straddle", pip_polyline(edge, 3, 0), -PIP_EFAULT);

    CK("arc_ok", pip_arc(&a), 0);
    a.radius = 512;
    a.fill = PIP_ARC_FILLED;
    a.start_deg = 359;
    a.end_deg = 0;
    CK("arc_edges", pip_arc(&a), 0);
    a.radius = 0;
    CK("arc_r0", pip_arc(&a), -PIP_EINVAL);
    a.radius = 513;
    CK("arc_r513", pip_arc(&a), -PIP_EINVAL);
    a.radius = 1;
    a.start_deg = 360;
    CK("arc_start360", pip_arc(&a), -PIP_EINVAL);
    a.start_deg = 0;
    a.end_deg = 361;
    CK("arc_end361", pip_arc(&a), -PIP_EINVAL);
    a.end_deg = 360;
    a.fill = 2;
    CK("arc_fill2", pip_arc(&a), -PIP_EINVAL);
    CK("arc_kernel", pip_arc(at(KERNEL_ADDR)), -PIP_EFAULT);
    CK("arc_straddle", pip_arc(arc_end), -PIP_EFAULT);
}

static pip_blit_t blit(int w, int h, int stride, uint32_t data)
{
    pip_blit_t b = { 0, 0, (uint16_t)w, (uint16_t)h, (uint16_t)stride, 0, data, 0xffffff };

    return b;
}

static void blits(void)
{
    uint32_t big = PIP_ROM_HEAP_BASE;   /* 320 x 240 words of heap */
    pip_blit_t b;

    b = blit(8, PIP_BLIT_H_MAX, 1, (uint32_t)(uintptr_t)bits);
    CK("b1_ok", pip_blit_1bpp(&b), 0);
    b = blit(0, 1, 1, (uint32_t)(uintptr_t)bits);
    CK("b1_w0", pip_blit_1bpp(&b), -PIP_EINVAL);
    b = blit(321, 1, 41, big);
    CK("b1_w321", pip_blit_1bpp(&b), -PIP_EINVAL);
    b = blit(8, 241, 1, big);
    CK("b1_h241", pip_blit_1bpp(&b), -PIP_EINVAL);
    b = blit(9, 1, 1, big);
    CK("b1_stride", pip_blit_1bpp(&b), -PIP_EINVAL);
    b = blit(8, 1, 1, big);
    b.flags = 1;
    CK("b1_flags", pip_blit_1bpp(&b), -PIP_EINVAL);
    /* stride 1, h 100: data to the last row's end is 100 bytes. */
    b = blit(8, 100, 1, SLOT_END - 100);
    CK("b1_edge", pip_blit_1bpp(&b), 0);
    b.h = 101;
    CK("b1_past", pip_blit_1bpp(&b), -PIP_EFAULT);
    b = blit(8, 1, 1, KERNEL_ADDR);
    CK("b1_data_kernel", pip_blit_1bpp(&b), -PIP_EFAULT);
    CK("b1_desc_kernel", pip_blit_1bpp(at(KERNEL_ADDR)), -PIP_EFAULT);

    b = blit(320, 240, 1280, big);
    CK("rgb_full", pip_blit_rgb(&b), 0);
    b = blit(3, 2, 11, big);
    CK("rgb_stride", pip_blit_rgb(&b), -PIP_EINVAL);
    /* stride 16, h 3, w 3: 16 * 2 + 12 = 44 bytes. */
    b = blit(3, 3, 16, SLOT_END - 44);
    CK("rgb_edge", pip_blit_rgb(&b), 0);
    b.data = SLOT_END - 43;
    CK("rgb_past", pip_blit_rgb(&b), -PIP_EFAULT);
    b = blit(1, 1, 4, BACK_BUFFER);
    CK("rgb_data_bb", pip_blit_rgb(&b), -PIP_EFAULT);
}

static void clip(void)
{
    CK("clip_set", pip_set_clip_xywh(10, 10, 100, 50), 0);
    CK("clip_w0", pip_set_clip_xywh(10, 10, 0, 50), -PIP_EINVAL);
    CK("clip_h0", pip_set_clip_xywh(10, 10, 50, 0), -PIP_EINVAL);
    CK("clip_far", pip_set_clip(INT32_MIN, INT32_MAX, 0xffffffffu), 0);
    CK("clip_reset", pip_set_clip(0, 0, 0), 0);
}

static void present(void)
{
    int32_t t0, t1;
    int i, rc = 0;

    t0 = pip_ticks_ms();
    while (pip_ticks_ms() == t0) {
    }
    t0 = pip_ticks_ms();
    for (i = 0; i < PRESENTS; i++) {
        rc |= pip_present();
    }
    t1 = pip_ticks_ms();
    CK("present", rc, 0);
    rt_begin("present");
    rt_dec("count", PRESENTS);
    rt_dec("ms", t1 - t0);
    rt_end();
}

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    info();
    simple();
    text();
    shapes();
    blits();
    clip();
    present();
    rt_checks_done();
    return 0;
}
