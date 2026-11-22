#include "rt.h"

/* One line is built here, then logged in one call. */
static char line[PIP_LOG_MAX];
static size_t len;

static void put(const char *s)
{
    while (*s != '\0' && len < sizeof(line) - 2) {
        line[len++] = *s++;
    }
}

void rt_begin(const char *ev)
{
    len = 0;
    put("@RC stage=testrom ev=");
    put(ev);
}

void rt_hex(const char *key, uint32_t v)
{
    static const char digits[] = "0123456789abcdef";
    char buf[11];
    int i;

    buf[0] = '0';
    buf[1] = 'x';
    for (i = 0; i < 8; i++) {
        buf[2 + i] = digits[(v >> (28 - 4 * i)) & 0xfu];
    }
    buf[10] = '\0';
    put(" ");
    put(key);
    put("=");
    put(buf);
}

void rt_dec(const char *key, int32_t v)
{
    char buf[12];
    int i = 11;
    uint32_t u = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;

    buf[i] = '\0';
    do {
        buf[--i] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u != 0);
    if (v < 0) {
        buf[--i] = '-';
    }
    put(" ");
    put(key);
    put("=");
    put(&buf[i]);
}

void rt_end(void)
{
    line[len++] = '\n';
    line[len] = '\0';
    pip_log(line);
}

void *memset(void *d, int c, size_t n)
{
    unsigned char *p = d;

    while (n-- > 0) {
        *p++ = (unsigned char)c;
    }
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    unsigned char *p = d;
    const unsigned char *q = s;

    while (n-- > 0) {
        *p++ = *q++;
    }
    return d;
}
