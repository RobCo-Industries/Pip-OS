/* The audio group's argument rules, the note conversion and sample_queue's buffer state. See audio_rules.h. */
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

void pipos_pcmq_sync(pipos_pcmq_t *q, int can_queue, int busy)
{
    if (!busy) {
        pipos_pcmq_drop(q);
    } else if (q->wait >= 0 && can_queue) {
        q->play = q->wait;
        q->wait = -1;
    }
}

pipos_pcmq_plan_t pipos_pcmq_plan(const pipos_pcmq_t *q, int can_queue)
{
    pipos_pcmq_plan_t p = { PIPOS_PCMQ_START, 0 };

    if (q->wait >= 0) {
        p.action = PIPOS_PCMQ_BUSY;
        p.buf = -1;
    } else if (q->play >= 0 && can_queue) {
        p.action = PIPOS_PCMQ_QUEUE;
        p.buf = 1 - q->play;
    }
    return p;
}

void pipos_pcmq_queued(pipos_pcmq_t *q, int buf)
{
    q->wait = buf;
}

void pipos_pcmq_started(pipos_pcmq_t *q, int buf)
{
    q->play = buf;
    q->wait = -1;
}

void pipos_pcmq_drop(pipos_pcmq_t *q)
{
    q->play = -1;
    q->wait = -1;
}

int pipos_audio_reconcile(int owned, pipos_pcmq_t *q, int busy, int click, int can_queue)
{
    if (owned && busy && !click) {
        pipos_pcmq_sync(q, can_queue, 1);
        return 1;
    }
    pipos_pcmq_drop(q);
    return 0;
}

int pipos_audio_status_of(int owned, int busy)
{
    return owned && busy;
}
