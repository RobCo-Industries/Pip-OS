/* A tape for the runner ROM test: prints one line and returns 0. */
#include <pipos/pip.h>

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    pip_printf("tape: ready\n");
    return 0;
}
