/*
 * Plays its own sounds while it reads files, with the system sound on.
 *
 * Part 1: a 2 s tone, with reads while it sounds. No click may start then.
 * Part 2: 40 rounds of a read, a pause of 300 to 390 ms and a 20 ms tone.
 * A click that plays when the tone starts must give way to it.
 * audio_status is checked in each pause: a click is never the ROM's sound.
 */
#include "rt.h"

static uint8_t buf[4096];

static void read_all(const char *path)
{
    int32_t h = pip_file_open(path);

    if (h < 0) {
        return;
    }
    while (pip_file_read(h, buf, sizeof(buf)) > 0) {
    }
    pip_file_close(h);
}

int pip_main(const pipos_startup_t *startup)
{
    int i, busy = 0;

    (void)startup;
    rt_mark("t0");
    rt_check_eq("long_tone", pip_tone(500, 2000), 0);
    for (i = 0; i < 15; i++) {
        read_all("/PIPBOY/ROM1.ROM");
        pip_sleep_ms(100);
    }
    rt_mark("reads_done");
    while (pip_audio_status() == 1) {
        pip_sleep_ms(10);
    }
    rt_mark("long_tone_end");
    for (i = 0; i < 40; i++) {
        read_all("/PIPBOY/PIPOS.CFG");
        pip_sleep_ms(300u + (uint32_t)(i * 37 % 10) * 10u);
        busy += pip_audio_status() != 0;
        if (pip_tone(1000u + (uint32_t)i, 20) != 0) {
            busy += 100;
        }
        pip_sleep_ms(60); /* the tone ends before the next round, the last before the exit */
    }
    rt_check_eq("status_quiet", busy, 0);
    rt_checks_done();
    return 0;
}
