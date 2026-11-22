/*
 * gfx_draw.c: every primitive, pixel exact. Small scenes compare with
 * pictures ('#' set, '.' clear); larger ones with a plain reference model.
 * Each surface sits between guard words that no call may touch.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx_draw.h"
#include "rc_test.h"

#define GUARD   64
#define CANARY  0xdeadbeefu
#define FG      0x00123456u

/* ---- surfaces --------------------------------------------------------- */

typedef struct {
    uint32_t *mem;              /* guard, w * h pixels, guard */
    gfx_surface_t s;
} canvas_t;

static uint32_t glyph_calls[256];
static uint8_t glyph_rows[16];

/* Row 0 is the character, row 15 its two edge pixels, the rest clear. */
static const uint8_t *fake_glyph(uint32_t ch)
{
    glyph_calls[ch & 0xff]++;
    memset(glyph_rows, 0, sizeof(glyph_rows));
    glyph_rows[0] = (uint8_t)ch;
    glyph_rows[15] = 0x81;
    return glyph_rows;
}

static canvas_t canvas(int32_t w, int32_t h)
{
    canvas_t c;
    size_t n = (size_t)w * (size_t)h, i;

    c.mem = malloc((n + 2 * GUARD) * sizeof(uint32_t));
    for (i = 0; i < n + 2 * GUARD; i++) {
        c.mem[i] = CANARY;
    }
    gfx_surface_init(&c.s, c.mem + GUARD, w, h, fake_glyph);
    gfx_clear(&c.s, 0);
    gfx_state_reset(&c.s, FG);
    memset(glyph_calls, 0, sizeof(glyph_calls));
    return c;
}

static int guards_ok(const canvas_t *c)
{
    size_t n = (size_t)c->s.w * (size_t)c->s.h, i;

    for (i = 0; i < GUARD; i++) {
        if (c->mem[i] != CANARY || c->mem[GUARD + n + i] != CANARY) {
            return 0;
        }
    }
    return 1;
}

static void done(canvas_t *c)
{
    CHECK(guards_ok(c));
    free(c->mem);
}

static uint32_t px(const canvas_t *c, int32_t x, int32_t y)
{
    return c->s.px[y * c->s.w + x];
}

static unsigned count_set(const canvas_t *c)
{
    unsigned n = 0;
    int32_t i;

    for (i = 0; i < c->s.w * c->s.h; i++) {
        n += c->s.px[i] != 0;
    }
    return n;
}

/* The canvas as rows of '#' (set) and '.' (clear), joined by '\n'. */
static char *picture(const canvas_t *c)
{
    static char buf[4096];
    size_t n = 0;
    int32_t x, y;

    for (y = 0; y < c->s.h; y++) {
        for (x = 0; x < c->s.w; x++) {
            buf[n++] = px(c, x, y) ? '#' : '.';
        }
        buf[n++] = '\n';
    }
    buf[n] = '\0';
    return buf;
}

#define CHECK_PIC(c, expected) CHECK_STR(picture(&(c)), expected)

/* ---- reference models -------------------------------------------------- */

/* The line rule written out step by step, with no clipping shortcut. */
static void ref_line(canvas_t *c, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int64_t dx = x1 - x0, dy = y1 - y0;
    int64_t adx = llabs(dx), ady = llabs(dy);
    int64_t len = adx > ady ? adx : ady, i;

    if (len == 0) {
        if (x0 >= 0 && y0 >= 0 && x0 < c->s.w && y0 < c->s.h) {
            c->s.px[y0 * c->s.w + x0] = FG;
        }
        return;
    }
    for (i = 0; i <= len; i++) {
        int64_t x, y;

        if (adx >= ady) {
            x = x0 + (dx < 0 ? -i : i);
            y = y0 + (dy < 0 ? -1 : 1) * ((2 * i * ady + adx) / (2 * adx));
        } else {
            y = y0 + (dy < 0 ? -i : i);
            x = x0 + (dx < 0 ? -1 : 1) * ((2 * i * adx + ady) / (2 * ady));
        }
        if (x >= 0 && y >= 0 && x < c->s.w && y < c->s.h) {
            c->s.px[y * c->s.w + x] = FG;
        }
    }
}

static int same_pixels(const canvas_t *a, const canvas_t *b)
{
    return memcmp(a->s.px, b->s.px, (size_t)a->s.w * a->s.h * 4) == 0;
}

/* Floating point without libm: Newton's square root and the atan series. */
static double ref_sqrt(double v)
{
    double r = v > 1 ? v : 1;
    int i;

    for (i = 0; i < 60; i++) {
        r = 0.5 * (r + v / r);
    }
    return r;
}

static double ref_atan01(double z)
{
    double sum = 0, term, z2;
    int k;

    /* Halve the angle twice, then sum the series. */
    z = z / (1 + ref_sqrt(1 + z * z));
    z = z / (1 + ref_sqrt(1 + z * z));
    z2 = z * z;
    term = z;
    for (k = 0; k < 30; k++) {
        sum += (k & 1 ? -term : term) / (2 * k + 1);
        term *= z2;
    }
    return 4 * sum;
}

/* Degrees in [0, 360), clockwise on the screen from +x. */
static double ref_angle(int dx, int dy)
{
    const double pi = 3.14159265358979323846;
    double ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, t;

    t = ax >= ay ? ref_atan01(ay / ax) : pi / 2 - ref_atan01(ax / ay);
    if (dx < 0) {
        t = pi - t;
    }
    if (dy < 0) {
        t = 2 * pi - t;
    }
    t = t * 180.0 / pi;
    return t >= 360.0 ? t - 360.0 : t;
}

/* The arc rule with floating point angles; exact for small radii. */
static int ref_inside(int64_t dx, int64_t dy, int64_t r)
{
    return dx * dx + dy * dy <= r * r + r;
}

static void ref_arc(canvas_t *c, int cx, int cy, int r, int start, int end, int fill)
{
    int span = end > start ? end - start : end + 360 - start;
    int dx, dy;

    for (dy = -r; dy <= r; dy++) {
        for (dx = -r; dx <= r; dx++) {
            double ang;
            int x = cx + dx, y = cy + dy, in;

            if (!ref_inside(dx, dy, r)) {
                continue;
            }
            if (!fill && ref_inside(dx - 1, dy, r) && ref_inside(dx + 1, dy, r) &&
                ref_inside(dx, dy - 1, r) && ref_inside(dx, dy + 1, r)) {
                continue;
            }
            in = span >= 360 || (dx == 0 && dy == 0);
            if (!in) {
                ang = ref_angle(dx, dy) - start;
                ang = ang < 0 ? ang + 360.0 : ang;
                in = ang <= span + 1e-9 || ang >= 360.0 - 1e-9;
            }
            if (in && x >= 0 && y >= 0 && x < c->s.w && y < c->s.h) {
                c->s.px[y * c->s.w + x] = FG;
            }
        }
    }
}

static pip_arc_t arc(int cx, int cy, int r, int start, int end, int fill)
{
    pip_arc_t a = { (int16_t)cx, (int16_t)cy, (uint16_t)r, (uint16_t)start, (uint16_t)end,
                    (uint16_t)fill };

    return a;
}

/* ---- lines ------------------------------------------------------------- */

TEST(line_rule_pinned)
{
    canvas_t c = canvas(8, 4);

    gfx_line(&c.s, 0, 0, 7, 2);
    CHECK_PIC(c, "##......\n"
                 "..####..\n"
                 "......##\n"
                 "........\n");
    done(&c);
    c = canvas(8, 4);
    /* The same pixels drawn from the other end: halves round away from the start. */
    gfx_line(&c.s, 7, 2, 0, 0);
    CHECK_PIC(c, "##......\n"
                 "..####..\n"
                 "......##\n"
                 "........\n");
    done(&c);
}

TEST(line_all_octants)
{
    int a, b, sa, sb, swap;

    for (swap = 0; swap < 2; swap++) {
        for (sa = -1; sa <= 1; sa += 2) {
            for (sb = -1; sb <= 1; sb += 2) {
                for (a = 0; a < 8; a++) {
                    for (b = 0; b <= a; b++) {
                        canvas_t c = canvas(17, 17), r = canvas(17, 17);
                        int dx = sa * (swap ? b : a), dy = sb * (swap ? a : b);

                        gfx_line(&c.s, 8, 8, 8 + dx, 8 + dy);
                        ref_line(&r, 8, 8, 8 + dx, 8 + dy);
                        CHECK(same_pixels(&c, &r));
                        CHECK(px(&c, 8, 8) == FG && px(&c, 8 + dx, 8 + dy) == FG);
                        CHECK_EQ(count_set(&c), (unsigned)(a + 1));
                        done(&c);
                        done(&r);
                    }
                }
            }
        }
    }
}

TEST(line_point)
{
    canvas_t c = canvas(4, 4);

    gfx_line(&c.s, 2, 1, 2, 1);
    CHECK_EQ(count_set(&c), 1u);
    CHECK_EQ(px(&c, 2, 1), FG);
    done(&c);
}

TEST(line_clipped_matches_reference)
{
    static const int32_t cases[][4] = {
        { -50, -20, 70, 33 }, { 70, 33, -50, -20 }, { 5, -100, 9, 200 }, { -3, 7, 40, 7 },
        { 31, -1, -1, 31 }, { 100, 100, -100, -60 }, { 0, 0, 31, 23 }, { -7, 30, 40, -9 },
    };
    unsigned i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        canvas_t c = canvas(32, 24), r = canvas(32, 24);

        gfx_line(&c.s, cases[i][0], cases[i][1], cases[i][2], cases[i][3]);
        ref_line(&r, cases[i][0], cases[i][1], cases[i][2], cases[i][3]);
        CHECK(same_pixels(&c, &r));
        done(&c);
        done(&r);
    }
}

TEST(line_extremes)
{
    canvas_t c = canvas(GFX_W, GFX_H);

    gfx_line(&c.s, INT32_MIN, INT32_MIN, INT32_MAX, INT32_MAX);
    /* The diagonal through the surface: (x, x) for every row. */
    CHECK_EQ(count_set(&c), (unsigned)GFX_H);
    CHECK_EQ(px(&c, 0, 0), FG);
    CHECK_EQ(px(&c, 239, 239), FG);
    gfx_clear(&c.s, 0);
    gfx_line(&c.s, INT32_MIN, 5, INT32_MAX, 5);
    CHECK_EQ(count_set(&c), (unsigned)GFX_W);
    gfx_clear(&c.s, 0);
    gfx_line(&c.s, INT32_MAX, INT32_MIN, INT32_MAX, INT32_MAX);
    gfx_line(&c.s, INT32_MIN, INT32_MAX, INT32_MAX, INT32_MAX);
    gfx_line(&c.s, -32768, -32768, 32767, 32767);
    CHECK_EQ(count_set(&c), (unsigned)GFX_H);
    gfx_line(&c.s, INT32_MIN, INT32_MIN, INT32_MIN, INT32_MIN);
    done(&c);
}

/* ---- points, clear, clip ------------------------------------------------ */

TEST(point_uses_its_colour)
{
    canvas_t c = canvas(4, 4);

    gfx_point(&c.s, 1, 2, 0xff00abcdu);
    CHECK_EQ(px(&c, 1, 2), 0x0000abcdu);
    gfx_point(&c.s, -1, 0, 1);
    gfx_point(&c.s, 4, 0, 1);
    gfx_point(&c.s, 0, 4, 1);
    gfx_point(&c.s, INT32_MIN, INT32_MAX, 1);
    CHECK_EQ(count_set(&c), 1u);
    done(&c);
}

TEST(clip_set_reset_cut)
{
    canvas_t c = canvas(6, 4);

    CHECK_EQ(gfx_set_clip(&c.s, 1, 1, PIP_WH(3, 2)), 0);
    gfx_rect(&c.s, -10, -10, PIP_WH(100, 100), 1);
    CHECK_PIC(c, "......\n"
                 ".###..\n"
                 ".###..\n"
                 "......\n");
    /* clear ignores the clip. */
    gfx_clear(&c.s, 5);
    CHECK_EQ(count_set(&c), 24u);
    gfx_clear(&c.s, 0);
    /* Cut to the surface. */
    CHECK_EQ(gfx_set_clip(&c.s, -3, 2, PIP_WH(5, 0xffff)), 0);
    CHECK_EQ(c.s.st.cx0, 0);
    CHECK_EQ(c.s.st.cy0, 2);
    CHECK_EQ(c.s.st.cx1, 2);
    CHECK_EQ(c.s.st.cy1, 4);
    gfx_rect(&c.s, 0, 0, PIP_WH(6, 4), 1);
    CHECK_PIC(c, "......\n"
                 "......\n"
                 "##....\n"
                 "##....\n");
    /* wh = 0 resets. */
    CHECK_EQ(gfx_set_clip(&c.s, 99, 99, 0), 0);
    CHECK_EQ(c.s.st.cx1, 6);
    CHECK_EQ(c.s.st.cy1, 4);
    CHECK_EQ(c.s.st.cx0, 0);
    /* A zero width or height alone is a bad argument and changes nothing. */
    CHECK_EQ(gfx_set_clip(&c.s, 0, 0, PIP_WH(0, 3)), -PIP_EINVAL);
    CHECK_EQ(gfx_set_clip(&c.s, 0, 0, PIP_WH(3, 0)), -PIP_EINVAL);
    CHECK_EQ(c.s.st.cx1, 6);
    /* Entirely outside: an empty clip, nothing draws. */
    CHECK_EQ(gfx_set_clip(&c.s, INT32_MAX, INT32_MIN, PIP_WH(0xffff, 0xffff)), 0);
    CHECK_EQ(c.s.st.cx0, c.s.st.cx1);
    gfx_clear(&c.s, 0);
    gfx_rect(&c.s, 0, 0, PIP_WH(6, 4), 1);
    gfx_point(&c.s, 0, 0, 1);
    CHECK_EQ(count_set(&c), 0u);
    CHECK_EQ(gfx_set_clip(&c.s, INT32_MIN, INT32_MIN, PIP_WH(0xffff, 0xffff)), 0);
    CHECK_EQ(c.s.st.cx0, c.s.st.cx1);
    done(&c);
}

TEST(colors_and_font)
{
    canvas_t c = canvas(2, 2);

    CHECK_EQ(c.s.st.fg, FG);
    CHECK_EQ(c.s.st.bg, PIP_COLOR_NONE);
    gfx_set_color(&c.s, 0xff112233u, 0x00445566u);
    CHECK_EQ(c.s.st.fg, 0x00112233u);
    CHECK_EQ(c.s.st.bg, 0x00445566u);
    gfx_set_color(&c.s, 1, PIP_COLOR_NONE);
    CHECK_EQ(c.s.st.bg, PIP_COLOR_NONE);
    gfx_set_color(&c.s, 1, 0x02abcdefu);
    CHECK_EQ(c.s.st.bg, 0x00abcdefu);
    CHECK_EQ(gfx_set_font(&c.s, 0), 0);
    CHECK_EQ(gfx_set_font(&c.s, 1), -PIP_EINVAL);
    CHECK_EQ(gfx_set_font(&c.s, 0xffffffffu), -PIP_EINVAL);
    done(&c);
}

/* ---- rect --------------------------------------------------------------- */

TEST(rect_sizes)
{
    canvas_t c = canvas(6, 5);

    CHECK_EQ(gfx_rect(&c.s, 1, 1, PIP_WH(0, 3), 0), 0);
    CHECK_EQ(gfx_rect(&c.s, 1, 1, PIP_WH(3, 0), 1), 0);
    CHECK_EQ(gfx_rect(&c.s, 1, 1, 0, 1), 0);
    CHECK_EQ(count_set(&c), 0u);
    CHECK_EQ(gfx_rect(&c.s, 1, 1, PIP_WH(1, 1), 0), 0);
    CHECK_PIC(c, "......\n"
                 ".#....\n"
                 "......\n"
                 "......\n"
                 "......\n");
    gfx_clear(&c.s, 0);
    CHECK_EQ(gfx_rect(&c.s, 0, 0, PIP_WH(5, 4), 0), 0);
    CHECK_PIC(c, "#####.\n"
                 "#...#.\n"
                 "#...#.\n"
                 "#####.\n"
                 "......\n");
    gfx_clear(&c.s, 0);
    CHECK_EQ(gfx_rect(&c.s, 1, 1, PIP_WH(4, 3), 1), 0);
    CHECK_PIC(c, "......\n"
                 ".####.\n"
                 ".####.\n"
                 ".####.\n"
                 "......\n");
    gfx_clear(&c.s, 0);
    CHECK_EQ(gfx_rect(&c.s, 1, 1, PIP_WH(1, 4), 0), 0);
    CHECK_EQ(gfx_rect(&c.s, 3, 2, PIP_WH(3, 1), 0), 0);
    CHECK_PIC(c, "......\n"
                 ".#....\n"
                 ".#.###\n"
                 ".#....\n"
                 ".#....\n");
    CHECK_EQ(gfx_rect(&c.s, 0, 0, PIP_WH(1, 1), 2), -PIP_EINVAL);
    CHECK_EQ(gfx_rect(&c.s, 0, 0, PIP_WH(1, 1), 0xffffffffu), -PIP_EINVAL);
    done(&c);
}

TEST(rect_clipped_outline)
{
    canvas_t c = canvas(6, 4);

    CHECK_EQ(gfx_rect(&c.s, -2, 1, PIP_WH(6, 9), 0), 0);
    CHECK_PIC(c, "......\n"
                 "####..\n"
                 "...#..\n"
                 "...#..\n");
    done(&c);
}

TEST(rect_extremes)
{
    canvas_t c = canvas(GFX_W, GFX_H);

    CHECK_EQ(gfx_rect(&c.s, INT32_MAX, INT32_MAX, PIP_WH(0xffff, 0xffff), 1), 0);
    CHECK_EQ(gfx_rect(&c.s, INT32_MAX - 1, 0, PIP_WH(0xffff, 0xffff), 0), 0);
    CHECK_EQ(count_set(&c), 0u);
    CHECK_EQ(gfx_rect(&c.s, INT32_MIN, INT32_MIN, PIP_WH(0xffff, 0xffff), 1), 0);
    CHECK_EQ(count_set(&c), 0u);
    CHECK_EQ(gfx_rect(&c.s, -32768, -32768, PIP_WH(0xffff, 0xffff), 0), 0);
    /* The right edge at x = 32766 and bottom edge are off the surface: nothing shows. */
    CHECK_EQ(count_set(&c), 0u);
    CHECK_EQ(gfx_rect(&c.s, -32768, -32768, PIP_WH(0xffff, 0xffff), 1), 0);
    CHECK_EQ(count_set(&c), (unsigned)(GFX_W * GFX_H));
    done(&c);
}

/* ---- polyline ----------------------------------------------------------- */

TEST(polyline_open_closed)
{
    static const int16_t tri[] = { 0, 0, 4, 0, 4, 3 };
    canvas_t c = canvas(6, 4);

    CHECK_EQ(gfx_polyline_check(3, 0), 0);
    gfx_polyline(&c.s, tri, 3, 0);
    CHECK_PIC(c, "#####.\n"
                 "....#.\n"
                 "....#.\n"
                 "....#.\n");
    gfx_polyline(&c.s, tri, 3, 1);
    /* 4,3 to 0,0: halves round away from 4,3, so 2,1 rather than 2,2. */
    CHECK_PIC(c, "#####.\n"
                 ".##.#.\n"
                 "...##.\n"
                 "....#.\n");
    done(&c);
    c = canvas(6, 4);
    gfx_polyline(&c.s, tri, 2, 1);
    CHECK_PIC(c, "#####.\n"
                 "......\n"
                 "......\n"
                 "......\n");
    done(&c);
}

TEST(polyline_limits_and_extremes)
{
    int16_t pts[2 * PIP_POLY_MAX];
    canvas_t c = canvas(GFX_W, GFX_H);
    unsigned i;

    CHECK_EQ(gfx_polyline_check(1, 0), -PIP_EINVAL);
    CHECK_EQ(gfx_polyline_check(0, 0), -PIP_EINVAL);
    CHECK_EQ(gfx_polyline_check(2, 0), 0);
    CHECK_EQ(gfx_polyline_check(256, 1), 0);
    CHECK_EQ(gfx_polyline_check(257, 0), -PIP_EINVAL);
    CHECK_EQ(gfx_polyline_check(2, 2), -PIP_EINVAL);
    for (i = 0; i < PIP_POLY_MAX; i++) {
        pts[2 * i] = (int16_t)(i & 1 ? INT16_MAX : INT16_MIN);
        pts[2 * i + 1] = (int16_t)(i & 2 ? INT16_MAX : INT16_MIN);
    }
    gfx_polyline(&c.s, pts, PIP_POLY_MAX, 1);
    done(&c);
}

/* ---- arc ---------------------------------------------------------------- */

TEST(arc_pinned)
{
    canvas_t c = canvas(9, 9);
    pip_arc_t a = arc(4, 4, 3, 0, 360, 0);

    CHECK_EQ(gfx_arc(&c.s, &a), 0);
    CHECK_PIC(c, ".........\n"
                 "...###...\n"
                 "..#...#..\n"
                 ".#.....#.\n"
                 ".#.....#.\n"
                 ".#.....#.\n"
                 "..#...#..\n"
                 "...###...\n"
                 ".........\n");
    gfx_clear(&c.s, 0);
    /* A quarter, 0 to 90: from +x clockwise to straight down. */
    a = arc(4, 4, 3, 0, 90, 0);
    CHECK_EQ(gfx_arc(&c.s, &a), 0);
    CHECK_PIC(c, ".........\n"
                 ".........\n"
                 ".........\n"
                 ".........\n"
                 ".......#.\n"
                 ".......#.\n"
                 "......#..\n"
                 "....##...\n"
                 ".........\n");
    gfx_clear(&c.s, 0);
    a = arc(4, 4, 1, 0, 360, 0);
    CHECK_EQ(gfx_arc(&c.s, &a), 0);
    CHECK_PIC(c, ".........\n"
                 ".........\n"
                 ".........\n"
                 "...###...\n"
                 "...#.#...\n"
                 "...###...\n"
                 ".........\n"
                 ".........\n"
                 ".........\n");
    gfx_clear(&c.s, 0);
    a = arc(4, 4, 1, 0, 360, 1);
    CHECK_EQ(gfx_arc(&c.s, &a), 0);
    CHECK_EQ(count_set(&c), 9u);
    gfx_clear(&c.s, 0);
    /* A filled slice through 0, 270 to 90: the right half, centre included. */
    a = arc(4, 4, 2, 270, 90, 1);
    CHECK_EQ(gfx_arc(&c.s, &a), 0);
    CHECK_PIC(c, ".........\n"
                 ".........\n"
                 "....##...\n"
                 "....###..\n"
                 "....###..\n"
                 "....###..\n"
                 "....##...\n"
                 ".........\n"
                 ".........\n");
    done(&c);
}

TEST(arc_matches_reference)
{
    static const int cases[][4] = {
        /* start, end, fill, radius */
        { 0, 360, 0, 7 }, { 0, 90, 0, 9 }, { 300, 60, 0, 10 }, { 300, 60, 1, 10 },
        { 45, 135, 1, 11 }, { 10, 350, 0, 12 }, { 10, 350, 1, 12 }, { 90, 270, 1, 8 },
        { 359, 1, 0, 14 }, { 180, 180, 0, 6 }, { 0, 0, 1, 5 }, { 123, 321, 0, 13 },
        { 200, 10, 1, 15 }, { 0, 1, 1, 15 },
    };
    unsigned i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        canvas_t c = canvas(41, 41), r = canvas(41, 41);
        pip_arc_t a = arc(20, 20, cases[i][3], cases[i][0], cases[i][1], cases[i][2]);

        CHECK_EQ(gfx_arc(&c.s, &a), 0);
        ref_arc(&r, 20, 20, cases[i][3], cases[i][0], cases[i][1], cases[i][2]);
        if (!same_pixels(&c, &r)) {
            fprintf(stderr, "arc case %u differs\n", i);
        }
        CHECK(same_pixels(&c, &r));
        done(&c);
        done(&r);
    }
}

TEST(arc_wrap_is_union)
{
    /* 300 to 60 is 300 to 360 plus 0 to 60. */
    canvas_t c = canvas(31, 31), r = canvas(31, 31);
    pip_arc_t a = arc(15, 15, 12, 300, 60, 0), b1 = arc(15, 15, 12, 300, 360, 0),
              b2 = arc(15, 15, 12, 0, 60, 0);

    gfx_arc(&c.s, &a);
    gfx_arc(&r.s, &b1);
    gfx_arc(&r.s, &b2);
    CHECK(same_pixels(&c, &r));
    done(&c);
    done(&r);
}

TEST(arc_big_clipped)
{
    /* Radius 512 on the surface, against the same arc on a surface that holds it all. */
    static const int cases[][4] = {
        { 0, 360, 0, 0 }, { 30, 200, 1, 0 }, { 0, 360, 1, 1 }, { 270, 0, 0, 1 },
    };
    unsigned i;
    int32_t x, y;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        canvas_t c = canvas(GFX_W, GFX_H), big = canvas(1100, 1100);
        int cx = cases[i][3] ? 160 : -300, cy = cases[i][3] ? 120 : 500;
        pip_arc_t a = arc(cx, cy, 512, cases[i][0], cases[i][1], cases[i][2]);
        pip_arc_t b = arc(cx + 400, cy + 400, 512, cases[i][0], cases[i][1], cases[i][2]);
        int same = 1;

        CHECK_EQ(gfx_arc(&c.s, &a), 0);
        CHECK_EQ(gfx_arc(&big.s, &b), 0);
        for (y = 0; y < GFX_H; y++) {
            for (x = 0; x < GFX_W; x++) {
                same &= px(&c, x, y) == px(&big, x + 400, y + 400);
            }
        }
        CHECK(same);
        done(&c);
        done(&big);
    }
}

TEST(arc_limits_and_extremes)
{
    canvas_t c = canvas(GFX_W, GFX_H);
    pip_arc_t a;

    a = arc(5, 5, 0, 0, 360, 0);
    CHECK_EQ(gfx_arc(&c.s, &a), -PIP_EINVAL);
    a = arc(5, 5, 513, 0, 360, 0);
    CHECK_EQ(gfx_arc(&c.s, &a), -PIP_EINVAL);
    a = arc(5, 5, 512, 360, 360, 0);
    CHECK_EQ(gfx_arc(&c.s, &a), -PIP_EINVAL);
    a = arc(5, 5, 1, 0, 361, 0);
    CHECK_EQ(gfx_arc(&c.s, &a), -PIP_EINVAL);
    a = arc(5, 5, 1, 0, 360, 2);
    CHECK_EQ(gfx_arc(&c.s, &a), -PIP_EINVAL);
    CHECK_EQ(count_set(&c), 0u);
    a = arc(5, 5, 1, 359, 360, 0);
    CHECK_EQ(gfx_arc(&c.s, &a), 0);
    a = arc(INT16_MIN, INT16_MIN, 512, 0, 360, 1);
    CHECK_EQ(gfx_arc(&c.s, &a), 0);
    a = arc(INT16_MAX, INT16_MAX, 512, 0, 360, 1);
    CHECK_EQ(gfx_arc(&c.s, &a), 0);
    a = arc(INT16_MAX, INT16_MIN, 512, 0, 360, 0);
    CHECK_EQ(gfx_arc(&c.s, &a), 0);
    done(&c);
}

/* ---- blits -------------------------------------------------------------- */

static pip_blit_t blit_desc(int x, int y, int w, int h, int stride, uint32_t color)
{
    pip_blit_t b;

    memset(&b, 0, sizeof(b));
    b.x = (int16_t)x;
    b.y = (int16_t)y;
    b.w = (uint16_t)w;
    b.h = (uint16_t)h;
    b.stride = (uint16_t)stride;
    b.color = color;
    return b;
}

TEST(blit_check)
{
    pip_blit_t b = blit_desc(0, 0, 13, 3, 2, 0);
    uint32_t len = 0;

    CHECK_EQ(gfx_blit_check(&b, 0, &len), 0);
    CHECK_EQ(len, 2u * 2 + 2);
    b.stride = 1;
    CHECK_EQ(gfx_blit_check(&b, 0, &len), -PIP_EINVAL);
    b = blit_desc(0, 0, 5, 2, 20, 0);
    CHECK_EQ(gfx_blit_check(&b, 1, &len), 0);
    CHECK_EQ(len, 20u + 20);
    b.stride = 19;
    CHECK_EQ(gfx_blit_check(&b, 1, &len), -PIP_EINVAL);
    b = blit_desc(0, 0, 320, 240, 1280, 0);
    CHECK_EQ(gfx_blit_check(&b, 1, &len), 0);
    CHECK_EQ(len, 1280u * 240);
    b.w = 321;
    b.stride = 1284;
    CHECK_EQ(gfx_blit_check(&b, 1, &len), -PIP_EINVAL);
    b = blit_desc(0, 0, 1, 241, 1, 0);
    CHECK_EQ(gfx_blit_check(&b, 0, &len), -PIP_EINVAL);
    b.h = 0;
    CHECK_EQ(gfx_blit_check(&b, 0, &len), -PIP_EINVAL);
    b = blit_desc(0, 0, 0, 1, 1, 0);
    CHECK_EQ(gfx_blit_check(&b, 0, &len), -PIP_EINVAL);
    b = blit_desc(0, 0, 1, 1, 1, 0);
    b.flags = 1;
    CHECK_EQ(gfx_blit_check(&b, 0, &len), -PIP_EINVAL);
}

TEST(blit_1bpp_odd_width_stride)
{
    /* w 5 and stride 2: the second byte of each row and bits past 5 are ignored. */
    static const uint8_t bits[] = { 0xaf, 0xff, 0x50, 0xff, 0xff, 0xff };
    pip_blit_t b = blit_desc(1, 1, 5, 3, 2, 0xff0000ffu);
    canvas_t c = canvas(7, 5);

    gfx_blit_1bpp(&c.s, &b, bits);
    CHECK_PIC(c, ".......\n"
                 ".#.#.#.\n"
                 "..#.#..\n"
                 ".#####.\n"
                 ".......\n");
    CHECK_EQ(px(&c, 1, 1), 0x000000ffu);
    /* Clear bits leave what was there. */
    gfx_clear(&c.s, 7);
    gfx_blit_1bpp(&c.s, &b, bits);
    CHECK_EQ(px(&c, 2, 1), 7u);
    done(&c);
}

TEST(blit_clipped_each_side)
{
    static const uint8_t ones[4 * 2] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
    static const int pos[][2] = { { -2, 1 }, { 5, 1 }, { 2, -1 }, { 2, 2 }, { -3, -2 } };
    static const char *const want[] = {
        "......\n#.....\n#.....\n......\n",
        "......\n.....#\n.....#\n......\n",
        "..###.\n......\n......\n......\n",
        "......\n......\n..###.\n..###.\n",
        "......\n......\n......\n......\n",
    };
    unsigned i;

    for (i = 0; i < 5; i++) {
        canvas_t c = canvas(6, 4);
        pip_blit_t b = blit_desc(pos[i][0], pos[i][1], 3, 2, 4, 1);

        gfx_blit_1bpp(&c.s, &b, ones);
        CHECK_STR(picture(&c), want[i]);
        done(&c);
    }
}

TEST(blit_rgb)
{
    /* w 3, stride 16: one spare pixel per row. Bytes are little-endian. */
    uint8_t data[16 * 2];
    pip_blit_t b = blit_desc(-1, 0, 3, 2, 16, 0x123456);
    canvas_t c = canvas(3, 3);
    int i;

    for (i = 0; i < 32; i++) {
        data[i] = (uint8_t)(i + 1);
    }
    gfx_blit_rgb(&c.s, &b, data);
    /* Column 0 of the data is clipped; the top byte is dropped. */
    CHECK_EQ(px(&c, 0, 0), 0x00070605u);
    CHECK_EQ(px(&c, 1, 0), 0x000b0a09u);
    CHECK_EQ(px(&c, 2, 0), 0u);
    CHECK_EQ(px(&c, 0, 1), 0x00171615u);
    CHECK_EQ(px(&c, 1, 1), 0x001b1a19u);
    CHECK_EQ(count_set(&c), 4u);
    done(&c);
    /* Unaligned data. */
    c = canvas(3, 3);
    b = blit_desc(0, 2, 1, 1, 4, 0);
    gfx_blit_rgb(&c.s, &b, data + 1);
    CHECK_EQ(px(&c, 0, 2), 0x00040302u);
    done(&c);
}

TEST(blit_extremes)
{
    static uint8_t data[1280 * 240];
    canvas_t c = canvas(GFX_W, GFX_H);
    pip_blit_t b = blit_desc(INT16_MIN, INT16_MIN, 320, 240, 1280, 1);

    memset(data, 0xff, sizeof(data));
    gfx_blit_rgb(&c.s, &b, data);
    b.x = INT16_MAX;
    b.y = INT16_MAX;
    gfx_blit_rgb(&c.s, &b, data);
    b.stride = 40;
    gfx_blit_1bpp(&c.s, &b, data);
    b.x = INT16_MIN;
    gfx_blit_1bpp(&c.s, &b, data);
    CHECK_EQ(count_set(&c), 0u);
    b.x = 319;
    b.y = 239;
    gfx_blit_1bpp(&c.s, &b, data);
    CHECK_EQ(count_set(&c), 1u);
    done(&c);
}

/* ---- text --------------------------------------------------------------- */

TEST(text_metrics)
{
    CHECK_EQ(gfx_text_metrics(0, 1), 16);
    CHECK_EQ(gfx_text_metrics(5, 1), (40 << 16) | 16);
    CHECK_EQ(gfx_text_metrics(255, 8), (255 * 64) << 16 | 128);
    CHECK_EQ(gfx_text_metrics(1, 0), -PIP_EINVAL);
    CHECK_EQ(gfx_text_metrics(1, 9), -PIP_EINVAL);
}

TEST(text_scale1_transparent)
{
    canvas_t c = canvas(18, 16);

    /* 'A' = 0x41: row 0 shows .#.....#; row 15 shows #......# */
    CHECK_EQ(gfx_text(&c.s, 1, 0, "A\x01", 2, 1), 16);
    CHECK_EQ(glyph_calls['A'], 1u);
    CHECK_EQ(glyph_calls['?'], 1u);
    CHECK_EQ(glyph_calls[1], 0u);
    CHECK_EQ(px(&c, 2, 0), FG);
    CHECK_EQ(px(&c, 8, 0), FG);
    CHECK_EQ(px(&c, 1, 15), FG);
    CHECK_EQ(px(&c, 8, 15), FG);
    /* '?' = 0x3f: ..###### */
    CHECK_EQ(px(&c, 11, 0), FG);
    CHECK_EQ(px(&c, 10, 0), 0u);
    CHECK_EQ(count_set(&c), 2u + 2 + 6 + 2);
    done(&c);
}

TEST(text_unprintable_bytes)
{
    static const char s[] = { 0x1f, 0x7f, (char)0x80, (char)0xff, ' ', '~' };
    canvas_t c = canvas(60, 16);

    CHECK_EQ(gfx_text(&c.s, 0, 0, s, sizeof(s), 1), 48);
    CHECK_EQ(glyph_calls['?'], 4u);
    CHECK_EQ(glyph_calls[' '], 1u);
    CHECK_EQ(glyph_calls['~'], 1u);
    done(&c);
}

TEST(text_opaque_scale8)
{
    canvas_t c = canvas(70, 130);

    gfx_set_color(&c.s, 0x00ff0000u, 0x000000ffu);
    CHECK_EQ(gfx_text(&c.s, 2, 1, "@", 1, 8), 64);
    /* '@' = 0x40: one pixel at column 1 of row 0, an 8x8 block. */
    CHECK_EQ(px(&c, 2 + 8, 1), 0x00ff0000u);
    CHECK_EQ(px(&c, 2 + 15, 1 + 7), 0x00ff0000u);
    CHECK_EQ(px(&c, 2 + 16, 1), 0x000000ffu);
    CHECK_EQ(px(&c, 2, 1), 0x000000ffu);
    CHECK_EQ(px(&c, 2 + 63, 1 + 127), 0x00ff0000u);
    CHECK_EQ(px(&c, 2 + 62, 1 + 119), 0x000000ffu);
    CHECK_EQ(px(&c, 1, 1), 0u);
    CHECK_EQ(px(&c, 66, 1), 0u);
    CHECK_EQ(px(&c, 2, 0), 0u);
    CHECK_EQ(px(&c, 2, 129), 0u);
    CHECK_EQ(count_set(&c), 64u * 128);
    done(&c);
}

TEST(text_clipped_and_extremes)
{
    canvas_t c = canvas(GFX_W, GFX_H);
    char s[255];

    memset(s, 'W', sizeof(s));
    gfx_set_color(&c.s, 1, 2);
    CHECK_EQ(gfx_text(&c.s, INT32_MAX, INT32_MAX, s, 255, 8), 255 * 64);
    CHECK_EQ(gfx_text(&c.s, INT32_MIN, INT32_MIN, s, 255, 8), 255 * 64);
    CHECK_EQ(gfx_text(&c.s, INT32_MAX, 0, s, 255, 8), 255 * 64);
    CHECK_EQ(gfx_text(&c.s, INT32_MIN, 0, s, 255, 8), 255 * 64);
    CHECK_EQ(count_set(&c), 0u);
    CHECK_EQ(gfx_text(&c.s, -4, -8, s, 1, 1), 8);
    /* The opaque cell is clipped to its 4 x 8 visible corner. */
    CHECK_EQ(count_set(&c), 32u);
    CHECK_EQ(gfx_text(&c.s, 0, 0, "", 0, 1), 0);
    done(&c);
}

/* ---- present ------------------------------------------------------------ */

TEST(scale_copy)
{
    static uint32_t src[GFX_W * GFX_H];
    static uint32_t dst[1000 * 760];
    uint32_t i, x, y;

    for (i = 0; i < GFX_W * GFX_H; i++) {
        src[i] = i * 2654435761u & 0x00ffffffu;
    }
    CHECK_EQ(gfx_scale_factor(320, 240, 640, 480), 2u);
    CHECK_EQ(gfx_scale_factor(320, 240, 959, 720), 2u);
    CHECK_EQ(gfx_scale_factor(320, 240, 319, 480), 0u);
    CHECK_EQ(gfx_scale_factor(320, 240, 320, 240), 1u);

    /* 640 x 480, BGR: 2 x 2 blocks, top byte 0xFF. */
    CHECK_EQ(gfx_scale_copy(src, 320, 240, (uint8_t *)dst, 640, 480, 640 * 4, 0), 2u);
    for (y = 0; y < 480; y++) {
        for (x = 0; x < 640; x++) {
            if (dst[y * 640 + x] != (src[(y / 2) * 320 + x / 2] | 0xff000000u)) {
                RC_FAIL("pixel %u,%u", x, y);
                return;
            }
        }
    }
    /* RGB order swaps red and blue. */
    src[0] = 0x00112233u;
    CHECK_EQ(gfx_scale_copy(src, 320, 240, (uint8_t *)dst, 640, 480, 640 * 4, 1), 2u);
    CHECK_EQ(dst[0], 0xff332211u);
    CHECK_EQ(dst[641], 0xff332211u);

    /* 700 x 500 with a pitch of 720 pixels: factor 2, centred, black border. */
    for (i = 0; i < 720 * 500; i++) {
        dst[i] = 0x5a5a5a5au;
    }
    CHECK_EQ(gfx_scale_copy(src, 320, 240, (uint8_t *)dst, 700, 500, 720 * 4, 0), 2u);
    CHECK_EQ(dst[0], 0xff000000u);
    CHECK_EQ(dst[10 * 720 + 29], 0xff000000u);
    CHECK_EQ(dst[10 * 720 + 30], src[0] | 0xff000000u);
    CHECK_EQ(dst[489 * 720 + 669], src[239 * 320 + 319] | 0xff000000u);
    CHECK_EQ(dst[489 * 720 + 670], 0xff000000u);
    CHECK_EQ(dst[490 * 720 + 30], 0xff000000u);
    CHECK_EQ(dst[0 * 720 + 700], 0x5a5a5a5au);   /* past the width: untouched */

    /* 960 x 720: factor 3. */
    CHECK_EQ(gfx_scale_copy(src, 320, 240, (uint8_t *)dst, 960, 720, 960 * 4, 0), 3u);
    CHECK_EQ(dst[719 * 960 + 959], src[239 * 320 + 319] | 0xff000000u);

    /* Too small: nothing written. */
    dst[0] = 0x5a5a5a5au;
    CHECK_EQ(gfx_scale_copy(src, 320, 240, (uint8_t *)dst, 640, 239, 640 * 4, 0), 0u);
    CHECK_EQ(dst[0], 0x5a5a5a5au);
}

/* ---- per-image state ---------------------------------------------------- */

TEST(image_state)
{
    canvas_t c = canvas(8, 8);
    gfx_state_t saved;

    gfx_clear(&c.s, 9);
    gfx_set_color(&c.s, 7, 8);
    gfx_image_begin(&c.s, &saved, PIP_KIND_ROM, 0x00abcdef);
    CHECK_EQ(c.s.st.fg, 0x00abcdefu);
    CHECK_EQ(c.s.st.bg, PIP_COLOR_NONE);
    CHECK_EQ(count_set(&c), 0u);
    /* The ROM sets its state, a holotape starts with a fresh one. */
    gfx_set_color(&c.s, 1, 2);
    gfx_set_clip(&c.s, 1, 2, PIP_WH(3, 4));
    gfx_clear(&c.s, 5);
    gfx_image_begin(&c.s, &saved, PIP_KIND_HOLOTAPE, 0x00abcdef);
    CHECK_EQ(c.s.st.fg, 0x00abcdefu);
    CHECK_EQ(c.s.st.cx0, 0);
    CHECK_EQ(c.s.st.cx1, 8);
    CHECK_EQ(count_set(&c), 0u);
    gfx_set_color(&c.s, 3, 4);
    gfx_set_clip(&c.s, 0, 0, PIP_WH(1, 1));
    CHECK_EQ(gfx_image_end(&c.s, &saved, PIP_KIND_HOLOTAPE), 0);
    CHECK_EQ(c.s.st.fg, 1u);
    CHECK_EQ(c.s.st.bg, 2u);
    CHECK_EQ(c.s.st.cx0, 1);
    CHECK_EQ(c.s.st.cy0, 2);
    CHECK_EQ(c.s.st.cx1, 4);
    CHECK_EQ(c.s.st.cy1, 6);
    CHECK_EQ(gfx_image_end(&c.s, &saved, PIP_KIND_ROM), 1);
    done(&c);
}
