/* audio_rules.c: every limit at its edge, the note conversion, the volume swap. */
#include <stdint.h>
#include <string.h>

#include "audio_rules.h"
#include "rc_test.h"

TEST(tone_limits)
{
    CHECK_EQ(pipos_audio_check_tone(20, 1), 0);
    CHECK_EQ(pipos_audio_check_tone(20000, 60000), 0);
    CHECK_EQ(pipos_audio_check_tone(19, 100), -PIP_EINVAL);
    CHECK_EQ(pipos_audio_check_tone(20001, 100), -PIP_EINVAL);
    CHECK_EQ(pipos_audio_check_tone(0, 100), -PIP_EINVAL);
    CHECK_EQ(pipos_audio_check_tone(440, 0), -PIP_EINVAL);
    CHECK_EQ(pipos_audio_check_tone(440, 60001), -PIP_EINVAL);
    CHECK_EQ(pipos_audio_check_tone(0xffffffffu, 0xffffffffu), -PIP_EINVAL);
}

TEST(count_limits)
{
    CHECK_EQ(pipos_audio_check_count(0), -PIP_EINVAL);
    CHECK_EQ(pipos_audio_check_count(1), 0);
    CHECK_EQ(pipos_audio_check_count(256), 0);
    CHECK_EQ(pipos_audio_check_count(257), -PIP_EINVAL);
    CHECK_EQ(pipos_audio_check_count(0xffffffffu), -PIP_EINVAL);
}

TEST(note_limits)
{
    pip_note_t n;

    n = (pip_note_t){ 0, 1 };
    CHECK_EQ(pipos_audio_check_note(&n), 0);
    n = (pip_note_t){ 20, 60000 };
    CHECK_EQ(pipos_audio_check_note(&n), 0);
    n = (pip_note_t){ 20000, 1 };
    CHECK_EQ(pipos_audio_check_note(&n), 0);
    n = (pip_note_t){ 19, 100 };
    CHECK_EQ(pipos_audio_check_note(&n), -PIP_EINVAL);
    n = (pip_note_t){ 1, 100 };
    CHECK_EQ(pipos_audio_check_note(&n), -PIP_EINVAL);
    n = (pip_note_t){ 20001, 100 };
    CHECK_EQ(pipos_audio_check_note(&n), -PIP_EINVAL);
    n = (pip_note_t){ 440, 0 };
    CHECK_EQ(pipos_audio_check_note(&n), -PIP_EINVAL);
    n = (pip_note_t){ 0, 0 };
    CHECK_EQ(pipos_audio_check_note(&n), -PIP_EINVAL);
    n = (pip_note_t){ 440, 60001 };
    CHECK_EQ(pipos_audio_check_note(&n), -PIP_EINVAL);
    n = (pip_note_t){ 440, 65535 };
    CHECK_EQ(pipos_audio_check_note(&n), -PIP_EINVAL);
}

TEST(convert_notes)
{
    pip_note_t in[3] = { { 440, 100 }, { 0, 50 }, { 20000, 60000 } };
    audio_note_t out[3];

    CHECK_EQ(pipos_audio_convert(in, 3, out), 0);
    CHECK_EQ(out[0].hz, 440);
    CHECK_EQ(out[0].ms, 100);
    CHECK_EQ(out[1].hz, 0);
    CHECK_EQ(out[1].ms, 50);
    CHECK_EQ(out[2].hz, 20000);
    CHECK_EQ(out[2].ms, 60000);
}

TEST(convert_256)
{
    static pip_note_t in[257];
    static audio_note_t out[257];
    unsigned i;

    for (i = 0; i < 257; i++) {
        in[i] = (pip_note_t){ (uint16_t)(100 + i), (uint16_t)(1 + i) };
    }
    CHECK_EQ(pipos_audio_convert(in, 256, out), 0);
    CHECK_EQ(out[255].hz, 355);
    CHECK_EQ(out[255].ms, 256);
    CHECK_EQ(pipos_audio_convert(in, 257, out), -PIP_EINVAL);
    CHECK_EQ(pipos_audio_convert(in, 0, out), -PIP_EINVAL);
}

/* A bad last note refuses the whole sequence and writes nothing. */
TEST(convert_bad_last_note)
{
    pip_note_t in[4] = { { 440, 100 }, { 0, 50 }, { 880, 10 }, { 440, 0 } };
    audio_note_t out[4];
    audio_note_t keep[4];

    memset(out, 0xa5, sizeof(out));
    memcpy(keep, out, sizeof(out));
    CHECK_EQ(pipos_audio_convert(in, 4, out), -PIP_EINVAL);
    CHECK_MEM(out, keep, sizeof(out));
    in[3] = (pip_note_t){ 19, 10 };
    CHECK_EQ(pipos_audio_convert(in, 4, out), -PIP_EINVAL);
    CHECK_MEM(out, keep, sizeof(out));
    in[3] = (pip_note_t){ 20, 10 };
    CHECK_EQ(pipos_audio_convert(in, 4, out), 0);
}

TEST(volume_swap)
{
    unsigned level = 70;

    CHECK_EQ(pipos_audio_volume_swap(&level, 40), 70);
    CHECK_EQ(level, 40);
    CHECK_EQ(pipos_audio_volume_swap(&level, 100), 40);
    CHECK_EQ(pipos_audio_volume_swap(&level, 101), -PIP_EINVAL);
    CHECK_EQ(level, 100);
    CHECK_EQ(pipos_audio_volume_swap(&level, 0xffffffffu), -PIP_EINVAL);
    CHECK_EQ(pipos_audio_volume_swap(&level, 0), 100);
    CHECK_EQ(pipos_audio_volume_swap(&level, 0), 0);
    CHECK_EQ(level, 0);
}
