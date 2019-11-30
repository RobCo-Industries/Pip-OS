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