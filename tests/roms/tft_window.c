/*
 * Frames for tests/test_tft.py at tft_rotation = 0, where the panel shows
 * only the left 240 columns and the top 240 rows. Before each present the
 * ROM prints "ev=step n=<n>", after it "ev=shown", then waits for a key.
 *
 *   0 first   a scene that crosses the edge of the window
 *   1 hidden  a change right of the window only
 *   2 pixel   one pixel inside the window
 *
 * Then it blanks the screen with power, prints "ev=dark" and sleeps, so
 * the test can force a panic on a blanked screen.
 */
#include "rt.h"

static int32_t steps;

static void step(void)
{
    pip_event_t ev;

    rt_begin("step");
    rt_dec("n", steps++);
    rt_end();
    pip_present();
    rt_begin("shown");
    rt_end();
    while (pip_wait_event(&ev, 60000) != 1 || ev.value != 1) {
    }
}

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    pip_clear(0x203040);
    pip_set_color(0xff8000, PIP_COLOR_NONE);
    pip_rect_xywh(200, 40, 80, 60, PIP_RECT_FILLED);
    pip_set_color(0x10e0f0, PIP_COLOR_NONE);
    pip_draw_line(0, 0, 319, 239);
    step();
    pip_set_color(0x00ff00, PIP_COLOR_NONE);
    pip_rect_xywh(250, 120, 60, 100, PIP_RECT_FILLED);
    step();
    pip_draw_point(10, 20, 0xff00ff);
    step();
    rt_check_eq("off", pip_power(PIP_POWER_SCREEN_OFF), 0);
    rt_checks_done();
    rt_begin("dark");
    rt_end();
    for (;;) {
        pip_sleep_ms(1000);
    }
}
