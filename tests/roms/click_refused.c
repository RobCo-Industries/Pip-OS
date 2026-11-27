/*
 * Reads files and repeats a refused tone every 100 ms, with the system
 * sound on. A call that starts no sound must not hold the clicks back.
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
    int i, ok = 0;

    (void)startup;
    rt_mark("t0");
    for (i = 0; i < 40; i++) {
        if (i % 4 == 0) {
            read_all("/PIPBOY/PIPOS.CFG");
        }
        ok += pip_tone(0, 20) == 0; /* 0 Hz: EINVAL */
        pip_sleep_ms(100);
    }
    rt_check_eq("all_refused", ok, 0);
    rt_checks_done();
    return 0;
}
