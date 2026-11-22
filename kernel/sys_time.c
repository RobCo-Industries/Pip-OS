/*
 * The time, sensors, battery and power group: system calls 0x30, 0x31,
 * 0x40, 0x41, 0x47 and 0x4A. No call is served yet: every number
 * returns -PIP_ENOSYS. See sys.h for the interface.
 */
#include "sys.h"

/* PIPOS_SYS_ENTRY(TIME, <NAME>, <handler>), one line per served call, in number order. */
static const pipos_sys_entry_t time_calls[] = {
};

const pipos_sys_table_t pipos_time_table = PIPOS_SYS_TABLE(TIME, "time", time_calls);

void pipos_time_init(void)
{
}

void pipos_time_image_start(int kind)
{
    (void)kind;
}

void pipos_time_image_end(int kind)
{
    (void)kind;
}

void pipos_time_poll(void)
{
}
