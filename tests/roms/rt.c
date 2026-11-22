#include "rt.h"

/* One line is built here, then logged in one call. */
static char line[PIP_LOG_MAX];
static size_t len;

/* Appends formatted text, keeping room for "\n" and the NUL. */
static void put(const char *fmt, const char *key, uint32_t v)
{
    size_t room = sizeof(line) - 1 - len;
    int n = pip_snprintf(line + len, room, fmt, key, v);

    len += (size_t)n < room ? (size_t)n : room - 1;
}

void rt_begin(const char *ev)
{
    len = 0;
    put("@RC stage=testrom ev=%s", ev, 0);
}

void rt_hex(const char *key, uint32_t v)
{
    put(" %s=0x%08x", key, v);
}

void rt_dec(const char *key, int32_t v)
{
    put(" %s=%d", key, (uint32_t)v);
}

void rt_end(void)
{
    line[len++] = '\n';
    line[len] = '\0';
    pip_log(line);
}
