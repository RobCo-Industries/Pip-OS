/*
 * Starts a 500 ms sample, queues a second one behind it and exits after
 * 100 ms: the kernel must stop the one that plays and drop the one that waits.
 */
#include "rt.h"

#define LEN 4000u

static uint8_t buf[LEN];

int pip_main(const pipos_startup_t *startup)
{
    uint32_t i;
    int32_t play, queue;

    (void)startup;
    for (i = 0; i < LEN; i++) {
        buf[i] = (uint8_t)i;
    }
    play = pip_sample_queue(buf, LEN);
    for (i = 0; i < LEN; i++) {
        buf[i] = (uint8_t)~i;
    }
    queue = pip_sample_queue(buf, LEN);
    rt_begin("queue_start");
    rt_dec("play", play);
    rt_dec("queue", queue);
    rt_dec("status", pip_audio_status());
    rt_end();
    pip_sleep_ms(100);
    return 0;
}
