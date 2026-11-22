/*
 * The holotapes group: system calls 0x60-0x63. No call is served yet:
 * every number returns -PIP_ENOSYS. See sys.h for the interface.
 */
#include "sys.h"

/* PIPOS_SYS_ENTRY(HOLO, <NAME>, <handler>), one line per served call, in number order. */
static const pipos_sys_entry_t holo_calls[] = {
};

const pipos_sys_table_t pipos_holo_table = PIPOS_SYS_TABLE(HOLO, "holo", holo_calls);

void pipos_holo_init(void)
{
}

void pipos_holo_image_start(int kind)
{
    (void)kind;
}

void pipos_holo_image_end(int kind)
{
    (void)kind;
}

void pipos_holo_poll(void)
{
}
