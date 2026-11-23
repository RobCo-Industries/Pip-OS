/* The audio group's argument rules and the note conversion. See audio_rules.h. */
#include "audio_rules.h"

static int hz_ok(uint32_t hz)
{
    return hz >= PIP_TONE_FREQ_MIN && hz <= PIP_TONE_FREQ_MAX;
}

static int ms_ok(uint32_t ms)
{
    return ms >= PIP_TONE_MS_MIN && ms <= PIP_TONE_MS_MAX;
}

int32_t pipos_audio_check_tone(uint32_t hz, uint32_t ms)
{
    return hz_ok(hz) && ms_ok(ms) ? 0 : -PIP_EINVAL;
}

int32_t pipos_audio_check_sample(uint32_t len)
{
    return len >= 1 && len <= PIP_SAMPLE_MAX ? 0 : -PIP_EINVAL;
}

int32_t pipos_audio_check_count(uint32_t count)
{
    return count >= PIP_SEQ_MIN && count <= PIP_SEQ_MAX ? 0 : -PIP_EINVAL;
}

int32_t pipos_audio_check_note(const pip_note_t *n)
{
    uint32_t hz = n->freq_hz;

    return (hz == PIP_NOTE_REST || hz_ok(hz)) && ms_ok(n->duration_ms) ? 0 : -PIP_EINVAL;
}

int32_t pipos_audio_convert(const pip_note_t *in, uint32_t count, audio_note_t *out)
{
    uint32_t i;

    if (pipos_audio_check_count(count) != 0) {
        return -PIP_EINVAL;
    }
    for (i = 0; i < count; i++) {
        if (pipos_audio_check_note(&in[i]) != 0) {
            return -PIP_EINVAL;
        }
    }
    for (i = 0; i < count; i++) {
        out[i].hz = in[i].freq_hz;
        out[i].ms = in[i].duration_ms;
    }
    return 0;
}

int32_t pipos_audio_volume_swap(unsigned *level, uint32_t new_level)
{
    unsigned prev = *level;

    if (new_level > PIP_VOLUME_MAX) {
        return -PIP_EINVAL;
    }
    *level = new_level;
    return (int32_t)prev;
}
