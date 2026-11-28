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

/* PIPOS.SET: the reader gives "ok" here for a file that applies. */
static const char *rd(const char *text, pipos_levels_t *lv)
{
    const char *why = pipos_levels_read_text(text, strlen(text), lv);

    return why == NULL ? "ok" : why;
}

static void set5(pipos_levels_t *lv, unsigned m, unsigned r, unsigned s, unsigned c, unsigned g)
{
    lv->master = m;
    lv->level[PIPOS_SND_RADIO] = r;
    lv->level[PIPOS_SND_SFX] = s;
    lv->level[PIPOS_SND_RECORDINGS] = c;
    lv->level[PIPOS_SND_GAMES] = g;
}

static int same5(const pipos_levels_t *lv, unsigned m, unsigned r, unsigned s, unsigned c,
                 unsigned g)
{
    return lv->master == m && lv->level[PIPOS_SND_RADIO] == r &&
           lv->level[PIPOS_SND_SFX] == s && lv->level[PIPOS_SND_RECORDINGS] == c &&
           lv->level[PIPOS_SND_GAMES] == g;
}

TEST(set_write_text)
{
    pipos_levels_t lv;
    char out[PIPOS_LEVELS_TEXT_MAX + 1];

    set5(&lv, 70, 45, 100, 60, 55);
    CHECK_EQ(pipos_levels_write_text(&lv, out, sizeof(out)), 68);
    CHECK_STR(out, "[audio]\nvolume = 70\nradio = 45\nsfx = 100\nrecordings = 60\ngames = 55\n");
    set5(&lv, 100, 100, 100, 100, 100);
    CHECK_EQ(pipos_levels_write_text(&lv, out, sizeof(out)), PIPOS_LEVELS_TEXT_MAX);
    set5(&lv, 0, 0, 0, 0, 0);
    CHECK_EQ(pipos_levels_write_text(&lv, out, sizeof(out)), 62);
    CHECK_STR(out, "[audio]\nvolume = 0\nradio = 0\nsfx = 0\nrecordings = 0\ngames = 0\n");
    /* Past the top it writes the top. */
    set5(&lv, 101, 200, 5, 7, 9);
    CHECK(pipos_levels_write_text(&lv, out, sizeof(out)) > 0);
    CHECK_STR(out, "[audio]\nvolume = 100\nradio = 100\nsfx = 5\nrecordings = 7\ngames = 9\n");
    /* A buffer too small for the longest text gets nothing. */
    CHECK_EQ(pipos_levels_write_text(&lv, out, PIPOS_LEVELS_TEXT_MAX), 0);
}

static void round_trip(unsigned m, unsigned r, unsigned s, unsigned c, unsigned g)
{
    pipos_levels_t in, back;
    char out[PIPOS_LEVELS_TEXT_MAX + 1];
    size_t n;

    set5(&in, m, r, s, c, g);
    set5(&back, 1, 2, 3, 4, 5);
    n = pipos_levels_write_text(&in, out, sizeof(out));
    CHECK(n > 0 && n <= PIPOS_LEVELS_TEXT_MAX);
    CHECK(pipos_levels_read_text(out, n, &back) == NULL);
    CHECK(same5(&back, m, r, s, c, g));
}

TEST(set_round_trip)
{
    unsigned a, b;

    round_trip(0, 0, 0, 0, 0);
    round_trip(100, 100, 100, 100, 100);
    round_trip(0, 100, 0, 100, 0);
    round_trip(100, 0, 100, 0, 100);
    round_trip(1, 9, 10, 99, 100);
    for (a = 0; a <= 100; a += 7) {
        for (b = 0; b <= 100; b += 3) {
            round_trip(a, b, 100 - b, (a + b) % 101, (a * b) % 101);
        }
    }
}

TEST(set_keys_absent_keep_their_value)
{
    pipos_levels_t lv;

    set5(&lv, 80, 70, 60, 50, 40);
    CHECK_STR(rd("[audio]\nsfx = 30\ngames = 020\n", &lv), "ok");
    CHECK(same5(&lv, 80, 70, 30, 50, 20));
    CHECK_STR(rd("", &lv), "ok");
    CHECK(same5(&lv, 80, 70, 30, 50, 20));
    CHECK_STR(rd("# kept by the device\n\n[audio]\n; master\nvolume = 10 # quiet\n\n", &lv),
              "ok");
    CHECK(same5(&lv, 10, 70, 30, 50, 20));
}

TEST(set_values)
{
    static const struct {
        const char *value;
        const char *why;
        unsigned got;
    } cases[] = {
        { "050", "ok", 50 },   { " 50 ", "ok", 50 },  { "0", "ok", 0 },
        { "100", "ok", 100 },  { "0000100", "ok", 100 },
        { "50x", "value", 0 }, { "101", "value", 0 }, { "-1", "value", 0 },
        { "", "value", 0 },    { "+5", "value", 0 },  { "5.0", "value", 0 },
        { "0x10", "value", 0 }, { "99999999999", "value", 0 },
    };
    pipos_levels_t lv;
    char text[64];
    unsigned i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        set5(&lv, 1, 2, 3, 4, 5);
        snprintf(text, sizeof(text), "[audio]\nradio = %s\nsfx = 9\n", cases[i].value);
        CHECK_STR(rd(text, &lv), cases[i].why);
        if (strcmp(cases[i].why, "ok") == 0) {
            CHECK(same5(&lv, 1, cases[i].got, 9, 4, 5));
        } else {
            /* The good key next to it does not apply either. */
            CHECK(same5(&lv, 1, 2, 3, 4, 5));
        }
    }
}

TEST(set_keys_and_sections)
{
    pipos_levels_t lv;

    set5(&lv, 1, 2, 3, 4, 5);
    CHECK_STR(rd("[audio]\nsfx = 10\nsfx = 20\n", &lv), "key");
    CHECK_STR(rd("[audio]\nsfx = 10\nbeep = yes\n", &lv), "key");
    CHECK_STR(rd("[audio]\nsfx = 10\n[display]\n", &lv), "key");
    CHECK_STR(rd("sfx = 10\n", &lv), "key");
    /* The PIPOS.CFG parser matches keys and sections with case. */
    CHECK_STR(rd("[audio]\nSFX = 10\n", &lv), "key");
    CHECK_STR(rd("[Audio]\nsfx = 10\n", &lv), "key");
    CHECK(same5(&lv, 1, 2, 3, 4, 5));
    /* A second [audio] header is the same section. */
    CHECK_STR(rd("[audio]\nsfx = 10\n[audio]\ngames = 20\n", &lv), "ok");
    CHECK(same5(&lv, 1, 2, 10, 4, 20));
}

TEST(set_syntax_and_size)
{
    pipos_levels_t lv;
    char text[PIPOS_LEVELS_FILE_MAX + 2];
    size_t head;

    set5(&lv, 1, 2, 3, 4, 5);
    CHECK_STR(rd("[audio]\nsfx = 10\nloud\n", &lv), "syntax");
    CHECK_STR(rd("[audio\nsfx = 10\n", &lv), "syntax");
    CHECK(same5(&lv, 1, 2, 3, 4, 5));

    /* Exactly 512 bytes: a valid file padded with blank lines. */
    head = (size_t)snprintf(text, sizeof(text), "[audio]\nsfx = 10\n");
    memset(text + head, '\n', PIPOS_LEVELS_FILE_MAX - head);
    text[PIPOS_LEVELS_FILE_MAX] = '\0';
    CHECK_EQ(strlen(text), PIPOS_LEVELS_FILE_MAX);
    CHECK_STR(rd(text, &lv), "ok");
    CHECK(same5(&lv, 1, 2, 10, 4, 5));
    text[PIPOS_LEVELS_FILE_MAX] = '\n';
    text[PIPOS_LEVELS_FILE_MAX + 1] = '\0';
    CHECK_STR(rd(text, &lv), "size");
}

TEST(set_reason_order)
{
    pipos_levels_t lv;
    char text[PIPOS_LEVELS_FILE_MAX + 2];

    set5(&lv, 1, 2, 3, 4, 5);
    /* syntax before key and value, wherever the bad line is */
    CHECK_STR(rd("[audio]\nsfx = 999\nfoo = 1\nloud\n", &lv), "syntax");
    CHECK_STR(rd("[other]\nloud\n", &lv), "syntax");
    /* key before value */
    CHECK_STR(rd("[audio]\nsfx = 999\nfoo = 1\n", &lv), "key");
    CHECK_STR(rd("[audio]\nsfx = 999\nsfx = 1\n", &lv), "key");
    CHECK_STR(rd("[audio]\nsfx = x\n[display]\n", &lv), "key");
    /* size before all */
    memset(text, 'x', sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
    CHECK_STR(rd(text, &lv), "size");
    CHECK(same5(&lv, 1, 2, 3, 4, 5));
}
