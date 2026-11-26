#include "rc_test.h"
#include "sound_table.h"

/* The committed table, as the kernel links it. */

TEST(click_stays_under_ceiling)
{
    unsigned k;
    unsigned i;
    int d;
    int top = 0;

    CHECK_EQ(SOUND_CLICK_CEILING, 90);
    /* Room for a click over the note's start, 22 steps. */
    CHECK(SOUND_CLICK_CEILING + 22 <= 127);
    for (k = 0; k < SOUND_CLICK_COUNT; k++) {
        unsigned long sum = 0;
        for (i = 0; i < SOUND_CLICK_LEN; i++) {
            d = (int)sound_click[k][i] - 128;
            if (d < 0)
                d = -d;
            CHECK(d <= (int)SOUND_CLICK_CEILING);
            if (d > top)
                top = d;
            sum += sound_click[k][i];
        }
        /* Mean within 1 step of 128. */
        CHECK(sum > 127ul * SOUND_CLICK_LEN && sum < 129ul * SOUND_CLICK_LEN);
    }
    CHECK_EQ(top, 88);
}

TEST(sizes_and_ends)
{
    CHECK_EQ(SOUND_RATE_HZ, 37500);
    CHECK_EQ(sizeof(sound_click), 12u * 789u);
    CHECK_EQ(sizeof(sound_blip), 600u);
    CHECK_EQ(sizeof(sound_beep), 2250u);
    CHECK_EQ(sound_beep[0], 128);
    CHECK_EQ(sound_beep[SOUND_BEEP_LEN - 1], 128);
    CHECK_EQ(sound_blip[0], 128);
    CHECK_EQ(sound_blip[SOUND_BLIP_LEN - 1], 128);
}

TEST(note_spans)
{
    unsigned k;
    unsigned long total = 0;
    uint32_t phase = 0;

    /* n, phase, inc, dinc, amp, damp, p2: 28 bytes a span. */
    CHECK_EQ(sizeof(sound_note_spans[0]), 28u);
    CHECK_EQ(SOUND_NOTE_SPANS, 162);
    CHECK_EQ(sizeof(sound_note_spans), 162u * 28u);
    for (k = 0; k < SOUND_NOTE_SPANS; k++) {
        const uint32_t *s = sound_note_spans[k];
        int32_t amp = (int32_t)s[4];
        int64_t end = (int64_t)amp + (int64_t)(s[0] - 1) * (int32_t)s[5];

        /* The voice engine's limits. */
        CHECK(s[0] >= 1 && s[0] <= 65535);
        CHECK(s[2] <= 0x7fffffffu);
        CHECK(amp >= 0 && amp <= (127 << 16));
        CHECK(end >= 0 && end <= (127 << 16));
        CHECK(s[6] <= 32768);
        /* The phase carries from span to span. */
        CHECK_EQ(s[1], phase);
        phase = s[1] + s[0] * s[2] + s[3] * (s[0] * (s[0] - 1) / 2);
        total += s[0];
    }
    CHECK_EQ(sound_note_spans[0][4], 0);
    CHECK_EQ(SOUND_NOTE_MS, (total * 1000 + SOUND_RATE_HZ - 1) / SOUND_RATE_HZ);
    CHECK_EQ(SOUND_NOTE_MS, 6326);
}
