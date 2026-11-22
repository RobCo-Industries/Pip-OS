/* Sleeps forever in 1 s steps: the kernel must still answer script commands. */
#include "rt.h"

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    rt_begin("sleeping");
    rt_end();
    for (;;) {
        pip_sleep_ms(1000);
    }
}
