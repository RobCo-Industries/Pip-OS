/* power(0): the device reboots and this ROM runs again. */
#include "rt.h"

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    rt_begin("rebooting");
    rt_end();
    pip_power(0);
    rt_begin("returned");
    rt_end();
    return 1;
}
