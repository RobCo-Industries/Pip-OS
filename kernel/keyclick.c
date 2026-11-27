#include "keyclick.h"

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

void pipos_type_char(char c, uint32_t delay, const struct pipos_type_ops *ops)
{
    ops->draw(c);
    ops->delay(delay);
}
