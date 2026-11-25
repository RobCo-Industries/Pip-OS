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

/* [display] backend. */
#define PIPOS_BACKEND_HDMI  0u
#define PIPOS_BACKEND_TFT   1u      /* the SPI panel only */
#define PIPOS_BACKEND_BOTH  2u      /* HDMI and the panel */

/* tft_reset and tft_backlight: no pin. Same value as TFT_PIN_NONE. */
#define PIPOS_PIN_NONE      (-1)
/* Highest GPIO on the 40-pin header. */
#define PIPOS_GPIO_MAX      27

/* The ranges of RobCo-Core's tft.h: TFT_HZ_MIN, TFT_HZ_MAX, TFT_DEFAULT_HZ. */
#define PIPOS_TFT_HZ_MIN    1000000u
#define PIPOS_TFT_HZ_MAX    64000000u
#define PIPOS_TFT_HZ_DEF    10000000u

/* The panel's wiring and options. Defaults: TFT_CONFIG_DEFAULT of tft.h. */
typedef struct {
    unsigned cs;            /* SPI0 chip select, 0 or 1 */
    int dc;                 /* data/command GPIO */
    int reset;              /* reset GPIO or PIPOS_PIN_NONE */
    int backlight;          /* backlight GPIO or PIPOS_PIN_NONE */
    uint32_t hz;            /* SPI clock asked for */
    unsigned rotation;      /* 0, 90, 180 or 270 */
    int bgr;
    int invert;
#ifdef PIPOS_TFT_TEST
    unsigned test;          /* PIPOS_TFT_TEST_*: test builds only */
#endif
} pipos_tft_settings_t;

#ifdef PIPOS_TFT_TEST
/* tft_test: what the test build makes of the panel. */
#define PIPOS_TFT_TEST_NONE  0u     /* as shipped: the id read decides */
#define PIPOS_TFT_TEST_PANEL 1u     /* the id read reports a panel */
#define PIPOS_TFT_TEST_FAIL  2u     /* tft_init() gets a config it refuses */
#endif

typedef struct {
    unsigned scheme;        /* CONSOLE_SCHEME_* number, 0 to 3 */
    uint32_t line_delay_ms; /* pause between boot lines */
    unsigned volume;        /* audio volume, 0 to 100 */
    int beep;               /* 1: beep at power-on */
    unsigned backend;       /* PIPOS_BACKEND_* */
    pipos_tft_settings_t tft;
    unsigned tft_bad;       /* backend and tft_* values refused, each reported once */
    int holo_slot;          /* [holotape] slot: 1 on, 0 off (the default) */
} pipos_settings_t;

/*
 * Called once per problem: rc is "badvalue", "unknown" or, for a panel
 * pin, "pin_input", "pin_audio", "pin_bus" or "pin_twice". key is
 * "section.key", line counts from 1 (0 for pin_twice, found after the
 * whole file).
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

/* Name of a PIPOS_BACKEND_* value: "hdmi", "tft", "both". */
const char *pipos_backend_name(unsigned backend);

/*
 * Why a GPIO cannot drive a panel line: NULL when it can, else the
 * report rc ("badvalue" past the header, "pin_input", "pin_audio",
 * "pin_bus").
 */
const char *pipos_tft_pin_refusal(int pin);

/* Scheme number for a name (green, amber, blue, white; any case), or -1. */
int pipos_scheme_from_name(const char *name);
/* Name of a scheme number, or "unknown". */
const char *pipos_scheme_name(unsigned scheme);

#endif
