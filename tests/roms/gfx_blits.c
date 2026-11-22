/* Screen golden: 1bpp and RGB blits, odd widths, wide strides, clipped on every side. */
#include "rt.h"

static uint8_t mono[16 * 40];
static uint32_t rgb[50 * 40];

static void both(int x, int y)
{
    pip_blit_t b = { (int16_t)x, (int16_t)y, 37, 40, 16, 0, (uint32_t)(uintptr_t)mono, 0xffe0a0 };
    pip_blit_t c = { (int16_t)(x + 40), (int16_t)y, 33, 40, 50 * 4, 0,
                     (uint32_t)(uintptr_t)rgb, 0 };

    pip_blit_1bpp(&b);
    pip_blit_rgb(&c);
}

int pip_main(const pipos_startup_t *startup)
{
    int i, j;

    (void)startup;
    for (j = 0; j < 40; j++) {
        for (i = 0; i < 16; i++) {
            mono[j * 16 + i] = (uint8_t)((j & 1 ? 0xaa : 0x55) ^ (i * 17 + j));
        }
        for (i = 0; i < 50; i++) {
            rgb[j * 50 + i] = (uint32_t)(i * 5) << 16 | (uint32_t)(j * 6) << 8 | 0x80;
        }
    }
    pip_clear(0x202040);
    both(20, 20);
    both(120, 100);
    both(-20, 60);     /* left */
    both(260, 140);    /* right */
    both(150, -25);    /* top */
    both(40, 220);     /* bottom */
    pip_present();
    rt_begin("drawn");
    rt_end();
    for (;;) {
        pip_sleep_ms(1000);
    }
}
