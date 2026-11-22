#include "settings.h"

#include <robco/ini.h>
#include <robco/klibc.h>

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

void pipos_settings_default(pipos_settings_t *s)
{
    s->scheme = 0;
    s->line_delay_ms = 0;
    s->volume = PIPOS_VOLUME_MAX;
    s->beep = 1;
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

static int on_key(const char *section, const char *key, const char *value, unsigned line,
                  void *ctx)
{
    parse_ctx_t *p = ctx;
    uint32_t n;
    int v;
    int ok = 1;

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
    } else if (k_strcmp(section, "audio") == 0 && k_strcmp(key, "volume") == 0) {
        if (parse_uint(value, PIPOS_VOLUME_MAX, &n) == 0) {
            p->s.volume = (unsigned)n;
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
    *s = p.s;
    return 0;
}
