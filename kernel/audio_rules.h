/*
 * The audio group's argument rules and the note conversion. Pure code: it
 * builds and runs on the host in the unit tests.
 */
#ifndef PIPOS_AUDIO_RULES_H
#define PIPOS_AUDIO_RULES_H

#include <stddef.h>
#include <stdint.h>

#include <pipos/abi.h>
#include <robco/audio.h>

/* 0, or -PIP_EINVAL unless hz is 20 to 20000 and ms is 1 to 60000. */
int32_t pipos_audio_check_tone(uint32_t hz, uint32_t ms);

/* 0, or -PIP_EINVAL unless count is 1 to 256. */
int32_t pipos_audio_check_count(uint32_t count);

/* 0, or -PIP_EINVAL unless hz is 0 (a rest) or 20 to 20000 and ms is 1 to 60000. */
int32_t pipos_audio_check_note(const pip_note_t *n);

/*
 * Checks all `count` notes, then converts them into out[count]. On a bad
 * note it returns -PIP_EINVAL and leaves out untouched. count is checked
 * as in pipos_audio_check_count().
 */
int32_t pipos_audio_convert(const pip_note_t *in, uint32_t count, audio_note_t *out);

/*
 * Sets *level to `level` and returns the previous one, or returns
 * -PIP_EINVAL and keeps *level when level is above 100.
 */
int32_t pipos_audio_volume_swap(unsigned *level, uint32_t new_level);

#endif
