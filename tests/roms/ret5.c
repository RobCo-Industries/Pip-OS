/* Returns 5 from pip_main: crt0 passes it to exit. */
#include "rt.h"

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    return 5;
}
