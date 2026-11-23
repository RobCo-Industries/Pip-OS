/* power(1): the device stops for good. */
#include "rt.h"

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    rt_begin("halting");
    rt_end();
    pip_power(1);
    rt_begin("returned");
    rt_end();
    return 1;
}
