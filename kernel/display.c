/*
 * The SPI panel backend. The kernel keeps the last frame it sent in a
 * shadow buffer and sends only the smallest rectangle that changed.
 * See docs/ARCHITECTURE.md, "Display path".
 */
#include <robco/console.h>
#include <robco/font.h>
#include <robco/input.h>
#include <robco/klibc.h>
#include <robco/tft.h>
#include <robco/timer.h>

#include "display.h"
#include "gfx_draw.h"
#include "pipos.h"

#ifdef PIPOS_TFT_TEST
#include <robco/crc32.h>
#endif

#define FRAME_W 320u
#define FRAME_H 240u

static unsigned backend = PIPOS_BACKEND_HDMI;  /* what is in use */
static int panel;               /* the panel takes frames */
static int full;                /* the next send covers the whole frame */
static int failed;              /* panel calls that failed at run time */
static int dark;                /* blanked, or a wake that failed */
static uint32_t panel_w, panel_h;   /* the part of the frame the panel shows */
static uint32_t *const shadow = (uint32_t *)(uintptr_t)GFX_SHADOW_BASE;

static char text[PIPOS_PANEL_ROWS][PIPOS_PANEL_COLS + 1];
static unsigned rows;           /* lines held, at most PIPOS_PANEL_ROWS */

#ifdef PIPOS_TFT_TEST
/*
 * Every byte sent since tft_init(), in runs of one DC level: a word with
 * dc << 31 | length, then the bytes, padded to a word.
 */
static uint32_t *tap_run;       /* header of the current run, or NULL */
static uint8_t *tap_end = (uint8_t *)(uintptr_t)GFX_TAP_BASE;
static int tap_full;
static uint32_t send_bytes, send_crc;

static void tap(int dc, const uint8_t *bytes, size_t len)
{
    uint8_t pair[2];
    size_t i;

    pair[0] = (uint8_t)dc;
    for (i = 0; i < len; i++) {
        pair[1] = bytes[i];
        send_crc = crc32_update(send_crc, pair, 2);
    }
    send_bytes += (uint32_t)len;
    if (tap_full) {
        return;
    }
    if (tap_run == NULL || (int)(*tap_run >> 31) != dc) {
        tap_end = (uint8_t *)(((uintptr_t)tap_end + 3u) & ~(uintptr_t)3u);
        if ((uintptr_t)tap_end + 4u > GFX_TAP_END) {
            tap_full = 1;
            return;
        }
        tap_run = (uint32_t *)tap_end;
        *tap_run = (uint32_t)dc << 31;
        tap_end += 4;
    }
    if ((uintptr_t)tap_end + len > GFX_TAP_END) {
        tap_full = 1;
        return;
    }
    k_memcpy(tap_end, bytes, len);
    tap_end += len;
    *tap_run += (uint32_t)len;
}

static uint32_t send_t0;

static void send_begin(void)
{
    send_bytes = 0;
    send_crc = 0;
    send_t0 = timer_now_lo();
}

/*
 * One marker per send. Then, when the bytes took less time than they
 * take on the wire at the rate set, a wait for the rest: QEMU moves them
 * faster than a panel would.
 */
static void send_end(const char *why, const gfx_rect_t *r)
{
    uint32_t hz100k = tft_get_hz() / 100000u;
    uint32_t wire = hz100k > 0 ? send_bytes * 80u / hz100k : 0;
    uint32_t spent = timer_now_lo() - send_t0;

    k_printf("@RC stage=" PIPOS_STAGE " ev=tft_send why=%s x=%u y=%u w=%u h=%u bytes=%u"
             " crc=%08x\n", why, (unsigned)r->x, (unsigned)r->y, (unsigned)r->w,
             (unsigned)r->h, (unsigned)send_bytes, (unsigned)send_crc);
    if (wire > spent) {
        timer_delay_us(wire - spent);
    }
}

static void put_hex(input_script_reply_t *reply, char *line, size_t *n, uint32_t v,
                    unsigned digits)
{
    static const char hex[] = "0123456789abcdef";

    while (digits-- > 0) {
        line[(*n)++] = hex[(v >> (4u * digits)) & 15u];
    }
    (void)reply;
}

static void flush_line(input_script_reply_t *reply, char *line, size_t *n, char kind)
{
    if (*n > 1) {
        line[(*n)++] = '\n';
        line[*n] = '\0';
        reply->write(line);
    }
    line[0] = kind;
    *n = 1;
}

/*
 * The transcript as text: "C<hex>" lines hold command bytes, "D" lines
 * data, as 16-bit words "hhhh" or "hhhh*<count>" in hex and one "=hh"
 * for an odd last byte, separated by spaces. Then "end bytes=<n> full=<0|1>".
 */
int pipos_display_dump(input_script_reply_t *reply)
{
    const uint8_t *p = (const uint8_t *)(uintptr_t)GFX_TAP_BASE;
    char line[112];
    size_t n = 0;
    uint32_t total = 0;

    while (p + 4 <= tap_end) {
        uint32_t head = *(const uint32_t *)(uintptr_t)p;
        uint32_t len = head & 0x7fffffffu;
        const uint8_t *b = p + 4;
        uint32_t i = 0;

        total += len;
        if ((head >> 31) == 0) {
            flush_line(reply, line, &n, 'C');
            for (; i < len; i++) {
                if (n > 90) {
                    flush_line(reply, line, &n, 'C');
                }
                put_hex(reply, line, &n, b[i], 2);
            }
        } else {
            flush_line(reply, line, &n, 'D');
            while (i + 1 < len) {
                uint32_t w = (uint32_t)b[i] << 8 | b[i + 1];
                uint32_t count = 1;

                while (i + 2 * count + 1 < len &&
                       ((uint32_t)b[i + 2 * count] << 8 | b[i + 2 * count + 1]) == w) {
                    count++;
                }
                if (n > 80) {
                    flush_line(reply, line, &n, 'D');
                }
                line[n++] = ' ';
                put_hex(reply, line, &n, w, 4);
                if (count > 1) {
                    line[n++] = '*';
                    put_hex(reply, line, &n, count, 8);
                }
                i += 2 * count;
            }
            if (i < len) {
                line[n++] = ' ';
                line[n++] = '=';
                put_hex(reply, line, &n, b[i], 2);
            }
        }
        p = b + ((len + 3u) & ~3u);
    }
    flush_line(reply, line, &n, 'C');
    k_snprintf(line, sizeof(line), "end bytes=%u full=%d\n", (unsigned)total, tap_full);
    reply->write(line);
    return 0;
}
#else
static void send_begin(void)
{
}

static void send_end(const char *why, const gfx_rect_t *r)
{
    (void)why, (void)r;
}

int pipos_display_dump(struct input_script_reply *reply)
{
    (void)reply;
    return -1;
}
#endif

/* A panel call that failed while the kernel runs: counted, reported once. */
static void fail(const char *op, int rc)
{
    if (failed++ == 0) {
        k_printf("@RC stage=" PIPOS_STAGE " ev=display_error op=%s rc=%d\n", op, rc);
    }
}

int pipos_display_hdmi(void)
{
    return backend != PIPOS_BACKEND_TFT;
}

int pipos_display_panel(void)
{
    return panel;
}

void pipos_display_full(void)
{
    full = 1;
}

int32_t pipos_display_send(const uint32_t *bb)
{
    gfx_rect_t r = { 0, 0, panel_w, panel_h };
    const char *why = full ? "full" : "diff";
    int rc;

    if (!panel) {
        return 0;
    }
    send_begin();
    /* Only the part the panel shows counts: changes outside it are never sent. */
    if (!full && !gfx_dirty_rect(bb, shadow, FRAME_W, panel_w, panel_h, &r)) {
        r.w = r.h = 0;
    }
    if (r.w == 0 || r.h == 0) {
        r.x = r.y = r.w = r.h = 0;
        send_end(why, &r);
        return 0;
    }
    rc = tft_present_px32_rect(bb, FRAME_W * 4u, TFT_SRC_XRGB, r.x, r.y, r.w, r.h);
    send_end(why, &r);
    if (rc != 0) {
        /* Part of the rectangle may be on the panel: one more try, whole frame. */
        fail("present", rc);
        r.x = r.y = 0;
        r.w = panel_w;
        r.h = panel_h;
        send_begin();
        rc = tft_present_px32_rect(bb, FRAME_W * 4u, TFT_SRC_XRGB, 0, 0, r.w, r.h);
        send_end("retry", &r);
    }
    if (rc != 0) {
        fail("present", rc);
        full = 1;
        return -PIP_EIO;
    }
    gfx_copy_rect(bb, shadow, FRAME_W, &r);
    full = 0;
    return 0;
}

void pipos_display_blank(int on)
{
    int rc;

    if (!panel) {
        return;
    }
    if (on) {
        dark = 1;
        rc = tft_display(0);
        if (rc == 0) {
            rc = tft_sleep(1);
        }
    } else {
        rc = tft_sleep(0);
        if (rc == 0) {
            rc = tft_display(1);
        }
        dark = rc != 0;
        full = 1;
    }
    if (rc != 0) {
        fail(on ? "blank" : "wake", rc);
    }
}

/* Draws the held lines into the console frame and sends the change. */
void pipos_display_console(void)
{
    static gfx_surface_t s;
    const console_scheme_t *sc = console_scheme_info(console_scheme());
    uint32_t *bb = (uint32_t *)(uintptr_t)GFX_CONSOLE_BASE;
    unsigned i;

    if (!panel) {
        return;
    }
    gfx_surface_init(&s, bb, (int32_t)FRAME_W, (int32_t)FRAME_H, font8x16_glyph);
    gfx_state_reset(&s, (uint32_t)sc->normal.r << 16 | (uint32_t)sc->normal.g << 8 |
                        sc->normal.b);
    gfx_clear(&s, (uint32_t)sc->bg.r << 16 | (uint32_t)sc->bg.g << 8 | sc->bg.b);
    for (i = 0; i < rows; i++) {
        (void)gfx_text(&s, 0, (int32_t)(i * 16u), text[i], k_strlen(text[i]), 1);
    }
    (void)pipos_display_send(bb);
}

static void add_line(const char *t)
{
    if (rows == PIPOS_PANEL_ROWS) {
        k_memmove(text[0], text[1], sizeof(text) - sizeof(text[0]));
        rows--;
    }
    k_snprintf(text[rows++], PIPOS_PANEL_COLS + 1, "%s", t);
}

void pipos_display_line(const char *t)
{
    add_line(t);
    pipos_display_console();
}

/*
 * A panic after power blanked the screen must still show: wake the panel
 * first, then send the whole console frame. tft_sleep() returns at once
 * when the timer has stopped, and every SPI wait has a deadline, so this
 * stays bounded. A failed call skips the rest; the frame still goes out.
 */
void pipos_display_panic(const char *why)
{
    gfx_rect_t none = { 0, 0, 0, 0 };
    int rc;

    if (panel && dark) {
        send_begin();
        rc = tft_sleep(0);
        if (rc == 0) {
            rc = tft_display(1);
        }
        send_end("wake", &none);
        if (rc != 0) {
            fail("wake", rc);
        }
        full = 1;
    }
    add_line("");
    add_line("KERNEL PANIC:");
    add_line(why);
    add_line("SYSTEM HALTED");
    pipos_display_console();
}

/* What tft_init() gets: the settings, or in a test build a config it refuses. */
static void tft_config(const pipos_settings_t *s, tft_config_t *c)
{
    c->cs = s->tft.cs;
    c->hz = s->tft.hz;
    c->dc_pin = s->tft.dc;
    c->reset_pin = s->tft.reset;
    c->backlight_pin = s->tft.backlight;
    c->rotation = s->tft.rotation;
    c->bgr = s->tft.bgr;
    c->invert = s->tft.invert;
#ifdef PIPOS_TFT_TEST
    if (s->tft.test == PIPOS_TFT_TEST_FAIL) {
        c->rotation = 45;
    }
#endif
}

void pipos_display_start(const pipos_settings_t *s)
{
    tft_config_t cfg;
    const char *state = "err";
    uint32_t id = 0;
    int rc;

    if (s->backend == PIPOS_BACKEND_HDMI) {
        return;
    }
    tft_config(s, &cfg);
#ifdef PIPOS_TFT_TEST
    tft_set_tap(tap);
#endif
    rc = tft_init(&cfg);
    /* A bus error can be a one-off: one more try. */
    if (rc == TFT_ERR_BUS) {
        rc = tft_init(&cfg);
    }
    if (rc == 0) {
        /* No answer can also be a module without MISO: the panel stays in use. */
        state = tft_read_id(&id) == 0 ? "ok" : "absent";
#ifdef PIPOS_TFT_TEST
        if (s->tft.test == PIPOS_TFT_TEST_PANEL) {
            state = "ok";
        }
#endif
        panel = 1;
        full = 1;
        backend = s->backend;
        panel_w = tft_width() < FRAME_W ? tft_width() : FRAME_W;
        panel_h = tft_height() < FRAME_H ? tft_height() : FRAME_H;
        /* An absent panel may be a broken one: HDMI keeps the frames too. */
        if (backend == PIPOS_BACKEND_TFT && state[0] == 'a') {
            backend = PIPOS_BACKEND_BOTH;
        }
    }
    k_printf("@RC stage=" PIPOS_STAGE " ev=display backend=%s tft=%s rc=%d hz=%u asked=%s"
             " bad=%u\n", pipos_backend_name(backend), state, rc, (unsigned)tft_get_hz(),
             pipos_backend_name(s->backend), s->tft_bad);
    if (rc != 0) {
        pipos_screen_line("SPI DISPLAY FAILED, USING HDMI");
    }
}
