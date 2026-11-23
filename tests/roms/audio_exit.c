/* Prints the start volume, starts a 30 s tone at 1900 Hz and exits: the kernel must stop it. */
#include "rt.h"

int pip_main(const pipos_startup_t *startup)
{
    int32_t vol = pip_volume(PIP_VOLUME_MAX);
    int32_t rc;

    (void)startup;
    pip_volume((uint32_t)vol);
    rt_mark("t0");
    rc = pip_tone(1900, 30000);
    rt_begin("audio_start");
    rt_dec("vol", vol);
    rt_dec("rc", rc);
    rt_dec("status", pip_audio_status());
    rt_end();
    pip_sleep_ms(100);
    return 0;
}
