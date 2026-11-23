/*
 * holo_enumerate and holo_launch on a card that fails a read. test_holo.py
 * puts NEAR.HTP inside the card image and FAR.HTP past its end, so the
 * card reports an error for FAR.HTP's data only.
 */
#include "rt.h"

static pip_holo_info_t all[PIP_HOLO_ENUM_MAX];

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    rt_check_eq("near", pip_holo_launch("NEAR.HTP", "exit 3"), 3);
    rt_check_eq("enum", pip_holo_enumerate(all, PIP_HOLO_ENUM_MAX), -PIP_EIO);
    rt_check_eq("far", pip_holo_launch("FAR.HTP", "exit 4"), -PIP_EIO);
    rt_check_eq("last_far", pip_holo_last_status(), -PIP_EIO);
    rt_check_eq("near_again", pip_holo_launch("NEAR.HTP", "exit 5"), 5);
    return rt_checks_done();
}
