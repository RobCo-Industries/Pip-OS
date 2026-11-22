/*
 * Drawing primitives on a gfx_surface_t. Integer math only: the target is
 * soft-float. Every coordinate is widened to 64 bits before it is added
 * to, so INT32_MIN and INT32_MAX never overflow. Every write goes through
 * the clip rectangle, which always lies inside the surface.
 */
#include "gfx_draw.h"

#define RGB_MASK 0x00ffffffu

static uint32_t rgb(uint32_t c)
{
    return c & RGB_MASK;
}

static int64_t max64(int64_t a, int64_t b)
{
    return a > b ? a : b;
}

static int64_t min64(int64_t a, int64_t b)
{
    return a < b ? a : b;
}

void gfx_surface_init(gfx_surface_t *s, uint32_t *px, int32_t w, int32_t h, gfx_glyph_fn glyph)
{
    s->px = px;
    s->w = w;
    s->h = h;
    s->glyph = glyph;
    gfx_state_reset(s, 0);
}

void gfx_state_reset(gfx_surface_t *s, uint32_t fg)
{
    s->st.fg = rgb(fg);
    s->st.bg = PIP_COLOR_NONE;
    s->st.cx0 = 0;
    s->st.cy0 = 0;
    s->st.cx1 = s->w;
    s->st.cy1 = s->h;
    s->st.font = PIP_FONT_SYSTEM;
}

void gfx_image_begin(gfx_surface_t *s, gfx_state_t *saved, int kind, uint32_t fg)
{
    if (kind == PIP_KIND_HOLOTAPE) {
        *saved = s->st;
    }
    gfx_state_reset(s, fg);
    gfx_clear(s, 0);
}

int gfx_image_end(gfx_surface_t *s, const gfx_state_t *saved, int kind)
{
    if (kind == PIP_KIND_HOLOTAPE) {
        s->st = *saved;
        return 0;
    }
    return 1;
}

void gfx_set_color(gfx_surface_t *s, uint32_t fg, uint32_t bg)
{
    s->st.fg = rgb(fg);
    s->st.bg = bg == PIP_COLOR_NONE ? PIP_COLOR_NONE : rgb(bg);
}

int32_t gfx_set_clip(gfx_surface_t *s, int32_t x, int32_t y, uint32_t wh)
{
    uint32_t w = wh >> 16, h = wh & 0xffffu;

    if (wh == 0) {
        s->st.cx0 = 0;
        s->st.cy0 = 0;
        s->st.cx1 = s->w;
        s->st.cy1 = s->h;
        return 0;
    }
    if (w == 0 || h == 0) {
        return -PIP_EINVAL;
    }
    /* Cut to the surface. An empty result is legal: nothing draws. */
    s->st.cx0 = (int32_t)min64(max64(x, 0), s->w);
    s->st.cy0 = (int32_t)min64(max64(y, 0), s->h);
    s->st.cx1 = (int32_t)max64(min64((int64_t)x + w, s->w), s->st.cx0);
    s->st.cy1 = (int32_t)max64(min64((int64_t)y + h, s->h), s->st.cy0);
    return 0;
}

int32_t gfx_set_font(gfx_surface_t *s, uint32_t font)
{
    if (font != PIP_FONT_SYSTEM) {
        return -PIP_EINVAL;
    }
    s->st.font = font;
    return 0;
}

static void put(gfx_surface_t *s, int64_t x, int64_t y, uint32_t c)
{
    if (x >= s->st.cx0 && x < s->st.cx1 && y >= s->st.cy0 && y < s->st.cy1) {
        s->px[y * s->w + x] = c;
    }
}

/* Fills [x, x + w) x [y, y + h), clipped. */
static void fill(gfx_surface_t *s, int64_t x, int64_t y, int64_t w, int64_t h, uint32_t c)
{
    int64_t x0 = max64(x, s->st.cx0), x1 = min64(x + w, s->st.cx1);
    int64_t y0 = max64(y, s->st.cy0), y1 = min64(y + h, s->st.cy1);
    int64_t i, j;

    for (j = y0; j < y1; j++) {
        uint32_t *row = s->px + j * s->w;

        for (i = x0; i < x1; i++) {
            row[i] = c;
        }
    }
}

void gfx_point(gfx_surface_t *s, int32_t x, int32_t y, uint32_t color)
{
    put(s, x, y, rgb(color));
}

void gfx_clear(gfx_surface_t *s, uint32_t color)
{
    int32_t i, n = s->w * s->h;

    for (i = 0; i < n; i++) {
        s->px[i] = rgb(color);
    }
}

/*
 * Bresenham. Along the major axis step i (0 to its length L) moves one
 * pixel; the minor axis moves round(i * m / L) with halves rounded away
 * from the start, m being the minor length. Both ends are drawn. Only the
 * steps inside the clip on the major axis run, so the loop is at most
 * the clip size long whatever the coordinates.
 */
void gfx_line(gfx_surface_t *s, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    int64_t dx = (int64_t)x1 - x0, dy = (int64_t)y1 - y0;
    int64_t sx = dx < 0 ? -1 : 1, sy = dy < 0 ? -1 : 1;
    uint64_t adx = (uint64_t)(dx < 0 ? -dx : dx), ady = (uint64_t)(dy < 0 ? -dy : dy);
    int xmajor = adx >= ady;
    uint64_t len = xmajor ? adx : ady, minor = xmajor ? ady : adx;
    int64_t m0 = xmajor ? x0 : y0, n0 = xmajor ? y0 : x0;
    int64_t sm = xmajor ? sx : sy, sn = xmajor ? sy : sx;
    int64_t lo = xmajor ? s->st.cx0 : s->st.cy0, hi = (xmajor ? s->st.cx1 : s->st.cy1) - 1;
    int64_t i0, i1, i;
    uint64_t q, r, p;

    if (len == 0) {
        put(s, x0, y0, s->st.fg);
        return;
    }
    if (sm > 0) {
        i0 = max64(0, lo - m0);
        i1 = min64((int64_t)len, hi - m0);
    } else {
        i0 = max64(0, m0 - hi);
        i1 = min64((int64_t)len, m0 - lo);
    }
    if (i0 > i1) {
        return;
    }
    /* i * minor fits: both are below 2^32. */
    p = (uint64_t)i0 * minor;
    q = p / len;
    r = p % len;
    for (i = i0; i <= i1; i++) {
        int64_t n = n0 + sn * (int64_t)(q + (2 * r >= len ? 1 : 0));
        int64_t m = m0 + sm * i;

        if (xmajor) {
            put(s, m, n, s->st.fg);
        } else {
            put(s, n, m, s->st.fg);
        }
        r += minor;
        if (r >= len) {
            r -= len;
            q++;
        }
    }
}

int32_t gfx_rect(gfx_surface_t *s, int32_t x, int32_t y, uint32_t wh, uint32_t fill_mode)
{
    int64_t w = wh >> 16, h = wh & 0xffffu;

    if (fill_mode > PIP_RECT_FILLED) {
        return -PIP_EINVAL;
    }
    if (w == 0 || h == 0) {
        return 0;
    }
    if (fill_mode == PIP_RECT_FILLED || w <= 2 || h <= 2) {
        fill(s, x, y, w, h, s->st.fg);
        return 0;
    }
    fill(s, x, y, w, 1, s->st.fg);
    fill(s, x, (int64_t)y + h - 1, w, 1, s->st.fg);
    fill(s, x, (int64_t)y + 1, 1, h - 2, s->st.fg);
    fill(s, (int64_t)x + w - 1, (int64_t)y + 1, 1, h - 2, s->st.fg);
    return 0;
}

int32_t gfx_polyline_check(uint32_t count, uint32_t closed)
{
    if (count < PIP_POLY_MIN || count > PIP_POLY_MAX || closed > 1) {
        return -PIP_EINVAL;
    }
    return 0;
}

void gfx_polyline(gfx_surface_t *s, const int16_t *xy, uint32_t count, uint32_t closed)
{
    uint32_t i;

    for (i = 0; i + 1 < count; i++) {
        gfx_line(s, xy[2 * i], xy[2 * i + 1], xy[2 * i + 2], xy[2 * i + 3]);
    }
    if (closed) {
        gfx_line(s, xy[2 * count - 2], xy[2 * count - 1], xy[0], xy[1]);
    }
}

/* ---- arc ---------------------------------------------------------------- */

/* round(65536 * sin(d)) for d = 0 to 90 degrees. */
static const int32_t sin_q16[91] = {
    0, 1144, 2287, 3430, 4572, 5712, 6850, 7987,
    9121, 10252, 11380, 12505, 13626, 14742, 15855, 16962,
    18064, 19161, 20252, 21336, 22415, 23486, 24550, 25607,
    26656, 27697, 28729, 29753, 30767, 31772, 32768, 33754,
    34729, 35693, 36647, 37590, 38521, 39441, 40348, 41243,
    42126, 42995, 43852, 44695, 45525, 46341, 47143, 47930,
    48703, 49461, 50203, 50931, 51643, 52339, 53020, 53684,
    54332, 54963, 55578, 56175, 56756, 57319, 57865, 58393,
    58903, 59396, 59870, 60326, 60764, 61183, 61584, 61966,
    62328, 62672, 62997, 63303, 63589, 63856, 64104, 64332,
    64540, 64729, 64898, 65048, 65177, 65287, 65376, 65446,
    65496, 65526, 65536,
};

typedef struct {
    int64_t x, y;
} vec_t;

/* The ray at d degrees, clockwise on the screen (y down). */
static vec_t ray(uint32_t d)
{
    uint32_t k;
    vec_t v;

    d %= 360;
    k = d % 90;
    switch (d / 90) {
    case 0:  v.x = sin_q16[90 - k]; v.y = sin_q16[k]; break;
    case 1:  v.x = -sin_q16[k]; v.y = sin_q16[90 - k]; break;
    case 2:  v.x = -sin_q16[90 - k]; v.y = -sin_q16[k]; break;
    default: v.x = sin_q16[k]; v.y = -sin_q16[90 - k]; break;
    }
    return v;
}

/* Positive when b lies clockwise of a on the screen, within half a turn. */
static int64_t cross(vec_t a, vec_t b)
{
    return a.x * b.y - a.y * b.x;
}

typedef struct {
    int full, wide;     /* wide: the span is above 180 degrees */
    vec_t a, b;
} sector_t;

static int in_sector(const sector_t *sc, int64_t dx, int64_t dy)
{
    vec_t p = { dx, dy };

    if (sc->full) {
        return 1;
    }
    if (!sc->wide) {
        return cross(sc->a, p) >= 0 && cross(p, sc->b) >= 0;
    }
    /* Outside only when strictly inside the gap from end to start. */
    return !(cross(sc->b, p) > 0 && cross(p, sc->a) > 0);
}

static int64_t isqrt(int64_t v)
{
    int64_t r = 0, bit = (int64_t)1 << 20;

    while (bit > 0) {
        if ((r + bit) * (r + bit) <= v) {
            r += bit;
        }
        bit >>= 1;
    }
    return r;
}

/* Half width of the disk row dy: the largest |dx| inside, or -1. */
static int64_t half_width(int64_t r2, int64_t dy)
{
    return dy * dy > r2 ? -1 : isqrt(r2 - dy * dy);
}

/*
 * The disk is every pixel with dx^2 + dy^2 <= r^2 + r, the pixels whose
 * centre lies within r + 1/2. The outline is the disk pixels that have
 * one of their four neighbours outside it. Then a pixel is kept when its
 * angle is in the range, tested against the start and end rays of the
 * sin_q16 table, both rays included.
 */
int32_t gfx_arc(gfx_surface_t *s, const pip_arc_t *a)
{
    int64_t r = a->radius, r2 = r * r + r;
    int64_t cx = a->cx, cy = a->cy, dy, dx;
    uint32_t span;
    sector_t sc;

    if (a->radius < PIP_ARC_RADIUS_MIN || a->radius > PIP_ARC_RADIUS_MAX ||
        a->start_deg > PIP_ARC_START_MAX || a->end_deg > PIP_ARC_END_MAX ||
        a->fill > PIP_ARC_FILLED) {
        return -PIP_EINVAL;
    }
    span = a->end_deg > a->start_deg ? (uint32_t)a->end_deg - a->start_deg
                                     : a->end_deg + 360u - a->start_deg;
    sc.full = span >= 360;
    sc.wide = span > 180;
    sc.a = ray(a->start_deg);
    sc.b = ray(a->end_deg);

    for (dy = max64(-r, s->st.cy0 - cy); dy <= min64(r, s->st.cy1 - 1 - cy); dy++) {
        int64_t xr = half_width(r2, dy);
        int64_t m = min64(half_width(r2, dy - 1), half_width(r2, dy + 1));
        int64_t lo = max64(-xr, s->st.cx0 - cx), hi = min64(xr, s->st.cx1 - 1 - cx);

        for (dx = lo; dx <= hi; dx++) {
            int64_t ax = dx < 0 ? -dx : dx;

            if (a->fill == PIP_ARC_OUTLINE && ax != xr && ax <= m) {
                continue;
            }
            if (in_sector(&sc, dx, dy)) {
                s->px[(cy + dy) * s->w + cx + dx] = s->st.fg;
            }
        }
    }
    return 0;
}

/* ---- blits -------------------------------------------------------------- */

int32_t gfx_blit_check(const pip_blit_t *b, int rgb_mode, uint32_t *len)
{
    uint32_t row = rgb_mode ? 4u * b->w : (b->w + 7u) / 8u;

    if (b->w < 1 || b->w > PIP_BLIT_W_MAX || b->h < 1 || b->h > PIP_BLIT_H_MAX ||
        b->flags != 0 || b->stride < row) {
        return -PIP_EINVAL;
    }
    *len = (uint32_t)b->stride * (b->h - 1u) + row;
    return 0;
}

void gfx_blit_1bpp(gfx_surface_t *s, const pip_blit_t *b, const uint8_t *data)
{
    uint32_t c = rgb(b->color);
    int32_t i, j;

    for (j = 0; j < b->h; j++) {
        const uint8_t *row = data + (uint32_t)j * b->stride;

        for (i = 0; i < b->w; i++) {
            if (row[i >> 3] & (0x80u >> (i & 7))) {
                put(s, (int64_t)b->x + i, (int64_t)b->y + j, c);
            }
        }
    }
}

void gfx_blit_rgb(gfx_surface_t *s, const pip_blit_t *b, const uint8_t *data)
{
    int32_t i, j;

    for (j = 0; j < b->h; j++) {
        const uint8_t *p = data + (uint32_t)j * b->stride;

        for (i = 0; i < b->w; i++, p += 4) {
            /* Little-endian bytes; data needs no alignment. */
            put(s, (int64_t)b->x + i, (int64_t)b->y + j,
                (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16);
        }
    }
}

/* ---- text --------------------------------------------------------------- */

int32_t gfx_text_metrics(uint32_t len, uint32_t scale)
{
    if (scale < PIP_SCALE_MIN || scale > PIP_SCALE_MAX) {
        return -PIP_EINVAL;
    }
    return (int32_t)((len * PIP_FONT_W * scale) << 16 | PIP_FONT_H * scale);
}

int32_t gfx_text(gfx_surface_t *s, int32_t x, int32_t y, const char *text, uint32_t len,
                 uint32_t scale)
{
    int64_t cw = PIP_FONT_W * (int64_t)scale, ch = PIP_FONT_H * (int64_t)scale;
    uint32_t n;

    for (n = 0; n < len; n++) {
        int64_t gx = x + cw * n;
        uint8_t c = (uint8_t)text[n];
        const uint8_t *g;
        int row, col;

        if (gx >= s->st.cx1 || gx + cw <= s->st.cx0 || y >= s->st.cy1 ||
            (int64_t)y + ch <= s->st.cy0) {
            continue;
        }
        if (s->st.bg != PIP_COLOR_NONE) {
            fill(s, gx, y, cw, ch, s->st.bg);
        }
        g = s->glyph(c >= 0x20 && c <= 0x7e ? c : '?');
        for (row = 0; row < PIP_FONT_H; row++) {
            for (col = 0; col < PIP_FONT_W; col++) {
                if (g[row] & (0x80u >> col)) {
                    fill(s, gx + col * (int64_t)scale, (int64_t)y + row * (int64_t)scale,
                         scale, scale, s->st.fg);
                }
            }
        }
    }
    return (int32_t)(len * PIP_FONT_W * scale);
}

/* ---- present ------------------------------------------------------------ */

uint32_t gfx_scale_factor(uint32_t w, uint32_t h, uint32_t dw, uint32_t dh)
{
    uint32_t fx = dw / w, fy = dh / h;

    return fx < fy ? fx : fy;
}

uint32_t gfx_scale_copy(const uint32_t *src, uint32_t w, uint32_t h, uint8_t *dst, uint32_t dw,
                        uint32_t dh, uint32_t pitch, int swap_rb)
{
    uint32_t f = gfx_scale_factor(w, h, dw, dh);
    uint32_t ox, oy, x, y, k;

    if (f == 0) {
        return 0;
    }
    ox = (dw - w * f) / 2;
    oy = (dh - h * f) / 2;
    for (y = 0; y < dh; y++) {
        uint32_t *out = (uint32_t *)(void *)(dst + y * pitch);

        if (y < oy || y >= oy + h * f) {
            for (x = 0; x < dw; x++) {
                out[x] = 0xff000000u;
            }
            continue;
        }
        for (x = 0; x < ox; x++) {
            out[x] = 0xff000000u;
        }
        for (x = ox + w * f; x < dw; x++) {
            out[x] = 0xff000000u;
        }
        out += ox;
        {
            const uint32_t *in = src + ((y - oy) / f) * w;

            for (x = 0; x < w; x++) {
                uint32_t c = in[x];

                if (swap_rb) {
                    c = (c & 0x00ff00u) | (c & 0xffu) << 16 | (c >> 16 & 0xffu);
                }
                c |= 0xff000000u;
                for (k = 0; k < f; k++) {
                    *out++ = c;
                }
            }
        }
    }
    return f;
}
