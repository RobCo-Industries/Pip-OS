#include "k_stdlib.h"
#include "k_string.h"

static void strrev(char * s)
{
    uint32_t i, j;
    char c;

    for (i = 0, j = k_strlen(s)-1; i<j; i++, j--) {
        c = s[i];
        s[i] = s[j];
        s[j] = c;
    }
}

void k_itoa(int n, char * s)
{
    int i, sign;

    if ((sign = n) < 0)
        n = -n;
    i = 0;
    do {
        s[i++] = n % 10 + '0';
    } while ((n /= 10) > 0);
    if (sign < 0)
        s[i++] = '-';
    s[i] = '\0';
    strrev(s);
}