/*
 * Reads its own ROM file and the config three times at its start, then
 * sleeps 4 s and exits. With the system sound on the reads owe disk
 * clicks, and the kernel pays at most 8 of them while the ROM sleeps.
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
    for (i = 0; i < 3; i++) {
        read_all("/PIPBOY/ROM1.ROM");
        read_all("/PIPBOY/PIPOS.CFG");
    }
    rt_mark("read");
    for (i = 0; i < 40; i++) {
        pip_sleep_ms(100);
    }
    rt_mark("done");
    return 0;
}
