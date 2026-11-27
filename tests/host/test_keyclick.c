#include "bootlines.h"
#include "keyclick.h"
#include "rc_test.h"

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

static void rec_draw(char c) { (void)c; steps[nsteps++] = 'D'; }
static void rec_delay(uint32_t ms) { (void)ms; steps[nsteps++] = 'L'; }

static const struct pipos_type_ops rec_ops = { rec_draw, rec_delay };

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

TEST(typing_makes_no_sound)
{
    /* Draw, then the delay. No step waits for a sound or starts one. */
    nsteps = 0;
    pipos_type_char('*', 20, &rec_ops);
    CHECK(steps_are("DL"));
    nsteps = 0;
    pipos_type_char(' ', 20, &rec_ops);
    CHECK(steps_are("DL"));
}
