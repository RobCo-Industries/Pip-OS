/*
 * The input group: system calls 0x10-0x13. No call is served yet:
 * every number returns -PIP_ENOSYS. See sys.h for the interface.
 */
#include "sys.h"

/* PIPOS_SYS_ENTRY(INPUT, <NAME>, <handler>), one line per served call, in number order. */
static const pipos_sys_entry_t input_calls[] = {
};

const pipos_sys_table_t pipos_input_table = PIPOS_SYS_TABLE(INPUT, "input", input_calls);

void pipos_input_init(void)
{
}

void pipos_input_image_start(int kind)
{
    (void)kind;
}

void pipos_input_image_end(int kind)
{
    (void)kind;
}

void pipos_input_poll(void)
{
}
