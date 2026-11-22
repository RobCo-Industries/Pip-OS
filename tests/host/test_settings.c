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
