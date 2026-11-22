/* Logs one line and calls exit(7). */
#include "rt.h"

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    pip_log("hello from the test rom\n");
    pip_exit(7);
}
