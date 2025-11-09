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

int k_memcmp(const void *s1, const void *s2, size_t n)
{
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return (int)(p1[i] - p2[i]);
        }
    }

    return 0;
}