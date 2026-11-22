/*
 * The audio group: system calls 0x20-0x25. No call is served yet:
 * every number returns -PIP_ENOSYS. See sys.h for the interface.
 */
#include "sys.h"

/* PIPOS_SYS_ENTRY(AUDIO, <NAME>, <handler>), one line per served call, in number order. */
static const pipos_sys_entry_t audio_calls[] = {
};

const pipos_sys_table_t pipos_audio_table = PIPOS_SYS_TABLE(AUDIO, "audio", audio_calls);

void pipos_audio_init(void)
{
}

void pipos_audio_image_start(int kind)
{
    (void)kind;
}

void pipos_audio_image_end(int kind)
{
    (void)kind;
}

void pipos_audio_poll(void)
{
}
