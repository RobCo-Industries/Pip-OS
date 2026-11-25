/* A tape for the runner ROM test: returns 3. */
#include <pipos/pip.h>

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    return 3;
}
