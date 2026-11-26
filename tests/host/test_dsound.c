#include "bootlines.h"
#include "dsound_rules.h"
#include "sound_table.h"
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

TEST(scroll_wait)
{
    /* Fast typing: the quiet second holds the scroll. */
    CHECK_EQ(pipos_dsound_scroll_wait(1300, 1300, 200, 1, 1000), 700u);
    CHECK_EQ(pipos_dsound_scroll_wait(1999, 1300, 200, 1, 1000), 1u);
    CHECK_EQ(pipos_dsound_scroll_wait(2000, 1300, 200, 1, 1000), 0u);
    /* Slow typing: the line delay after the line holds it. */
    CHECK_EQ(pipos_dsound_scroll_wait(2500, 2500, 200, 1, 1000), 200u);
    CHECK_EQ(pipos_dsound_scroll_wait(2700, 2500, 200, 1, 1000), 0u);
    /* No gate, the sound off: only the line delay. */
    CHECK_EQ(pipos_dsound_scroll_wait(1300, 1300, 200, 0, 1000), 200u);
    CHECK_EQ(pipos_dsound_scroll_wait(1300, 1300, 0, 0, 0), 0u);
    /* Late callers wait nothing, and the clock may wrap. */
    CHECK_EQ(pipos_dsound_scroll_wait(9000, 1300, 200, 1, 1000), 0u);
    CHECK_EQ(pipos_dsound_scroll_wait(0xfffffff0u, 0xfffffff0u, 0, 1, 0xfffffff0u), 1000u);
    CHECK_EQ(pipos_dsound_scroll_wait(0x000003d0u, 0xfffffff0u, 0, 1, 0xfffffff0u), 8u);
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

/* The stock boot text from line 2 on: 271 characters and 13 line delays. */
TEST(note_text_counts_the_boot_lines)
{
    static const char *const head[] = { "BANNER", "AB", "", "CDE", NULL };
    static const char *const tail[] = { "F", NULL };
    static const char *const none[] = { NULL };
    unsigned chars, delays;

    pipos_dsound_note_text(pipos_boot_head, pipos_boot_tail, &chars, &delays);
    CHECK_EQ(chars, 271u);
    CHECK_EQ(delays, 13u);
    /* The banner is line 1: it ends before the note starts. */
    pipos_dsound_note_text(head, tail, &chars, &delays);
    CHECK_EQ(chars, 6u);
    CHECK_EQ(delays, 5u);
    pipos_dsound_note_text(none, none, &chars, &delays);
    CHECK_EQ(chars, 0u);
    CHECK_EQ(delays, 2u);
    CHECK_EQ(pipos_dsound_printable("A\tB\x01 "), 3u);
}

TEST(note_fits_when_the_text_outlasts_it)
{
    CHECK_EQ(SOUND_NOTE_MS, 6326u);
    /* 271 * 20 + 13 * 200 = 8020 */
    CHECK_EQ(pipos_dsound_note_fits(271, 13, 20, 200, SOUND_NOTE_MS), 1);
    /* 271 * 2 + 13 * 0 = 542 */
    CHECK_EQ(pipos_dsound_note_fits(271, 13, 2, 0, SOUND_NOTE_MS), 0);
    /* The bound itself fits, one ms less does not. */
    CHECK_EQ(pipos_dsound_note_fits(100, 10, 10, 100, 2000), 1);
    CHECK_EQ(pipos_dsound_note_fits(100, 10, 10, 100, 2001), 0);
    /* Typing off: no note, however long the line delay. */
    CHECK_EQ(pipos_dsound_note_fits(271, 13, 0, 10000, SOUND_NOTE_MS), 0);
    /* No overflow at the top of the range. */
    CHECK_EQ(pipos_dsound_note_fits(4000000000u, 0, 4000000000u, 0, 0xffffffffu), 1);
}

TEST(way_of_a_due_click)
{
    CHECK_EQ(PIPOS_NOTE_TAIL_MS, 30u);
    /* Idle: its own play, note or not. */
    CHECK_EQ(pipos_dsound_way(0, 0, 0, 0), PIPOS_CLICK_PLAY);
    CHECK_EQ(pipos_dsound_way(0, 1, 0, 0), PIPOS_CLICK_PLAY);
    /* Busy with anything but the note: it waits. */
    CHECK_EQ(pipos_dsound_way(1, 0, 5000, 0), PIPOS_CLICK_WAIT);
    /* Over the note, more than 30 ms left: it mixes. */
    CHECK_EQ(pipos_dsound_way(1, 1, 6000, 0), PIPOS_CLICK_MIX);
    CHECK_EQ(pipos_dsound_way(1, 1, 31, 0), PIPOS_CLICK_MIX);
    /* The note's last 30 ms: it waits. */
    CHECK_EQ(pipos_dsound_way(1, 1, 30, 0), PIPOS_CLICK_WAIT);
    CHECK_EQ(pipos_dsound_way(1, 1, 1, 0), PIPOS_CLICK_WAIT);
    CHECK_EQ(pipos_dsound_way(1, 1, 0, 0), PIPOS_CLICK_WAIT);
    /* An overlay still runs: it waits. */
    CHECK_EQ(pipos_dsound_way(1, 1, 6000, 1), PIPOS_CLICK_WAIT);
}

/* A click refused by the driver comes back on the next tick, in turn. */
TEST(refused_click_is_not_lost)
{
    struct pipos_dsound s, before;
    int k;

    pipos_dsound_init(&s);
    pipos_dsound_open(&s);
    pipos_dsound_add(&s, 2);
    before = s;
    k = pipos_dsound_tick(&s, 1);
    CHECK_EQ(k, 0);
    s = before;
    CHECK_EQ(pipos_dsound_tick(&s, 1), 0);
    CHECK_EQ(s.owed, 1u);
}

/*
 * The ROM pace, the debt held at 8 and the gate closed at the jump, as
 * after the quiet second. The gate opens 700 ms after the jump.
 */
TEST(rom_pace_pinned_start_ticks)
{
    static const int want[] = { 71, 77, 83, 120, 124, 131, 136, 222, 227, 232, 238, 295 };
    struct pipos_dsound s;
    int t, n = 0, last = -1;

    pipos_dsound_init(&s);
    pipos_dsound_rom(&s);
    for (t = 0; t < 1000; t++) {
        pipos_dsound_add(&s, PIPOS_CLICK_CAP);
        CHECK(s.owed <= PIPOS_CLICK_CAP_ROM);
        if (pipos_dsound_rom_tick(&s, 0, 0) >= 0) {
            if (n < 12) {
                CHECK_EQ(t, want[n]);
            }
            n++;
            last = t;
        }
    }
    CHECK_EQ(n, 35);
    CHECK_EQ(last, 910);
}

/* Groups of 1 to 4, gaps of 3 to 8 ticks, pauses of 30 to 90 ticks more. */
TEST(rom_pace_groups_and_pauses)
{
    struct pipos_dsound s;
    int t, prev = -1, group = 0, groups = 0;

    pipos_dsound_init(&s);
    pipos_dsound_open(&s);
    pipos_dsound_rom(&s);
    for (t = 0; t < 20000; t++) {
        pipos_dsound_add(&s, PIPOS_CLICK_CAP);
        if (pipos_dsound_rom_tick(&s, 0, 0) < 0) {
            continue;
        }
        CHECK(t > (int)PIPOS_ROM_START_TICKS - 1);
        if (prev >= 0) {
            int gap = t - prev;

            CHECK(gap >= 3);
            if (gap > 8) {
                CHECK(gap >= 3 + 30 && gap <= 8 + 90);
                CHECK(group >= 1 && group <= 4);
                groups++;
                group = 0;
            }
        }
        group++;
        prev = t;
    }
    CHECK(groups > 100);
}

/* With the gate open at the jump, the old debt waits 700 ms too. */
TEST(rom_waits_700_ms_after_the_jump)
{
    struct pipos_dsound s;
    int t;

    pipos_dsound_init(&s);
    pipos_dsound_open(&s);
    pipos_dsound_add(&s, 30);
    pipos_dsound_rom(&s);
    CHECK_EQ(s.owed, 8u);
    for (t = 0; t < (int)PIPOS_ROM_START_TICKS; t++) {
        CHECK_EQ(pipos_dsound_rom_tick(&s, 0, 0), -1);
    }
    CHECK(pipos_dsound_rom_tick(&s, 0, 0) >= 0);
    CHECK_EQ(s.owed, 7u);
}

/* A click waits 300 ms after the ROM's last sound and while a click plays. */
TEST(rom_sound_holds_a_click_300_ms)
{
    struct pipos_dsound s;
    int t;

    pipos_dsound_init(&s);
    pipos_dsound_rom(&s);
    for (t = 0; t < 100; t++) {
        pipos_dsound_rom_tick(&s, 0, 0);
    }
    pipos_dsound_add(&s, 1);
    for (t = 0; t < 50; t++) {
        CHECK_EQ(pipos_dsound_rom_tick(&s, 1, 0), -1);
    }
    for (t = 0; t < (int)PIPOS_ROM_QUIET_TICKS; t++) {
        CHECK_EQ(pipos_dsound_rom_tick(&s, 0, 0), -1);
    }
    CHECK_EQ(pipos_dsound_rom_tick(&s, 0, 1), -1); /* a kernel click plays */
    CHECK(pipos_dsound_rom_tick(&s, 0, 0) >= 0);
    CHECK_EQ(s.owed, 0u);
}

/* A debt that waits more than 3 s in a row for a ROM sound is dropped. */
TEST(rom_drops_a_debt_after_3_s)
{
    struct pipos_dsound s;
    int t;

    pipos_dsound_init(&s);
    pipos_dsound_rom(&s);
    for (t = 0; t < 100; t++) {
        pipos_dsound_rom_tick(&s, 0, 0);
    }
    pipos_dsound_add(&s, 30);
    for (t = 0; t < (int)PIPOS_ROM_DROP_TICKS; t++) {
        CHECK_EQ(pipos_dsound_rom_tick(&s, 1, 0), -1);
    }
    CHECK_EQ(s.owed, 8u);
    CHECK_EQ(pipos_dsound_rom_tick(&s, 1, 0), -1);
    CHECK_EQ(s.owed, 0u);
    /* A break in the wait starts the count again. */
    pipos_dsound_add(&s, 3);
    for (t = 0; t < 200; t++) {
        pipos_dsound_rom_tick(&s, 1, 0);
    }
    /* 310 ms of a kernel click: the ROM is quiet, the wait for it ends. */
    for (t = 0; t <= (int)PIPOS_ROM_QUIET_TICKS; t++) {
        pipos_dsound_rom_tick(&s, 0, 1);
    }
    CHECK_EQ(s.held, 0u);
    for (t = 0; t < 200; t++) {
        pipos_dsound_rom_tick(&s, 1, 0);
    }
    CHECK_EQ(s.owed, 3u);
}

/* The yield: a playing click stops; a sound call counts as a ROM sound. */
TEST(rom_yield)
{
    struct pipos_dsound s;
    int t;

    pipos_dsound_init(&s);
    CHECK_EQ(pipos_dsound_yields(&s, 1, 1, 0), 1);
    CHECK_EQ(pipos_dsound_yields(&s, 1, 0, 1), 0); /* the click ended */
    CHECK_EQ(pipos_dsound_yields(&s, 0, 1, 1), 0); /* the ROM's own sound */
    pipos_dsound_rom(&s);
    for (t = 0; t < 100; t++) {
        pipos_dsound_rom_tick(&s, 0, 0);
    }
    pipos_dsound_add(&s, 1);
    CHECK_EQ(pipos_dsound_yields(&s, 0, 0, 0), 0); /* audio_status, volume */
    CHECK(s.quiet > PIPOS_ROM_QUIET_TICKS);
    CHECK_EQ(pipos_dsound_yields(&s, 1, 1, 1), 1); /* tone, sample, stop */
    CHECK_EQ(s.quiet, 0u);
    for (t = 0; t < (int)PIPOS_ROM_QUIET_TICKS; t++) {
        CHECK_EQ(pipos_dsound_rom_tick(&s, 0, 0), -1);
    }
    CHECK(pipos_dsound_rom_tick(&s, 0, 0) >= 0);
}

/* The ROM's end: the boot pace, nothing owed, the gate open. */
TEST(rom_end)
{
    struct pipos_dsound s;

    pipos_dsound_init(&s);
    pipos_dsound_rom(&s);
    pipos_dsound_add(&s, 3);
    pipos_dsound_rom_end(&s);
    CHECK_EQ(s.rom, 0);
    CHECK_EQ(s.owed, 0u);
    CHECK_EQ(s.open, 1);
    pipos_dsound_add(&s, 30);
    CHECK_EQ(s.owed, 30u);
}
