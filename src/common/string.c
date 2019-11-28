#include <common/string.h>

uint32_t strlen(char * s)
{
    uint32_t n = 0;
    while (*s != '\0')
    {
        s++;
        n++;
    }
    return n;
}

void strrev(char * s)
{
    uint32_t i, j;
    char c;

    for (i = 0, j = strlen(s)-1; i<j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}
