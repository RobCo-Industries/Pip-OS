#include <pipos/abi.h>

#include "levels.h"
#include "rc_test.h"
#include "settings.h"

typedef struct {
    int count;
    char text[8][64];
} issues_t;

static void record(const char *rc, const char *key, unsigned line, void *ctx)
{
    issues_t *is = ctx;

    if (is->count < 8) {
        snprintf(is->text[is->count], sizeof(is->text[0]), "%s %s %u", rc, key, line);
    }
    is->count++;
}

static int parse(const char *text, pipos_settings_t *s, issues_t *is)
{
    unsigned line = 0;

    pipos_settings_default(s);
    memset(is, 0, sizeof(*is));
    return pipos_settings_parse(text, strlen(text), s, record, is, &line);
}

/* The formula in 64-bit arithmetic, as a reference. */
static uint32_t ref_gain(unsigned m, unsigned l, unsigned t)
{
    return (uint32_t)(((uint64_t)m * l * l * t + 5000u) / 10000u);
}

TEST(gain_grid)
{
    static const unsigned trims[] = { 0, 50, 100 };
    unsigned m, l, i;

    for (m = 0; m <= 100; m += 5) {
        for (l = 0; l <= 100; l += 5) {
            for (i = 0; i < 3; i++) {
                CHECK_EQ(pipos_levels_gain(m, l, trims[i]), ref_gain(m, l, trims[i]));
            }
        }
    }
}

TEST(gain_full_level_is_the_volume)
{
    unsigned m;

    for (m = 0; m <= 100; m++) {
        CHECK_EQ(pipos_levels_gain(m, 100, 100), m * 100);
    }
}

TEST(gain_examples_and_top)
{
    CHECK_EQ(pipos_levels_gain(100, 100, 100), PIPOS_GAIN_FULL);
    CHECK_EQ(pipos_levels_gain(80, 100, 100), 8000);
    CHECK_EQ(pipos_levels_gain(100, 50, 100), 2500);
    CHECK_EQ(pipos_levels_gain(80, 40, 100), 1280);
    CHECK_EQ(pipos_levels_gain(100, 45, 90), 1823);
    CHECK_EQ(pipos_levels_gain(100, 100, 0), 0);
    /* Values above 100 count as 100, so the product never overflows. */
    CHECK_EQ(pipos_levels_gain(0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu), PIPOS_GAIN_FULL);
    CHECK_EQ(pipos_levels_gain(100, 101, 100), PIPOS_GAIN_FULL);
}

TEST(gain_of_a_kind)
{
    pipos_levels_t lv;

    pipos_levels_default(&lv);
    CHECK_EQ(lv.master, 100);
    lv.master = 80;
    lv.level[PIPOS_SND_RADIO] = 40;
    lv.level[PIPOS_SND_SFX] = 50;
    lv.level[PIPOS_SND_RECORDINGS] = 100;
    lv.level[PIPOS_SND_GAMES] = 0;
    CHECK_EQ(pipos_levels_gain_of(&lv, PIPOS_SND_RADIO, 100), 1280);
    CHECK_EQ(pipos_levels_gain_of(&lv, PIPOS_SND_SFX, 100), 2000);
    CHECK_EQ(pipos_levels_gain_of(&lv, PIPOS_SND_RECORDINGS, 100), 8000);
    CHECK_EQ(pipos_levels_gain_of(&lv, PIPOS_SND_GAMES, 100), 0);
    /* An unknown kind counts as sfx. */
    CHECK_EQ(pipos_levels_gain_of(&lv, 4, 100), 2000);
    CHECK_EQ(pipos_levels_gain_of(&lv, -1, 100), 2000);
}

TEST(kind_of_an_image)
{
    CHECK_EQ(pipos_levels_image_snd(PIP_KIND_ROM), PIPOS_SND_SFX);
    CHECK_EQ(pipos_levels_image_snd(PIP_KIND_HOLOTAPE), PIPOS_SND_GAMES);
    CHECK_STR(pipos_levels_key(PIPOS_SND_RADIO), "radio");
    CHECK_STR(pipos_levels_key(PIPOS_SND_SFX), "sfx");
    CHECK_STR(pipos_levels_key(PIPOS_SND_RECORDINGS), "recordings");
    CHECK_STR(pipos_levels_key(PIPOS_SND_GAMES), "games");
}

TEST(settings_default_levels)
{
    pipos_settings_t s;
    int k;

    pipos_settings_default(&s);
    for (k = 0; k < PIPOS_SND_KINDS; k++) {
        CHECK_EQ(s.levels[k], 100);
    }
}

TEST(settings_good_levels)
{
    pipos_settings_t s;
    issues_t is;

    CHECK_EQ(parse("[audio]\nradio = 40\nsfx = 0\nrecordings = 80\ngames = 100\n", &s, &is), 0);
    CHECK_EQ(is.count, 0);
    CHECK_EQ(s.levels[PIPOS_SND_RADIO], 40);
    CHECK_EQ(s.levels[PIPOS_SND_SFX], 0);
    CHECK_EQ(s.levels[PIPOS_SND_RECORDINGS], 80);
    CHECK_EQ(s.levels[PIPOS_SND_GAMES], 100);
    CHECK_EQ(s.volume, 100);
}

TEST(settings_bad_levels)
{
    pipos_settings_t s;
    issues_t is;
    int k;

    CHECK_EQ(parse("[audio]\nradio = 101\nsfx = -1\nrecordings = loud\ngames = 5x\n", &s, &is),
             0);
    CHECK_EQ(is.count, 4);
    CHECK_STR(is.text[0], "badvalue audio.radio 2");
    CHECK_STR(is.text[1], "badvalue audio.sfx 3");
    CHECK_STR(is.text[2], "badvalue audio.recordings 4");
    CHECK_STR(is.text[3], "badvalue audio.games 5");
    for (k = 0; k < PIPOS_SND_KINDS; k++) {
        CHECK_EQ(s.levels[k], 100);
    }
    /* A bad value keeps the value before it, as for volume. */
    CHECK_EQ(parse("[audio]\nsfx = 30\nsfx = 200\n", &s, &is), 0);
    CHECK_EQ(is.count, 1);
    CHECK_EQ(s.levels[PIPOS_SND_SFX], 30);
    /* The keys belong to [audio] only. */
    CHECK_EQ(parse("[display]\nradio = 40\n", &s, &is), 0);
    CHECK_EQ(is.count, 1);
    CHECK_STR(is.text[0], "unknown display.radio 2");
    CHECK_EQ(s.levels[PIPOS_SND_RADIO], 100);
}
