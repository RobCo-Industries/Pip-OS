/* Screen golden: lines in every octant, rects, polylines, arcs, points, a clip. */
#include "rt.h"

int pip_main(const pipos_startup_t *startup)
{
    static const int16_t zig[] = { 200, 20, 220, 60, 240, 20, 260, 60, 280, 20 };
    static const int16_t tri[] = { 200, 100, 300, 100, 250, 180 };
    pip_arc_t a;
    int i;

    (void)startup;
    pip_clear(0x101828);
    /* A star of 16 lines from one centre: every octant. */
    pip_set_color(0xffff00, PIP_COLOR_NONE);
    for (i = 0; i < 16; i++) {
        static const int8_t d[16][2] = {
            { 40, 0 }, { 40, 15 }, { 40, 40 }, { 15, 40 }, { 0, 40 }, { -15, 40 },
            { -40, 40 }, { -40, 15 }, { -40, 0 }, { -40, -15 }, { -40, -40 }, { -15, -40 },
            { 0, -40 }, { 15, -40 }, { 40, -40 }, { 40, -15 },
        };
        pip_draw_line(60, 60, 60 + d[i][0], 60 + d[i][1]);
    }
    pip_draw_line(130, 20, 130, 20);
    pip_set_color(0xff4040, PIP_COLOR_NONE);
    pip_rect_xywh(110, 30, 30, 20, PIP_RECT_OUTLINE);
    pip_rect_xywh(145, 30, 30, 20, PIP_RECT_FILLED);
    pip_rect_xywh(110, 55, 1, 1, PIP_RECT_OUTLINE);
    pip_rect_xywh(115, 55, 2, 9, PIP_RECT_OUTLINE);
    pip_set_color(0x40ff40, PIP_COLOR_NONE);
    pip_polyline(zig, 5, 0);
    pip_polyline(tri, 3, 1);
    pip_set_color(0x40c0ff, PIP_COLOR_NONE);
    a = (pip_arc_t){ 60, 170, 50, 0, 360, PIP_ARC_OUTLINE };
    pip_arc(&a);
    a = (pip_arc_t){ 60, 170, 40, 300, 60, PIP_ARC_FILLED };
    pip_arc(&a);
    a = (pip_arc_t){ 60, 170, 30, 90, 180, PIP_ARC_OUTLINE };
    pip_arc(&a);
    a = (pip_arc_t){ 150, 150, 1, 0, 360, PIP_ARC_OUTLINE };
    pip_arc(&a);
    a = (pip_arc_t){ 319, 239, 512, 180, 270, PIP_ARC_OUTLINE };
    pip_arc(&a);
    for (i = 0; i < 32; i++) {
        pip_draw_point(120 + 2 * i, 100, 0x010101u * (uint32_t)(i * 8));
    }
    /* Clipped: only the part inside 110..170 x 110..140 shows. */
    pip_set_clip_xywh(110, 110, 60, 30);
    pip_set_color(0xff80ff, PIP_COLOR_NONE);
    pip_rect_xywh(100, 100, 100, 100, PIP_RECT_FILLED);
    pip_set_color(0x000000, PIP_COLOR_NONE);
    pip_draw_line(0, 0, 319, 239);
    pip_set_clip(0, 0, 0);
    pip_present();
    rt_begin("drawn");
    rt_end();
    for (;;) {
        pip_sleep_ms(1000);
    }
}
