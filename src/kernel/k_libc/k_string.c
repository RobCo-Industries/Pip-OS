#include "k_string.h"

uint32_t k_strlen(char * s)
{
    uint32_t n = 0;
    while (s[n] != '\0')
    {
        n++;
    }
    return n;
}

void *k_memcpy(void *dest, const void * src, size_t n)
{
    uint8_t * bdest = (uint8_t*)dest;
    uint8_t * bsrc = (uint8_t*)src;

    for (size_t i = 0; i < n; i++) {
        bdest[i] = bsrc[i];
    }

    return dest;
}