/*
 * sound_store after the card goes away. The test ejects the card from
 * QEMU once the ROM prints ev=waiting; the ROM stores until the card
 * fails, then checks that the levels in force stayed.
 */
#include "rt.h"

int pip_main(const pipos_startup_t *startup)
{
    int32_t rc = 0;
    int i;

    (void)startup;
    rt_check_eq("eio_radio", pip_sound_level(PIP_SND_RADIO, 10), 40);
    rt_check_eq("eio_before", pip_sound_store(), 0);
    rt_begin("waiting");
    rt_end();
    for (i = 0; i < 400; i++) {
        rc = pip_sound_store();
        if (rc != 0) {
            break;
        }
        pip_sleep_ms(50);
    }
    rt_check_eq("eio_store", rc, -PIP_EIO);
    rt_check_eq("eio_again", pip_sound_store(), -PIP_EIO);
    rt_check_eq("eio_radio_kept", pip_sound_level(PIP_SND_RADIO, PIP_LEVEL_ASK), 10);
    rt_check_eq("eio_master_kept", pip_sound_level(PIP_LEVEL_MASTER, PIP_LEVEL_ASK), 80);
    return rt_checks_done();
}
