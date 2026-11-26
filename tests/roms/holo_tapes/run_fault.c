/* A tape for the runner ROM test: runs an undefined instruction. */
#include <pipos/pip.h>

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    __asm__ volatile(".word 0xe7f000f0");
    return 0;
}
