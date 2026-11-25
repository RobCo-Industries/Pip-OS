#include <robco/console.h>
#include <robco/ini.h>

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

static int parse(const char *text, pipos_settings_t *s, issues_t *is, unsigned *line)
{
    pipos_settings_default(s);
    memset(is, 0, sizeof(*is));
    return pipos_settings_parse(text, strlen(text), s, record, is, line);
}

TEST(defaults)
{
    pipos_settings_t s;

    pipos_settings_default(&s);
    CHECK_EQ(s.scheme, CONSOLE_SCHEME_GREEN);
    CHECK_EQ(s.line_delay_ms, 0);
    CHECK_EQ(s.volume, 100);
    CHECK_EQ(s.beep, 1);
}

TEST(empty_text_keeps_defaults)
{
    pipos_settings_t s;
    issues_t is;
    unsigned line = 99;

    CHECK_EQ(parse("", &s, &is, &line), 0);
    CHECK_EQ(line, 0);
    CHECK_EQ(is.count, 0);
    CHECK_EQ(s.scheme, 0);
    CHECK_EQ(s.volume, 100);
}

TEST(every_key)
{
    pipos_settings_t s;
    issues_t is;
    unsigned line = 0;
    const char *text =
        "# comment\n"
        "[display]\n"
        "scheme = Amber\n"
        "line_delay_ms = 150\n"
        "[audio]\n"
        "volume = 40\n"
        "beep = off\n";

    CHECK_EQ(parse(text, &s, &is, &line), 0);
    CHECK_EQ(is.count, 0);
    CHECK_EQ(s.scheme, CONSOLE_SCHEME_AMBER);
    CHECK_EQ(s.line_delay_ms, 150);
    CHECK_EQ(s.volume, 40);
    CHECK_EQ(s.beep, 0);
}

TEST(scheme_names)
{
    CHECK_EQ(pipos_scheme_from_name("green"), CONSOLE_SCHEME_GREEN);
    CHECK_EQ(pipos_scheme_from_name("AMBER"), CONSOLE_SCHEME_AMBER);
    CHECK_EQ(pipos_scheme_from_name("Blue"), CONSOLE_SCHEME_BLUE);
    CHECK_EQ(pipos_scheme_from_name("white"), CONSOLE_SCHEME_WHITE);
    CHECK_EQ(pipos_scheme_from_name("pink"), -1);
    CHECK_EQ(pipos_scheme_from_name(""), -1);
    CHECK_EQ(pipos_scheme_from_name("greenish"), -1);
    CHECK_STR(pipos_scheme_name(1), "amber");
    CHECK_STR(pipos_scheme_name(4), "unknown");
}

TEST(bad_values_are_reported_and_ignored)
{
    pipos_settings_t s;
    issues_t is;
    unsigned line = 0;
    const char *text =
        "[display]\n"
        "scheme = pink\n"
        "line_delay_ms = 2001\n"
        "[audio]\n"
        "volume = -1\n"
        "beep = maybe\n";

    CHECK_EQ(parse(text, &s, &is, &line), 0);
    CHECK_EQ(is.count, 4);
    CHECK_STR(is.text[0], "badvalue display.scheme 2");
    CHECK_STR(is.text[1], "badvalue display.line_delay_ms 3");
    CHECK_STR(is.text[2], "badvalue audio.volume 5");
    CHECK_STR(is.text[3], "badvalue audio.beep 6");
    CHECK_EQ(s.scheme, 0);
    CHECK_EQ(s.line_delay_ms, 0);
    CHECK_EQ(s.volume, 100);
    CHECK_EQ(s.beep, 1);
}

TEST(number_edges)
{
    pipos_settings_t s;
    issues_t is;
    unsigned line = 0;

    CHECK_EQ(parse("[display]\nline_delay_ms = 2000\n[audio]\nvolume = 0\n", &s, &is, &line), 0);
    CHECK_EQ(is.count, 0);
    CHECK_EQ(s.line_delay_ms, 2000);
    CHECK_EQ(s.volume, 0);

    CHECK_EQ(parse("[audio]\nvolume = 101\n", &s, &is, &line), 0);
    CHECK_EQ(is.count, 1);
    CHECK_EQ(parse("[audio]\nvolume = 1x\n", &s, &is, &line), 0);
    CHECK_EQ(is.count, 1);
    CHECK_EQ(parse("[audio]\nvolume =\n", &s, &is, &line), 0);
    CHECK_EQ(is.count, 1);
    CHECK_EQ(parse("[display]\nline_delay_ms = 99999999999\n", &s, &is, &line), 0);
    CHECK_EQ(is.count, 1);
    CHECK_EQ(s.line_delay_ms, 0);
}

TEST(unknown_keys_and_sections)
{
    pipos_settings_t s;
    issues_t is;
    unsigned line = 0;
    const char *text =
        "top = 1\n"
        "[display]\n"
        "brightness = 3\n"
        "[network]\n"
        "scheme = blue\n"
        "[display]\n"
        "scheme = blue\n";

    CHECK_EQ(parse(text, &s, &is, &line), 0);
    CHECK_EQ(is.count, 3);
    CHECK_STR(is.text[0], "unknown .top 1");
    CHECK_STR(is.text[1], "unknown display.brightness 3");
    CHECK_STR(is.text[2], "unknown network.scheme 5");
    CHECK_EQ(s.scheme, CONSOLE_SCHEME_BLUE);
}

TEST(syntax_error_applies_nothing)
{
    pipos_settings_t s;
    issues_t is;
    unsigned line = 0;

    CHECK_EQ(parse("[display]\nscheme = amber\nnot a key line\n", &s, &is, &line),
             INI_ERR_SYNTAX);
    CHECK_EQ(line, 3);
    CHECK_EQ(s.scheme, CONSOLE_SCHEME_GREEN);
}

TEST(later_key_wins)
{
    pipos_settings_t s;
    issues_t is;
    unsigned line = 0;

    CHECK_EQ(parse("[display]\nscheme = amber\nscheme = white\n", &s, &is, &line), 0);
    CHECK_EQ(s.scheme, CONSOLE_SCHEME_WHITE);
}

TEST(null_report_is_allowed)
{
    pipos_settings_t s;
    unsigned line = 0;
    const char *text = "[x]\ny = 1\n";

    pipos_settings_default(&s);
    CHECK_EQ(pipos_settings_parse(text, strlen(text), &s, NULL, NULL, &line), 0);
}

/* The panel keys: defaults are TFT_CONFIG_DEFAULT of RobCo-Core's tft.h. */
TEST(display_backend_defaults)
{
    pipos_settings_t s;

    pipos_settings_default(&s);
    CHECK_EQ(s.backend, PIPOS_BACKEND_HDMI);
    CHECK_EQ(s.tft.cs, 0);
    CHECK_EQ(s.tft.dc, 25);
    CHECK_EQ(s.tft.reset, 4);
    CHECK_EQ(s.tft.backlight, PIPOS_PIN_NONE);
    CHECK_EQ(s.tft.hz, 10000000);
    CHECK_EQ(s.tft.rotation, 90);
    CHECK_EQ(s.tft.bgr, 1);
    CHECK_EQ(s.tft.invert, 0);
    CHECK_EQ(s.tft_bad, 0);
    CHECK_STR(pipos_backend_name(PIPOS_BACKEND_HDMI), "hdmi");
    CHECK_STR(pipos_backend_name(PIPOS_BACKEND_TFT), "tft");
    CHECK_STR(pipos_backend_name(PIPOS_BACKEND_BOTH), "both");
    CHECK_STR(pipos_backend_name(3), "unknown");
}

TEST(display_backend_every_key)
{
    pipos_settings_t s;
    issues_t is;
    unsigned line = 0;
    const char *text =
        "[display]\n"
        "backend = BOTH\n"
        "tft_cs = 1\n"
        "tft_dc = 12\n"
        "tft_reset = none\n"
        "tft_backlight = 13\n"
        "tft_hz = 32000000\n"
        "tft_rotation = 270\n"
        "tft_bgr = no\n"
        "tft_invert = on\n";

    CHECK_EQ(parse(text, &s, &is, &line), 0);
    CHECK_EQ(is.count, 0);
    CHECK_EQ(s.backend, PIPOS_BACKEND_BOTH);
    CHECK_EQ(s.tft.cs, 1);
    CHECK_EQ(s.tft.dc, 12);
    CHECK_EQ(s.tft.reset, PIPOS_PIN_NONE);
    CHECK_EQ(s.tft.backlight, 13);
    CHECK_EQ(s.tft.hz, 32000000);
    CHECK_EQ(s.tft.rotation, 270);
    CHECK_EQ(s.tft.bgr, 0);
    CHECK_EQ(s.tft.invert, 1);
    CHECK_EQ(s.tft_bad, 0);
    CHECK_EQ(parse("[display]\nbackend = tft\ntft_rotation = 0\n", &s, &is, &line), 0);
    CHECK_EQ(s.backend, PIPOS_BACKEND_TFT);
    CHECK_EQ(s.tft.rotation, 0);
    CHECK_EQ(parse("[display]\nbackend = hdmi\ntft_rotation = 180\n", &s, &is, &line), 0);
    CHECK_EQ(s.backend, PIPOS_BACKEND_HDMI);
    CHECK_EQ(s.tft.rotation, 180);
}

TEST(display_backend_bad_values)
{
    pipos_settings_t s;
    issues_t is;
    unsigned line = 0;
    const char *text =
        "[display]\n"
        "backend = spi\n"           /* 2 */
        "tft_cs = 2\n"              /* 3 */
        "tft_dc = none\n"           /* 4: DC has no "none" */
        "tft_reset = 28\n"          /* 5: past the header */
        "tft_backlight = -1\n"      /* 6 */
        "tft_rotation = 45\n"       /* 7 */
        "tft_rotation = 360\n"      /* 8 */
        "tft_bgr = maybe\n"         /* 9 */
        "tft_invert = 2\n"          /* 10 */
        "tft_hz = fast\n";          /* 11 */

    CHECK_EQ(parse(text, &s, &is, &line), 0);
    CHECK_EQ(is.count, 10);
    CHECK_EQ(s.tft_bad, 10);
    CHECK_STR(is.text[0], "badvalue display.backend 2");
    CHECK_STR(is.text[1], "badvalue display.tft_cs 3");
    CHECK_STR(is.text[2], "badvalue display.tft_dc 4");
    CHECK_STR(is.text[3], "badvalue display.tft_reset 5");
    CHECK_STR(is.text[7], "badvalue display.tft_bgr 9");
    CHECK_EQ(s.backend, PIPOS_BACKEND_HDMI);
    CHECK_EQ(s.tft.cs, 0);
    CHECK_EQ(s.tft.dc, 25);
    CHECK_EQ(s.tft.reset, 4);
    CHECK_EQ(s.tft.backlight, PIPOS_PIN_NONE);
    CHECK_EQ(s.tft.rotation, 90);
    CHECK_EQ(s.tft.bgr, 1);
    CHECK_EQ(s.tft.invert, 0);
    CHECK_EQ(s.tft.hz, 10000000);
}

TEST(display_tft_hz_clamps)
{
    pipos_settings_t s;
    issues_t is;
    unsigned line = 0;

    CHECK_EQ(parse("[display]\ntft_hz = 1000000\n", &s, &is, &line), 0);
    CHECK_EQ(is.count, 0);
    CHECK_EQ(s.tft.hz, 1000000);
    CHECK_EQ(parse("[display]\ntft_hz = 64000000\n", &s, &is, &line), 0);
    CHECK_EQ(is.count, 0);
    CHECK_EQ(s.tft.hz, 64000000);
    CHECK_EQ(parse("[display]\ntft_hz = 999999\n", &s, &is, &line), 0);
    CHECK_EQ(is.count, 1);
    CHECK_EQ(s.tft_bad, 1);
    CHECK_EQ(s.tft.hz, 1000000);
    CHECK_EQ(parse("[display]\ntft_hz = 64000001\n", &s, &is, &line), 0);
    CHECK_EQ(is.count, 1);
    CHECK_EQ(s.tft.hz, 64000000);
    CHECK_EQ(parse("[display]\ntft_hz = 0\n", &s, &is, &line), 0);
    CHECK_EQ(s.tft.hz, 1000000);
}

/* Every pin the input layer, audio and the buses use is refused by name. */
TEST(display_pins_in_use_are_refused)
{
    static const int input[] = { 5, 6, 17, 22, 26, 27, 23, 24, 16 };
    static const int audio[] = { 18 };
    static const int bus[] = { 2, 3, 7, 8, 9, 10, 11, 14, 15 };
    static const char *const keys[] = { "tft_dc", "tft_reset", "tft_backlight" };
    pipos_settings_t s;
    issues_t is;
    unsigned line = 0;
    unsigned i, k;
    char text[96], want[64];
    int free_pins = 0;

    for (k = 0; k < 3; k++) {
        for (i = 0; i < sizeof(input) / sizeof(input[0]); i++) {
            snprintf(text, sizeof(text), "[display]\n%s = %d\n", keys[k], input[i]);
            CHECK_EQ(parse(text, &s, &is, &line), 0);
            CHECK_EQ(is.count, 1);
            snprintf(want, sizeof(want), "pin_input display.%s 2", keys[k]);
            CHECK_STR(is.text[0], want);
        }
        for (i = 0; i < sizeof(bus) / sizeof(bus[0]); i++) {
            snprintf(text, sizeof(text), "[display]\n%s = %d\n", keys[k], bus[i]);
            CHECK_EQ(parse(text, &s, &is, &line), 0);
            snprintf(want, sizeof(want), "pin_bus display.%s 2", keys[k]);
            CHECK_STR(is.text[0], want);
        }
        snprintf(text, sizeof(text), "[display]\n%s = %d\n", keys[k], audio[0]);
        CHECK_EQ(parse(text, &s, &is, &line), 0);
        snprintf(want, sizeof(want), "pin_audio display.%s 2", keys[k]);
        CHECK_STR(is.text[0], want);
        CHECK_EQ(s.tft.dc, 25);
        CHECK_EQ(s.tft.reset, 4);
        CHECK_EQ(s.tft.backlight, PIPOS_PIN_NONE);
    }
    CHECK_STR(pipos_tft_pin_refusal(40), "badvalue");
    CHECK_STR(pipos_tft_pin_refusal(-1), "badvalue");
    for (i = 0; i <= 27; i++) {
        free_pins += pipos_tft_pin_refusal((int)i) == NULL;
    }
    /* 0, 1, 4, 12, 13, 19, 20, 21, 25 */
    CHECK_EQ(free_pins, 9);
}

TEST(display_pins_must_differ)
{
    pipos_settings_t s;
    issues_t is;
    unsigned line = 0;

    CHECK_EQ(parse("[display]\nbackend = tft\ntft_backlight = 4\n", &s, &is, &line), 0);
    CHECK_EQ(is.count, 1);
    CHECK_STR(is.text[0], "pin_twice display.tft_backlight 0");
    CHECK_EQ(s.tft.backlight, PIPOS_PIN_NONE);
    CHECK_EQ(s.backend, PIPOS_BACKEND_TFT);
    CHECK_EQ(parse("[display]\nbackend = both\ntft_dc = 4\n", &s, &is, &line), 0);
    CHECK_EQ(is.count, 1);
    CHECK_STR(is.text[0], "pin_twice display.tft_reset 0");
    CHECK_EQ(s.backend, PIPOS_BACKEND_HDMI);
    CHECK_EQ(s.tft_bad, 1);
    CHECK_EQ(parse("[display]\nbackend = both\ntft_dc = 4\ntft_reset = none\n", &s, &is, &line),
             0);
    CHECK_EQ(is.count, 0);
    CHECK_EQ(s.backend, PIPOS_BACKEND_BOTH);
}

TEST(display_test_key_is_unknown_when_shipped)
{
    pipos_settings_t s;
    issues_t is;
    unsigned line = 0;

    CHECK_EQ(parse("[display]\ntft_test = panel\n", &s, &is, &line), 0);
    CHECK_EQ(is.count, 1);
    CHECK_STR(is.text[0], "unknown display.tft_test 2");
}
