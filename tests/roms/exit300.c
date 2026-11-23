/* Calls exit(300): the status is the low 8 bits, 44. */
#include "rt.h"

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    pip_exit(300);
}
