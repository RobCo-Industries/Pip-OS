#include "keyclick.h"

uint32_t pipos_click_hz(char c, unsigned pos)
{
    unsigned char u = (unsigned char)c;

    if (u <= ' ' || u > '~') {
        return 0;
    }
    return PIPOS_CLICK_BASE_HZ + PIPOS_CLICK_STEP_HZ * ((u + pos) % PIPOS_CLICK_STEPS);
}

uint32_t pipos_type_ms(const char *text, uint32_t delay)
{
    uint32_t n = 0;

    if (delay == 0) {
        return 0;
    }
    while (text[n] != '\0') {
        n++;
    }
    return n * delay;
}

int pipos_click_on(uint32_t delay, int key_click)
{
    return delay > 0 && key_click;
}

void pipos_type_char(char c, unsigned pos, uint32_t delay, int key_click,
                     const struct pipos_type_ops *ops)
{
    uint32_t hz = key_click ? pipos_click_hz(c, pos) : 0;

    if (hz != 0) {
        ops->wait();
    }
    ops->draw(c);
    if (hz != 0) {
        ops->tone(hz, PIPOS_CLICK_MS);
    }
    ops->delay(delay);
}
