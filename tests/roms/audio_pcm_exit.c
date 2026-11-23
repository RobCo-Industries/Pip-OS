/* Starts the longest sample, 8.192 s, and exits after 100 ms: the kernel must stop it. */
#include "rt.h"

static uint8_t buf[PIP_SAMPLE_MAX];

int pip_main(const pipos_startup_t *startup)
{
    uint32_t i;

    (void)startup;
    for (i = 0; i < PIP_SAMPLE_MAX; i++) {
        buf[i] = (uint8_t)i;
    }
    rt_begin("sample_start");
    rt_dec("rc", pip_sample(buf, PIP_SAMPLE_MAX));
    rt_dec("status", pip_audio_status());
    rt_end();
    pip_sleep_ms(100);
    return 0;
}
