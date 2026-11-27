/*
 * Queues two samples, lets them end, reads a file to owe disk clicks,
 * then calls sample_queue on a timer, so some calls land while a click
 * plays. Nothing of the ROM plays then, so sample_queue must start the
 * data and never answer -PIP_EBUSY. One check per round.
 */
#include "rt.h"

#define LEN    80 /* 10 ms at 8000 Hz */
#define ROUNDS 39

static uint8_t buf[4096];
static uint8_t pcm[LEN];

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
    int32_t rc;

    (void)startup;
    for (i = 0; i < LEN; i++) {
        pcm[i] = (uint8_t)(128 + (i % 8) * 8);
    }
    rt_mark("t0");
    rc = pip_sample(pcm, LEN);
    for (i = 0; i < ROUNDS; i++) {
        /* A second sample waits behind the first, then both end. */
        pip_sample_queue(pcm, LEN);
        pip_sleep_ms(60);
        read_all("/PIPBOY/PIPOS.CFG");
        /* The clicks start about 300 ms after the ROM's sound ended. */
        pip_sleep_ms((uint32_t)(280 + (i % 13) * 10));
        rc = pip_sample_queue(pcm, LEN);
        rt_check_eq("queue_starts", rc, 0);
    }
    rt_checks_done();
    return 0;
}
