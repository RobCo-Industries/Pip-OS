/*
 * Sleeps 800 ms, past the 700 ms after the jump, then reads its own ROM
 * file and the config and exits at once. With the system sound on the
 * reads owe disk clicks that are still owed when the ROM returns.
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
    int i;

    (void)startup;
    rt_mark("t0");
    pip_sleep_ms(800);
    for (i = 0; i < 3; i++) {
        read_all("/PIPBOY/ROM1.ROM");
        read_all("/PIPBOY/PIPOS.CFG");
    }
    rt_mark("read");
    return 0;
}
