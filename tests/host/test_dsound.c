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

TEST(click_handed_before_the_close_is_pending)
{
    struct pipos_dsound s;
    int t, k = -1;

    pipos_dsound_init(&s);
    CHECK_EQ(pipos_dsound_click_pending(&s), 0);
    pipos_dsound_open(&s);
    pipos_dsound_add(&s, 10);
    for (t = 0; t < 100 && k < 0; t++) {
        k = pipos_dsound_tick(&s, 1);
    }
    CHECK(k >= 0);
    /* The close comes in the tick right after the hand-over. */
    pipos_dsound_close(&s);
    CHECK_EQ(pipos_dsound_click_pending(&s), 1);
    /* The next tick starts that click in the driver and hands none over. */
    CHECK_EQ(pipos_dsound_tick(&s, 1), -1);
    CHECK_EQ(pipos_dsound_click_pending(&s), 0);
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
        if (pipos_dsound_rom_tick(&s, 0, 0) >= 0) {
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
 * The ROM pace, the debt held at 8, the gate closed at the jump as after
 * the quiet second. The work of the first 700 ms is owed, and the first
 * click plays on the tick the 700 ms end.
 */
TEST(rom_pace_pinned_start_ticks)
{
    static const int want[] = { 70, 76, 82, 119, 123, 130, 135, 221, 226, 231, 237, 294 };
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
    CHECK_EQ(last, 909);
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

/* ROM_RUN reached, nothing owed, the ROM quiet long enough. */
static void rom_running(struct pipos_dsound *s)
{
    int t;

    pipos_dsound_init(s);
    pipos_dsound_rom(s);
    for (t = 0; t < 100; t++) {
        pipos_dsound_rom_tick(s, 0, 0);
    }
    CHECK_EQ(s->phase, PIPOS_DS_ROM_RUN);
}

/* A 2.9 s ROM sound over a debt, then silence: the clicks play after 300 ms. */
TEST(rom_drop_a_sound_under_3_s_keeps_the_debt)
{
    struct pipos_dsound s;
    int t;

    rom_running(&s);
    pipos_dsound_add(&s, 5);
    for (t = 0; t < 290; t++) {
        CHECK_EQ(pipos_dsound_rom_tick(&s, 1, 0), -1);
    }
    /* The 300 ms after the sound do not count toward the drop. */
    for (t = 0; t < (int)PIPOS_ROM_QUIET_TICKS; t++) {
        CHECK_EQ(pipos_dsound_rom_tick(&s, 0, 0), -1);
    }
    CHECK_EQ(s.owed, 5u);
    CHECK_EQ(s.held, 290u);
    CHECK(pipos_dsound_rom_tick(&s, 0, 0) >= 0);
    CHECK_EQ(s.held, 0u);
    CHECK_EQ(s.owed, 4u);
}

/* A 3.1 s ROM sound over a debt: dropped at 3.01 s, nothing plays after. */
TEST(rom_drop_a_sound_over_3_s_drops_the_debt)
{
    struct pipos_dsound s;
    int t;

    rom_running(&s);
    pipos_dsound_add(&s, 5);
    for (t = 0; t < (int)PIPOS_ROM_DROP_TICKS; t++) {
        CHECK_EQ(pipos_dsound_rom_tick(&s, 1, 0), -1);
    }
    CHECK_EQ(s.owed, 5u);
    CHECK_EQ(pipos_dsound_rom_tick(&s, 1, 0), -1);
    CHECK_EQ(s.owed, 0u);
    for (t = 0; t < 9; t++) {
        CHECK_EQ(pipos_dsound_rom_tick(&s, 1, 0), -1);
    }
    for (t = 0; t < 1000; t++) {
        CHECK_EQ(pipos_dsound_rom_tick(&s, 0, 0), -1);
    }
}

/*
 * 2.9 s of sound, 100 ms of silence, for ever: no click fits. Silence does
 * not reset the count, so the debt drops on the 11th tick of the second
 * sound, 3.11 s after the first one started.
 */
TEST(rom_drop_a_tune_with_short_gaps_drops_the_debt)
{
    struct pipos_dsound s;
    int t, dropped = -1;

    rom_running(&s);
    pipos_dsound_add(&s, 5);
    for (t = 0; t < 1200; t++) {
        int busy = t % 300 < 290;

        CHECK_EQ(pipos_dsound_rom_tick(&s, busy, 0), -1);
        if (dropped < 0 && s.owed == 0) {
            dropped = t;
        }
    }
    CHECK_EQ(dropped, 310);
}

/* Work that arrives late in a long sound is owed for 3 s of that sound. */
TEST(rom_drop_late_work_in_a_long_sound)
{
    struct pipos_dsound s;
    int t, dropped = -1;

    rom_running(&s);
    for (t = 0; t < 1000; t++) {
        if (t == 500) {
            pipos_dsound_add(&s, 3);
        }
        CHECK_EQ(pipos_dsound_rom_tick(&s, 1, 0), -1);
        if (t >= 500 && dropped < 0 && s.owed == 0) {
            dropped = t;
        }
    }
    CHECK_EQ(dropped, 500 + (int)PIPOS_ROM_DROP_TICKS);
}

/* Silence with nothing owed resets nothing it needs to: a new debt starts at 0. */
TEST(rom_drop_count_starts_with_the_debt)
{
    struct pipos_dsound s;
    int t;

    rom_running(&s);
    for (t = 0; t < 500; t++) {
        pipos_dsound_rom_tick(&s, 1, 0);
    }
    CHECK_EQ(s.held, 0u);
    pipos_dsound_add(&s, 2);
    for (t = 0; t < 200; t++) {
        pipos_dsound_rom_tick(&s, 1, 0);
    }
    CHECK_EQ(s.held, 200u);
    CHECK_EQ(s.owed, 2u);
}

/* A ROM state with one click owed and due on the next tick. */
static void rom_ready(struct pipos_dsound *s)
{
    int t;

    pipos_dsound_init(s);
    pipos_dsound_rom(s);
    for (t = 0; t < 100; t++) {
        pipos_dsound_rom_tick(s, 0, 0);
    }
    pipos_dsound_add(s, 1);
}

/*
 * Every audio call and return path: does it cut a click, and does it
 * restart the 300 ms? docs/BOOT.md has the same table.
 */
static const struct call_row {
    const char *call;
    int cuts;
    int outcome;
    int restarts;
} call_table[] = {
    { "tone EINVAL", 0, PIPOS_ACALL_REFUSED, 0 },
    { "tone EIO", 1, PIPOS_ACALL_FAILED, 0 },
    { "tone started", 1, PIPOS_ACALL_STARTED, 1 },
    { "sample EINVAL EFAULT on the check", 0, PIPOS_ACALL_REFUSED, 0 },
    { "sample EFAULT EIO, nothing of the image played", 1, PIPOS_ACALL_FAILED, 0 },
    { "sample EFAULT EIO, the image's sound stopped", 1, PIPOS_ACALL_STOPPED, 1 },
    { "sample started", 1, PIPOS_ACALL_STARTED, 1 },
    { "sample_queue EINVAL EFAULT on the check", 0, PIPOS_ACALL_REFUSED, 0 },
    { "sample_queue EBUSY", 1, PIPOS_ACALL_REFUSED, 0 },
    { "sample_queue joins the image's play", 1, PIPOS_ACALL_JOINED, 0 },
    { "sample_queue EFAULT EIO on the queue", 1, PIPOS_ACALL_FAILED, 0 },
    { "sample_queue EFAULT EIO, no play to queue behind", 1, PIPOS_ACALL_FAILED, 0 },
    { "sample_queue started", 1, PIPOS_ACALL_STARTED, 1 },
    { "play_seq EINVAL EFAULT", 0, PIPOS_ACALL_REFUSED, 0 },
    { "play_seq EIO", 1, PIPOS_ACALL_FAILED, 0 },
    { "play_seq started", 1, PIPOS_ACALL_STARTED, 1 },
    { "audio_stop, the image's sound plays", 1, PIPOS_ACALL_STOPPED, 1 },
    { "audio_stop, nothing of the image plays", 0, PIPOS_ACALL_IDLE, 0 },
    { "volume", 0, PIPOS_ACALL_ASKED, 0 },
    { "sample_rate", 0, PIPOS_ACALL_ASKED, 0 },
    { "audio_status", 0, PIPOS_ACALL_ASKED, 0 },
};

TEST(rom_call_table)
{
    struct pipos_dsound s;
    size_t i;
    int t;

    for (i = 0; i < sizeof(call_table) / sizeof(call_table[0]); i++) {
        const struct call_row *r = &call_table[i];

        CHECK_EQ(pipos_dsound_restarts(r->outcome), r->restarts);
        rom_ready(&s);
        if (r->cuts) {
            /* A click plays: it must stop, and none starts during the call. */
            CHECK_EQ(pipos_dsound_cut(&s, 1, 1), 1);
            CHECK_EQ(pipos_dsound_rom_tick(&s, 0, 0), -1);
        }
        pipos_dsound_call_end(&s, r->outcome);
        CHECK_EQ(s.in_call, 0);
        if (r->restarts) {
            CHECK_EQ(s.quiet, 0u);
            for (t = 0; t < (int)PIPOS_ROM_QUIET_TICKS; t++) {
                CHECK_EQ(pipos_dsound_rom_tick(&s, 0, 0), -1);
            }
        } else {
            CHECK(s.quiet > PIPOS_ROM_QUIET_TICKS);
        }
        CHECK(pipos_dsound_rom_tick(&s, 0, 0) >= 0);
    }
}

/* The cut stops only a click that plays. */
TEST(rom_cut)
{
    struct pipos_dsound s;

    pipos_dsound_init(&s);
    CHECK_EQ(pipos_dsound_cut(&s, 1, 1), 1);
    CHECK_EQ(pipos_dsound_cut(&s, 1, 0), 0); /* the click ended */
    CHECK_EQ(pipos_dsound_cut(&s, 0, 1), 0); /* the ROM's own sound */
    CHECK_EQ(pipos_dsound_cut(&s, 0, 0), 0);
    /* Outside a ROM the call holds the boot pace too, with no quiet time. */
    pipos_dsound_open(&s);
    pipos_dsound_add(&s, 1);
    CHECK_EQ(pipos_dsound_tick(&s, 1), -1);
    pipos_dsound_call_end(&s, PIPOS_ACALL_STARTED);
    CHECK(pipos_dsound_tick(&s, 1) >= 0);
}

/* A refused call repeated every 100 ms never holds the clicks. */
TEST(rom_refused_calls_hold_nothing)
{
    struct pipos_dsound s;
    int t, played = 0;

    rom_ready(&s);
    pipos_dsound_add(&s, 7);
    for (t = 0; t < 1500; t++) {
        if (t % 10 == 0) {
            pipos_dsound_call_end(&s, PIPOS_ACALL_REFUSED);
        }
        played += pipos_dsound_rom_tick(&s, 0, 0) >= 0;
    }
    CHECK_EQ(played, 8);
    CHECK_EQ(s.owed, 0u);
}

/* A 2 s tone: the 300 ms count from its own end, not from the call. */
TEST(rom_quiet_from_the_sound_end)
{
    struct pipos_dsound s;
    int t;

    rom_ready(&s);
    CHECK_EQ(pipos_dsound_cut(&s, 0, 0), 0);
    pipos_dsound_call_end(&s, PIPOS_ACALL_STARTED);
    for (t = 0; t < 200; t++) {
        CHECK_EQ(pipos_dsound_rom_tick(&s, 1, 0), -1);
    }
    for (t = 0; t < (int)PIPOS_ROM_QUIET_TICKS; t++) {
        CHECK_EQ(pipos_dsound_rom_tick(&s, 0, 0), -1);
    }
    CHECK(pipos_dsound_rom_tick(&s, 0, 0) >= 0);
}

/* audio_status, volume and sample_rate leave a playing click and the rules alone. */
TEST(rom_questions_leave_a_click)
{
    struct pipos_dsound s, before;

    rom_ready(&s);
    CHECK(pipos_dsound_rom_tick(&s, 0, 0) >= 0); /* a click starts */
    pipos_dsound_add(&s, 1);
    before = s;
    pipos_dsound_call_end(&s, PIPOS_ACALL_ASKED);
    CHECK_EQ(s.quiet, before.quiet);
    CHECK_EQ(s.owed, before.owed);
    CHECK_EQ(s.in_call, 0);
    CHECK_EQ(pipos_dsound_rom_tick(&s, 0, 1), -1); /* it plays on */
}

/*
 * The ROM returned with a debt: nothing is owed and nothing plays while
 * the kernel redraws and reports, even with card work. The gate then opens
 * in the boot pace.
 */
TEST(rom_end)
{
    struct pipos_dsound s;
    int t;

    rom_running(&s);
    pipos_dsound_add(&s, 8);
    pipos_dsound_rom_end(&s);
    CHECK_EQ(pipos_dsound_in_rom(&s), 0);
    CHECK_EQ(s.owed, 0u);
    CHECK_EQ(s.phase, PIPOS_DS_BOOT_QUIET);
    for (t = 0; t < 200; t++) {
        pipos_dsound_add(&s, 2);
        CHECK_EQ(pipos_dsound_tick(&s, 1), -1);
    }
    CHECK_EQ(s.owed, 0u);
    pipos_dsound_open(&s);
    CHECK_EQ(s.phase, PIPOS_DS_BOOT_OPEN);
    pipos_dsound_add(&s, 30);
    CHECK_EQ(s.owed, 30u);
    CHECK(pipos_dsound_tick(&s, 1) >= 0);
}

/* The boot's gate closed at the jump, as after the quiet second. */
static void rom_after_quiet(struct pipos_dsound *s)
{
    pipos_dsound_init(s);
    pipos_dsound_open(s);
    pipos_dsound_add(s, 5);
    pipos_dsound_close(s); /* the quiet second */
    pipos_dsound_add(s, 5); /* the scroll's time: not owed */
    CHECK_EQ(s->owed, 0u);
    pipos_dsound_hold(s);
    pipos_dsound_rom(s);
}

/* A ROM reads its files in the first 700 ms, then sleeps: the clicks come after. */
TEST(rom_work_in_the_first_700_ms_is_owed)
{
    struct pipos_dsound s;
    int t, first = -1, played = 0;

    rom_after_quiet(&s);
    for (t = 0; t < 600; t++) {
        if (t < 20) {
            pipos_dsound_add(&s, pipos_dsound_io_adds(1));
        }
        if (pipos_dsound_rom_tick(&s, 0, 0) >= 0) {
            if (first < 0) {
                first = t;
            }
            played++;
        }
    }
    CHECK_EQ(first, (int)PIPOS_ROM_START_TICKS);
    CHECK_EQ(played, (int)PIPOS_CLICK_CAP_ROM); /* the cap */
}

/* Work during a ROM sound and its 300 ms is owed; it plays after them. */
TEST(rom_work_during_a_rom_sound_is_owed)
{
    struct pipos_dsound s;
    int t, played = 0;

    rom_after_quiet(&s);
    for (t = 0; t < 100; t++) {
        pipos_dsound_rom_tick(&s, 0, 0);
    }
    for (t = 0; t < 50; t++) {
        if (t == 10) {
            pipos_dsound_add(&s, 2); /* while the ROM's sound plays */
        }
        CHECK_EQ(pipos_dsound_rom_tick(&s, 1, 0), -1);
    }
    for (t = 0; t < (int)PIPOS_ROM_QUIET_TICKS; t++) {
        if (t == 10) {
            pipos_dsound_add(&s, 20); /* in the 300 ms after it */
        }
        CHECK_EQ(pipos_dsound_rom_tick(&s, 0, 0), -1);
    }
    CHECK_EQ(s.owed, PIPOS_CLICK_CAP_ROM);
    for (t = 0; t < 1000; t++) {
        played += pipos_dsound_rom_tick(&s, 0, 0) >= 0;
    }
    CHECK_EQ(played, (int)PIPOS_CLICK_CAP_ROM);
}

/* A ROM sound in the first 700 ms does not count toward the drop. */
TEST(rom_drop_counts_from_the_end_of_the_700_ms)
{
    struct pipos_dsound s;
    int t;

    rom_after_quiet(&s);
    pipos_dsound_add(&s, 3);
    for (t = 0; t < (int)(PIPOS_ROM_START_TICKS + PIPOS_ROM_DROP_TICKS); t++) {
        CHECK_EQ(pipos_dsound_rom_tick(&s, 1, 0), -1);
    }
    CHECK_EQ(s.owed, 3u);
    CHECK_EQ(pipos_dsound_rom_tick(&s, 1, 0), -1);
    CHECK_EQ(s.owed, 0u);
}

/* The jump: nothing plays and the 700 ms wait until the anchor. */
TEST(rom_700_ms_start_at_the_anchor)
{
    struct pipos_dsound s;
    int t;

    pipos_dsound_init(&s);
    pipos_dsound_open(&s);
    pipos_dsound_add(&s, 3);
    pipos_dsound_hold(&s);
    CHECK(pipos_dsound_in_rom(&s));
    /* The wait for a playing boot click: ticks pass, the 700 ms do not. */
    for (t = 0; t < 200; t++) {
        CHECK_EQ(pipos_dsound_rom_tick(&s, 0, t < 3), -1);
    }
    pipos_dsound_add(&s, 30);
    CHECK_EQ(s.owed, PIPOS_CLICK_CAP_ROM);
    /* The calls of the gate belong to the boot: they do nothing now. */
    pipos_dsound_close(&s);
    CHECK_EQ(s.owed, PIPOS_CLICK_CAP_ROM);
    pipos_dsound_open(&s);
    CHECK_EQ(s.phase, PIPOS_DS_ROM_HOLD);
    pipos_dsound_rom(&s);
    for (t = 0; t < (int)PIPOS_ROM_START_TICKS; t++) {
        CHECK_EQ(pipos_dsound_rom_tick(&s, 0, 0), -1);
    }
    CHECK(pipos_dsound_rom_tick(&s, 0, 0) >= 0);
}
