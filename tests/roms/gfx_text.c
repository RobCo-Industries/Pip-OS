/* Screen golden: text at scales 1 to 8, opaque and transparent, unprintable bytes, clipped. */
#include "rt.h"

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    pip_clear(0x000000);
    pip_draw_text(0, 0, "Default colour, scale 1: !\"#$%&'()*+,-./0123456789", 1);
    pip_set_color(0xffb000, 0x202020);
    pip_draw_text(0, 16, "Opaque ABC xyz", 2);
    pip_set_color(0x5ab4ff, PIP_COLOR_NONE);
    pip_draw_text(0, 48, "Bad: \x01\x7f\x80\xff end", 1);
    pip_draw_text(-12, 64, "Clipped left", 3);
    pip_draw_text(250, 64, "right", 3);
    pip_set_color(0xffffff, 0x800000);
    pip_draw_text(4, 112, "S8", 8);
    pip_set_clip_xywh(150, 120, 100, 40);
    pip_set_color(0x33e066, 0x003000);
    pip_draw_text(140, 110, "CLIP", 4);
    pip_set_clip(0, 0, 0);
    pip_set_color(0xd8d8dc, PIP_COLOR_NONE);
    pip_draw_text(150, 224, "bottom edge", 1);
    pip_present();
    rt_begin("drawn");
    rt_end();
    for (;;) {
        pip_sleep_ms(1000);
    }
}
