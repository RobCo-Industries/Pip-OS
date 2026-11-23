/*
 * Frames for tests/test_tft.py. Before each present the ROM prints
 * "ev=step n=<n>", after it "ev=shown", then waits for a key press so
 * the test can take the HDMI screenshot and the panel transcript.
 *
 *   0 first  a scene with colours, lines and text
 *   1 same   the same frame again
 *   2 small  a 3x2 rectangle at 101,57
 *   3 holo   after a holotape ran and returned
 *   4 wake   after power screen off and on
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
    pip_rect_xywh(40, 40, 120, 80, PIP_RECT_FILLED);
    pip_set_color(0x10e0f0, PIP_COLOR_NONE);
    pip_rect_xywh(0, 0, 320, 240, 0);
    pip_draw_line(0, 239, 319, 0);
    pip_set_color(0xfefefe, 0x400000);
    pip_draw_text(8, 200, "PIP-OS PANEL 0123", 1);
    pip_set_color(0x07f81f, PIP_COLOR_NONE);
    pip_draw_point(319, 239, 0x07f81f);
    step();
    step();
    pip_set_color(0xff0000, PIP_COLOR_NONE);
    pip_rect_xywh(101, 57, 3, 2, PIP_RECT_FILLED);
    step();
    rt_check_eq("holo", pip_holo_launch("TAPE.HTP", ""), 200);
    step();
    rt_check_eq("off", pip_power(PIP_POWER_SCREEN_OFF), 0);
    rt_check_eq("on", pip_power(PIP_POWER_SCREEN_ON), 0);
    step();
    rt_checks_done();
    return 0;
}
