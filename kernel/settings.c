#include "settings.h"

#include <robco/holo.h>
#include <robco/ini.h>
#include <robco/klibc.h>

#include "keyclick.h"
#include "robco_config.h"

static const char *const scheme_names[] = { "green", "amber", "blue", "white" };
#define SCHEME_COUNT (sizeof(scheme_names) / sizeof(scheme_names[0]))

typedef struct {
    pipos_settings_t s;
    pipos_config_report_t report;
    void *ctx;
} parse_ctx_t;

static int lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

static int same_word(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        if (lower((unsigned char)*a) != lower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/* Unsigned decimal, 1 to 9 digits, at most max. Returns 0 or -1. */
static int parse_uint(const char *text, uint32_t max, uint32_t *out)
{
    uint32_t v = 0;
    unsigned digits = 0;

    if (*text == '+') {
        text++;
    }
    for (; *text != '\0'; text++) {
        if (*text < '0' || *text > '9' || ++digits > 9) {
            return -1;
        }
        v = v * 10u + (uint32_t)(*text - '0');
    }
    if (digits == 0 || v > max) {
        return -1;
    }
    *out = v;
    return 0;
}

/* The boolean words of ini_get_bool(). Returns 0 or -1. */
static int parse_bool(const char *text, int *out)
{
    static const char *const yes[] = { "true", "yes", "on", "1" };
    static const char *const no[] = { "false", "no", "off", "0" };
    unsigned i;

    for (i = 0; i < 4; i++) {
        if (same_word(text, yes[i])) {
            *out = 1;
            return 0;
        }
        if (same_word(text, no[i])) {
            *out = 0;
            return 0;
        }
    }
    return -1;
}

int pipos_scheme_from_name(const char *name)
{
    unsigned i;

    for (i = 0; i < SCHEME_COUNT; i++) {
        if (same_word(name, scheme_names[i])) {
            return (int)i;
        }
    }
    return -1;
}

const char *pipos_scheme_name(unsigned scheme)
{
    return scheme < SCHEME_COUNT ? scheme_names[scheme] : "unknown";
}

static const char *const backend_names[] = { "hdmi", "tft", "both" };

const char *pipos_backend_name(unsigned backend)
{
    return backend < 3u ? backend_names[backend] : "unknown";
}

/* Pins the kernel's input layer uses: kernel/robco_config.h. */
static const int input_pins[] = {
    PIPOS_PIN_UP, PIPOS_PIN_DOWN, PIPOS_PIN_LEFT, PIPOS_PIN_RIGHT, PIPOS_PIN_SELECT,
    PIPOS_PIN_BACK, PIPOS_PIN_DIAL_A, PIPOS_PIN_DIAL_B, PIPOS_PIN_DIAL_PUSH,
};

/* RobCo-Core's BOARD_SUPPORT.md: PWM audio on GPIO 18 (Zero) or 40, 41, 45. */
static const int audio_pins[] = { 18, 40, 41, 45 };

/* I2C 2 and 3 for the clock chip, SPI0 7 to 11, the PL011 on 14 and 15. */
static const int bus_pins[] = { 2, 3, 7, 8, 9, 10, 11, 14, 15 };

static int in_list(int pin, const int *list, unsigned n)
{
    unsigned i;

    for (i = 0; i < n; i++) {
        if (list[i] == pin) {
            return 1;
        }
    }
    return 0;
}

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

const char *pipos_tft_pin_refusal(int pin)
{
    if (pin < 0 || pin > PIPOS_GPIO_MAX) {
        return "badvalue";
    }
    if (in_list(pin, input_pins, COUNT(input_pins))) {
        return "pin_input";
    }
    if (in_list(pin, audio_pins, COUNT(audio_pins))) {
        return "pin_audio";
    }
    if (in_list(pin, bus_pins, COUNT(bus_pins))) {
        return "pin_bus";
    }
    return NULL;
}

void pipos_settings_default(pipos_settings_t *s)
{
    k_memset(s, 0, sizeof(*s));
    s->scheme = 0;
    s->line_delay_ms = 0;
    s->volume = PIPOS_VOLUME_MAX;
    s->beep = 1;
    s->char_delay_ms = 0;
    s->key_click = 0;
    s->backend = PIPOS_BACKEND_HDMI;
    s->tft.cs = 0;
    s->tft.dc = 25;
    s->tft.reset = 4;
    s->tft.backlight = PIPOS_PIN_NONE;
    s->tft.hz = PIPOS_TFT_HZ_DEF;
    s->tft.rotation = 90;
    s->tft.bgr = 1;
    s->tft.invert = 0;
}

static void report(parse_ctx_t *p, const char *rc, const char *section, const char *key,
                   unsigned line)
{
    char full[2 * (INI_MAX_KEY + 1)];

    if (p->report == NULL) {
        return;
    }
    k_snprintf(full, sizeof(full), "%s.%s", section, key);
    p->report(rc, full, line, p->ctx);
}

/*
 * A panel pin: "none" when allowed, else a GPIO that no other part of
 * the kernel uses. Returns NULL and sets *out, or the report rc.
 */
static const char *parse_pin(const char *value, int allow_none, int *out)
{
    uint32_t n;
    const char *why;

    if (allow_none && same_word(value, "none")) {
        *out = PIPOS_PIN_NONE;
        return NULL;
    }
    if (parse_uint(value, PIPOS_GPIO_MAX, &n) != 0) {
        return "badvalue";
    }
    why = pipos_tft_pin_refusal((int)n);
    if (why == NULL) {
        *out = (int)n;
    }
    return why;
}

/* [display] keys of the panel. Returns 1 when key is one of them. */
static int on_tft_key(parse_ctx_t *p, const char *key, const char *value, const char **why)
{
    pipos_tft_settings_t *t = &p->s.tft;
    uint32_t n;
    int v;

    *why = NULL;
    if (k_strcmp(key, "backend") == 0) {
        for (n = 0; n < 3u; n++) {
            if (same_word(value, backend_names[n])) {
                p->s.backend = n;
                return 1;
            }
        }
        *why = "badvalue";
    } else if (k_strcmp(key, "tft_cs") == 0) {
        if (parse_uint(value, 1, &n) == 0) {
            t->cs = n;
        } else {
            *why = "badvalue";
        }
    } else if (k_strcmp(key, "tft_dc") == 0) {
        *why = parse_pin(value, 0, &t->dc);
    } else if (k_strcmp(key, "tft_reset") == 0) {
        *why = parse_pin(value, 1, &t->reset);
    } else if (k_strcmp(key, "tft_backlight") == 0) {
        *why = parse_pin(value, 1, &t->backlight);
    } else if (k_strcmp(key, "tft_hz") == 0) {
        /* A number out of range is clamped to it, and still reported. */
        if (parse_uint(value, 999999999u, &n) != 0) {
            *why = "badvalue";
        } else if (n < PIPOS_TFT_HZ_MIN || n > PIPOS_TFT_HZ_MAX) {
            t->hz = n < PIPOS_TFT_HZ_MIN ? PIPOS_TFT_HZ_MIN : PIPOS_TFT_HZ_MAX;
            *why = "badvalue";
        } else {
            t->hz = n;
        }
    } else if (k_strcmp(key, "tft_rotation") == 0) {
        if (parse_uint(value, 270, &n) == 0 && n % 90u == 0) {
            t->rotation = n;
        } else {
            *why = "badvalue";
        }
    } else if (k_strcmp(key, "tft_bgr") == 0 || k_strcmp(key, "tft_invert") == 0) {
        if (parse_bool(value, &v) != 0) {
            *why = "badvalue";
        } else if (key[4] == 'b') {
            t->bgr = v;
        } else {
            t->invert = v;
        }
#ifdef PIPOS_TFT_TEST
    } else if (k_strcmp(key, "tft_test") == 0) {
        static const char *const modes[] = { "none", "panel", "fail" };

        for (n = 0; n < 3u; n++) {
            if (same_word(value, modes[n])) {
                t->test = n;
                return 1;
            }
        }
        *why = "badvalue";
#endif
    } else {
        return 0;
    }
    return 1;
}

static int on_key(const char *section, const char *key, const char *value, unsigned line,
                  void *ctx)
{
    parse_ctx_t *p = ctx;
    uint32_t n;
    int v;
    int ok = 1;
    const char *why;

    if (k_strcmp(section, "display") == 0 && on_tft_key(p, key, value, &why)) {
        if (why != NULL) {
            p->s.tft_bad++;
            report(p, why, section, key, line);
        }
        return 0;
    }

    if (k_strcmp(section, "display") == 0 && k_strcmp(key, "scheme") == 0) {
        v = pipos_scheme_from_name(value);
        if (v >= 0) {
            p->s.scheme = (unsigned)v;
        } else {
            ok = 0;
        }
    } else if (k_strcmp(section, "display") == 0 && k_strcmp(key, "line_delay_ms") == 0) {
        if (parse_uint(value, PIPOS_DELAY_MAX_MS, &n) == 0) {
            p->s.line_delay_ms = n;
        } else {
            ok = 0;
        }
    } else if (k_strcmp(section, "display") == 0 && k_strcmp(key, "char_delay_ms") == 0) {
        if (parse_uint(value, PIPOS_CHAR_DELAY_MAX_MS, &n) == 0) {
            p->s.char_delay_ms = n;
        } else {
            ok = 0;
        }
    } else if (k_strcmp(section, "audio") == 0 && k_strcmp(key, "key_click") == 0) {
        if (parse_bool(value, &v) == 0) {
            p->s.key_click = v;
        } else {
            ok = 0;
        }
    } else if (k_strcmp(section, "audio") == 0 && k_strcmp(key, "volume") == 0) {
        if (parse_uint(value, PIPOS_VOLUME_MAX, &n) == 0) {
            p->s.volume = (unsigned)n;
        } else {
            ok = 0;
        }
    } else if (k_strcmp(section, "holotape") == 0 && k_strcmp(key, "slot") == 0) {
        if (same_word(value, "on") || same_word(value, "off")) {
            p->s.holo_slot = same_word(value, "on");
        } else {
            ok = 0;
        }
    } else if (k_strcmp(section, "audio") == 0 && k_strcmp(key, "beep") == 0) {
        if (parse_bool(value, &v) == 0) {
            p->s.beep = v;
        } else {
            ok = 0;
        }
    } else {
        report(p, "unknown", section, key, line);
        return 0;
    }
    if (!ok) {
        report(p, "badvalue", section, key, line);
    }
    return 0;
}

/*
 * DC, reset and backlight need three different pins. A clash is reported
 * on the later key of the pair, which goes back to "none"; a clash with
 * DC, which has no "none", turns the panel off: the backend goes to hdmi.
 */
static void check_pins_apart(parse_ctx_t *p)
{
    pipos_tft_settings_t *t = &p->s.tft;

    if (t->reset != PIPOS_PIN_NONE && t->reset == t->dc) {
        report(p, "pin_twice", "display", "tft_reset", 0);
        p->s.tft_bad++;
        p->s.backend = PIPOS_BACKEND_HDMI;
    }
    if (t->backlight != PIPOS_PIN_NONE && t->backlight == t->dc) {
        report(p, "pin_twice", "display", "tft_backlight", 0);
        p->s.tft_bad++;
        p->s.backend = PIPOS_BACKEND_HDMI;
    }
    if (t->backlight != PIPOS_PIN_NONE && t->backlight == t->reset) {
        report(p, "pin_twice", "display", "tft_backlight", 0);
        p->s.tft_bad++;
        t->backlight = PIPOS_PIN_NONE;
    }
}

/* The slot runs with RobCo-Core's HOLO_CONFIG_DEFAULT: kernel/slot_holo.c. */
static const holo_config_t slot_pins = HOLO_CONFIG_DEFAULT;

/* SPI0's CE0 and CE1 pins: a panel chip select index names one of them. */
static int ce_pin(unsigned cs)
{
    return cs == 0u ? 8 : 7;
}

static int slot_pin(int pin)
{
    return pin == slot_pins.cd_pin || pin == slot_pins.wp_pin || pin == slot_pins.pwr_pin ||
           pin == slot_pins.bus_pin || pin == slot_pins.cs_pin;
}

const char *pipos_slot_clash(const pipos_settings_t *s, unsigned which)
{
    const pipos_tft_settings_t *t = &s->tft;
    unsigned n = 0;

    /* The kernel reads the panel keys only for tft and both. */
    if (s->backend == PIPOS_BACKEND_HDMI) {
        return NULL;
    }
    if ((t->cs == slot_pins.spi_cs || slot_pin(ce_pin(t->cs))) && n++ == which) {
        return "tft_cs";
    }
    if (slot_pin(t->dc) && n++ == which) {
        return "tft_dc";
    }
    if (t->reset != PIPOS_PIN_NONE && slot_pin(t->reset) && n++ == which) {
        return "tft_reset";
    }
    if (t->backlight != PIPOS_PIN_NONE && slot_pin(t->backlight) && n++ == which) {
        return "tft_backlight";
    }
    return NULL;
}

/* A slot that clashes with the panel stays off: one report per panel key. */
static void check_slot_apart(parse_ctx_t *p)
{
    const char *key;
    unsigned i;

    if (!p->s.holo_slot) {
        return;
    }
    for (i = 0; (key = pipos_slot_clash(&p->s, i)) != NULL; i++) {
        report(p, "slot_clash", "display", key, 0);
        p->s.holo_slot = 0;
    }
}

int pipos_settings_parse(const char *text, size_t len, pipos_settings_t *s,
                         pipos_config_report_t report_fn, void *ctx, unsigned *err_line)
{
    parse_ctx_t p;
    int rc;

    p.s = *s;
    p.report = report_fn;
    p.ctx = ctx;
    rc = ini_parse(text, len, on_key, &p, err_line);
    if (rc != 0) {
        return rc;
    }
    check_pins_apart(&p);
    check_slot_apart(&p);
    *s = p.s;
    return 0;
}
