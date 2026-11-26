/*
 * Reads files and asks audio_status every 10 ms, as a UI loop would, with
 * the system sound on. It plays nothing itself, so every answer must be 0,
 * and a click under it must play whole. One check per round of reads.
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
    int i, j, busy;

    (void)startup;
    rt_mark("t0");
    for (i = 0; i < 30; i++) {
        read_all("/PIPBOY/PIPOS.CFG");
        busy = 0;
        for (j = 0; j < 20; j++) {
            busy += pip_audio_status() != 0;
            pip_sleep_ms(10);
        }
        rt_check_eq("status_idle", busy, 0);
    }
    rt_checks_done();
    return 0;
}
