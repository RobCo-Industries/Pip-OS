/* Draws over the whole surface but never calls present, then sleeps. */
#include "rt.h"

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    pip_clear(0xff0000);
    pip_set_color(0x0000ff, 0xff00ff);
    pip_draw_text(0, 0, "NOT SHOWN", 4);
    rt_begin("drawn");
    rt_end();
    for (;;) {
        pip_sleep_ms(1000);
    }
}
