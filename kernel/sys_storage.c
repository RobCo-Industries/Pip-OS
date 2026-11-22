/*
 * The saves and files group: system calls 0x50-0x57. No call is served yet:
 * every number returns -PIP_ENOSYS. See sys.h for the interface.
 */
#include "sys.h"

/* PIPOS_SYS_ENTRY(STORAGE, <NAME>, <handler>), one line per served call, in number order. */
static const pipos_sys_entry_t storage_calls[] = {
};

const pipos_sys_table_t pipos_storage_table = PIPOS_SYS_TABLE(STORAGE, "storage", storage_calls);

void pipos_storage_init(void)
{
}

void pipos_storage_image_start(int kind)
{
    (void)kind;
}

void pipos_storage_image_end(int kind)
{
    (void)kind;
}

void pipos_storage_poll(void)
{
}
