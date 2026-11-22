/*
 * Kernel settings and the parser of /PIPBOY/PIPOS.CFG. Pure code: it
 * builds and runs on the host in the unit tests.
 */
#ifndef PIPOS_SETTINGS_H
#define PIPOS_SETTINGS_H

#include <stddef.h>
#include <stdint.h>

#define PIPOS_DELAY_MAX_MS  2000u
#define PIPOS_VOLUME_MAX    100u

typedef struct {
    unsigned scheme;        /* CONSOLE_SCHEME_* number, 0 to 3 */
    uint32_t line_delay_ms; /* pause between boot lines */
    unsigned volume;        /* audio volume, 0 to 100 */
    int beep;               /* 1: beep at power-on */
} pipos_settings_t;

/*
 * Called once per problem: rc is "badvalue" or "unknown", key is
 * "section.key", line counts from 1.
 */
typedef void (*pipos_config_report_t)(const char *rc, const char *key, unsigned line,
                                      void *ctx);

void pipos_settings_default(pipos_settings_t *s);

/*
 * Applies the keys in text to *s. Unknown keys and bad values are
 * reported and skipped. On a syntax error nothing is applied: *s keeps
 * its values and the result is the ini_parse() error, with the line in
 * *err_line. Returns 0 otherwise. report may be NULL.
 */
int pipos_settings_parse(const char *text, size_t len, pipos_settings_t *s,
                         pipos_config_report_t report, void *ctx, unsigned *err_line);

/* Scheme number for a name (green, amber, blue, white; any case), or -1. */
int pipos_scheme_from_name(const char *name);
/* Name of a scheme number, or "unknown". */
const char *pipos_scheme_name(unsigned scheme);

#endif
