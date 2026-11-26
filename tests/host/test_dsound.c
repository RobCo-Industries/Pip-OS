#include "dsound_rules.h"
#include "rc_test.h"

TEST(io_adds)
{
    CHECK_EQ(pipos_dsound_io_adds(0), 2u);
    CHECK_EQ(pipos_dsound_io_adds(1), 2u);
    CHECK_EQ(pipos_dsound_io_adds(3), 2u);
    CHECK_EQ(pipos_dsound_io_adds(4), 3u);
    CHECK_EQ(pipos_dsound_io_adds(64), 18u);
}

TEST(text_adds)
{
    CHECK_EQ(pipos_dsound_text_adds(""), 1u);
    CHECK_EQ(pipos_dsound_text_adds("12345"), 1u);
    CHECK_EQ(pipos_dsound_text_adds("123456"), 2u);
    CHECK_EQ(pipos_dsound_text_adds("PIP-OS READY"), 3u);
    /* Only printable characters count. */
    CHECK_EQ(pipos_dsound_text_adds("12345\t\r\x01"), 1u);
}

TEST(cap)
{
    struct pipos_dsound s;

    pipos_dsound_init(&s);
    pipos_dsound_open(&s);
    pipos_dsound_add(&s, 30);
    pipos_dsound_add(&s, 30);
    CHECK_EQ(s.owed, PIPOS_CLICK_CAP);
    CHECK_EQ(PIPOS_CLICK_CAP, 40u);
}

TEST(closed_gate_adds_nothing)
{
    struct pipos_dsound s;
    int t;

    pipos_dsound_init(&s);
    pipos_dsound_add(&s, 10);
    CHECK_EQ(s.owed, 0u);
    for (t = 0; t < 100; t++) {
        CHECK_EQ(pipos_dsound_tick(&s, 1), -1);
    }
    pipos_dsound_open(&s);
    pipos_dsound_add(&s, 10);
    pipos_dsound_close(&s);
    CHECK_EQ(s.owed, 0u);
    pipos_dsound_add(&s, 10);
    CHECK_EQ(s.owed, 0u);
}

TEST(nothing_owed_nothing_played)
{
    struct pipos_dsound s;
    int t;

    pipos_dsound_init(&s);
    pipos_dsound_open(&s);
    for (t = 0; t < 1000; t++) {
        CHECK_EQ(pipos_dsound_tick(&s, 1), -1);
    }
}

TEST(busy_voice_holds_a_click)
{
    struct pipos_dsound s;
    int t;

    pipos_dsound_init(&s);
    pipos_dsound_open(&s);
    pipos_dsound_add(&s, 1);
    for (t = 0; t < 500; t++) {
        CHECK_EQ(pipos_dsound_tick(&s, 0), -1);
    }
    CHECK_EQ(s.owed, 1u);
    CHECK_EQ(pipos_dsound_tick(&s, 1), 0);
    CHECK_EQ(s.owed, 0u);
}

TEST(clicks_in_turn)
{
    struct pipos_dsound s;
    int t, k, want = 0;

    pipos_dsound_init(&s);
    pipos_dsound_open(&s);
    for (t = 0; t < 3000; t++) {
        pipos_dsound_add(&s, PIPOS_CLICK_CAP);
        k = pipos_dsound_tick(&s, 1);
        if (k >= 0) {
            CHECK_EQ(k, want);
            want = (want + 1) % 12;
        }
    }
}

/* The debt held at the cap, the gate open from tick 15. */
TEST(pinned_start_ticks)
{
    static const int want[] = { 15, 21, 27, 34, 41, 48, 52, 74, 82, 89, 94, 139 };
    struct pipos_dsound s;
    int t, n = 0, last = -1;

    pipos_dsound_init(&s);
    for (t = 0; t < 940; t++) {
        if (t == 15) {
            pipos_dsound_open(&s);
        }
        pipos_dsound_add(&s, PIPOS_CLICK_CAP);
        if (pipos_dsound_tick(&s, 1) >= 0) {
            if (n < 12) {
                CHECK_EQ(t, want[n]);
            }
            n++;
            last = t;
        }
    }
    CHECK_EQ(n, 99);
    CHECK_EQ(last, 899);
}

/*
 * Over 10000 ticks at the cap: bursts of 3 to 21 clicks, gaps of 3 to 8
 * ticks inside a burst and pauses of 15 to 60 ticks more between bursts.
 */
TEST(bursts_and_pauses)
{
    struct pipos_dsound s;
    int t, prev = -1, burst = 0, bursts = 0;

    pipos_dsound_init(&s);
    pipos_dsound_open(&s);
    for (t = 0; t < 10000; t++) {
        pipos_dsound_add(&s, PIPOS_CLICK_CAP);
        if (pipos_dsound_tick(&s, 1) < 0) {
            continue;
        }
        if (prev >= 0) {
            int gap = t - prev;

            CHECK(gap >= 3);
            if (gap > 8) {
                CHECK(gap >= 3 + 15 && gap <= 8 + 60);
                CHECK(burst >= 3 && burst <= 21);
                bursts++;
                burst = 0;
            }
        }
        burst++;
        prev = t;
    }
    CHECK(bursts > 50);
}

TEST(rom_cuts_the_debt_to_8)
{
    struct pipos_dsound s;
    int t, prev = -1, n = 0;

    pipos_dsound_init(&s);
    pipos_dsound_open(&s);
    pipos_dsound_add(&s, 30);
    pipos_dsound_rom(&s);
    CHECK_EQ(s.owed, PIPOS_CLICK_CAP_ROM);
    CHECK_EQ(PIPOS_CLICK_CAP_ROM, 8u);
    pipos_dsound_add(&s, 30);
    CHECK_EQ(s.owed, 8u);
    for (t = 0; t < 3000; t++) {
        if (pipos_dsound_tick(&s, 1) >= 0) {
            CHECK(prev < 0 || t - prev >= 3);
            prev = t;
            n++;
        }
    }
    CHECK_EQ(n, 8);
}
