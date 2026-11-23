/*
 * The state handover around holo_launch. The ROM keeps a file open, sets
 * a clip and a colour, plays a long tone and has events waiting, then
 * runs "TAPE.HTP state <handle>". The holotape checks its fresh state,
 * draws and waits; test_holo.py takes a screenshot and kills it with the
 * kill script command. The ROM then checks what came back, draws inside
 * its own clip, and parks so the test can take the second screenshot.
 */
#include "rt.h"

static char buf[8];
static char cmd[32];

int pip_main(const pipos_startup_t *startup)
{
    pip_event_t ev;
    int32_t h, dial, rc;

    (void)startup;
    h = pip_file_open("/CARD.TXT");
    rt_check_eq("open", h, 0);
    rt_check("read_before", pip_file_read(h, buf, 4) == 4 && memcmp(buf, "ABCD", 4) == 0);
    pip_set_clip_xywh(100, 100, 50, 50);
    pip_set_color(0xFF0000u, PIP_COLOR_NONE);
    rt_check_eq("tone", pip_tone(440, 10000), 0);
    rt_check_eq("busy", pip_audio_status(), 1);

    /* The test sends key left press, release, then 3 dial steps: all wait in the queue. */
    dial = pip_read_dial();
    rt_begin("want_keys");
    rt_end();
    while (pip_read_dial() == dial) {
        pip_sleep_ms(20);
    }
    rt_check("keys", 1);

    pip_snprintf(cmd, sizeof(cmd), "state %d", (int)h);
    rc = pip_holo_launch("TAPE.HTP", cmd);
    rt_check_eq("launch_killed", rc, -PIP_EFAULT);
    rt_check_eq("last_killed", pip_holo_last_status(), -PIP_EFAULT);
    rt_check_eq("no_events", pip_poll_event(&ev), 0);
    rt_check_eq("no_sound", pip_audio_status(), 0);
    rt_check("read_after", pip_file_read(h, buf, 4) == 4 && memcmp(buf, "EFGH", 4) == 0);

    /* Events reach the ROM again. */
    rt_begin("want_more");
    rt_end();
    rt_check_eq("event_after", pip_wait_event(&ev, 30000), 1);
    rt_check("event_key", ev.type == PIP_EV_BUTTON);

    /* Clipped to (100,100)-(149,149) in red, if the ROM's state came back. */
    pip_rect_xywh(0, 0, PIP_SCREEN_W, PIP_SCREEN_H, PIP_RECT_FILLED);
    pip_present();
    rt_check_eq("close", pip_file_close(h), 0);
    rt_checks_done();
    h = pip_file_open("/CARD.TXT"); /* "state" shows files=1 while parked */
    rt_begin("parked");
    rt_end();
    for (;;) {
        pip_sleep_ms(50);
    }
}
