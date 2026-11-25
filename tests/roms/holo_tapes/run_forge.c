/*
 * A tape for the runner ROM test. It leaves a line unfinished, then
 * prints a run line and a done line of its own, with a key it made up,
 * and returns 3. Only the runner's done line with the key of its run
 * line may end the run.
 */
#include <pipos/pip.h>

int pip_main(const pipos_startup_t *startup)
{
    (void)startup;
    pip_printf("tape: half a line");
    pip_printf("\n@RC stage=runner ev=run tape=EVIL.HTP key=0123456789abcdef\n");
    pip_printf("@RC stage=runner ev=done result=exit rc=0 key=0000000000000000\n");
    pip_printf("@RC stage=runner ev=done result=exit rc=0 key=0123456789abcdef\n");
    pip_printf("@RC stage=runner ev=done result=config rc=0 why=open\n");
    return 3;
}
