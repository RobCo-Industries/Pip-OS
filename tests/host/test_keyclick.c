#include "bootlines.h"
#include "keyclick.h"
#include "rc_test.h"

TEST(space_and_controls_are_silent)
{
    CHECK_EQ(pipos_click_hz(' ', 0), 0);
    CHECK_EQ(pipos_click_hz(' ', 3), 0);
    CHECK_EQ(pipos_click_hz('\t', 0), 0);
    CHECK_EQ(pipos_click_hz('\x7f', 0), 0);
    CHECK_EQ(pipos_click_hz((char)0xdb, 0), 0);
}

TEST(pitch_rule)
{
    /* '*' is 42: steps (42 + pos) % 5. */
    CHECK_EQ(pipos_click_hz('*', 0), 3300);
    CHECK_EQ(pipos_click_hz('*', 1), 3450);
    CHECK_EQ(pipos_click_hz('*', 2), 3600);
    CHECK_EQ(pipos_click_hz('*', 3), 3000);
    CHECK_EQ(pipos_click_hz('*', 4), 3150);
    CHECK_EQ(pipos_click_hz('*', 5), 3300);
    CHECK_EQ(pipos_click_hz('!', 0), 3000 + 150 * (33 % 5));
    CHECK_EQ(pipos_click_hz('~', 0), 3000 + 150 * (126 % 5));
}

TEST(pitch_stays_in_band)
{
    unsigned c;
    unsigned pos;
    uint32_t hz;

    for (c = 33; c <= 126; c++) {
        for (pos = 0; pos < 64; pos++) {
            hz = pipos_click_hz((char)c, pos);
            CHECK(hz >= PIPOS_CLICK_BASE_HZ);
            CHECK(hz <= PIPOS_CLICK_BASE_HZ + PIPOS_CLICK_STEP_HZ * (PIPOS_CLICK_STEPS - 1));
        }
    }
}

TEST(equal_characters_do_not_drone)
{
    unsigned i;

    for (i = 1; i < 15; i++) {
        CHECK(pipos_click_hz('*', i) != pipos_click_hz('*', i - 1));
    }
}

TEST(clicks_need_a_delay)
{
    CHECK_EQ(pipos_click_on(0, 1), 0);
    CHECK_EQ(pipos_click_on(20, 0), 0);
    CHECK_EQ(pipos_click_on(20, 1), 1);
    CHECK_EQ(pipos_click_on(1, 1), 1);
}

TEST(timing)
{
    CHECK_EQ(pipos_type_ms("PIP-OS READY", 0), 0);
    CHECK_EQ(pipos_type_ms("PIP-OS READY", 20), 240);
    CHECK_EQ(pipos_type_ms("", 20), 0);
    CHECK_EQ(pipos_type_ms("LOADER V1.1", 100), 1100);
}

TEST(boot_text_at_the_suggested_pace)
{
    uint32_t ms = 0;
    unsigned lines = 0;
    unsigned i;

    for (i = 0; pipos_boot_head[i] != NULL; i++, lines++) {
        ms += pipos_type_ms(pipos_boot_head[i], 20);
    }
    for (i = 0; pipos_boot_tail[i] != NULL; i++, lines++) {
        ms += pipos_type_ms(pipos_boot_tail[i], 20);
    }
    /* 13 fixed lines, 321 characters; the two card lines come on top. */
    CHECK_EQ(lines, 13);
    CHECK_EQ(ms, 321u * 20u);
}

static char steps[64];
static unsigned nsteps;

static void rec_wait(void) { steps[nsteps++] = 'W'; }
static void rec_draw(char c) { (void)c; steps[nsteps++] = 'D'; }
static int rec_tone(uint32_t hz, uint32_t ms) { (void)hz; (void)ms; steps[nsteps++] = 'T'; return 0; }
static void rec_delay(uint32_t ms) { (void)ms; steps[nsteps++] = 'L'; }

static const struct pipos_type_ops rec_ops = { rec_wait, rec_draw, rec_tone, rec_delay };

static int steps_are(const char *want)
{
    unsigned i;

    for (i = 0; want[i] != '\0'; i++) {
        if (i >= nsteps || steps[i] != want[i]) {
            return 0;
        }
    }
    return i == nsteps;
}

TEST(character_and_click_come_together)
{
    /* Wait for the beep, then draw and click at once, then the delay. */
    nsteps = 0;
    pipos_type_char('*', 0, 20, 1, &rec_ops);
    CHECK(steps_are("WDTL"));
    /* A space makes no sound and waits for none. */
    nsteps = 0;
    pipos_type_char(' ', 1, 20, 1, &rec_ops);
    CHECK(steps_are("DL"));
    /* Without clicks nothing waits for sound. */
    nsteps = 0;
    pipos_type_char('*', 0, 20, 0, &rec_ops);
    CHECK(steps_are("DL"));
}
