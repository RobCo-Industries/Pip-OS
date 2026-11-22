/*
 * The graphics group: system calls 0x01-0x0F. No call is served yet:
 * every number returns -PIP_ENOSYS. See sys.h for the interface.
 */
#include "sys.h"

/* PIPOS_SYS_ENTRY(GFX, <NAME>, <handler>), one line per served call, in number order. */
static const pipos_sys_entry_t gfx_calls[] = {
};

const pipos_sys_table_t pipos_gfx_table = PIPOS_SYS_TABLE(GFX, "gfx", gfx_calls);

void pipos_gfx_init(void)
{
}

void pipos_gfx_image_start(int kind)
{
    (void)kind;
}

void pipos_gfx_image_end(int kind)
{
    (void)kind;
}

void pipos_gfx_poll(void)
{
}
