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

static int checks_total, checks_failed;

static int check_line(const char *name, int ok)
{
    checks_total++;
    checks_failed += !ok;
    rt_begin("check");
    put(" name=%s", name, 0);
    rt_dec("ok", ok ? 1 : 0);
    return ok;
}

int rt_check(const char *name, int ok)
{
    ok = check_line(name, ok);
    rt_end();
    return ok;
}

int rt_check_eq(const char *name, int32_t got, int32_t expected)
{
    int ok = check_line(name, got == expected);

    rt_dec("got", got);
    rt_dec("expected", expected);
    rt_end();
    return ok;
}

int rt_checks_done(void)
{
    rt_begin("checks");
    rt_dec("total", checks_total);
    rt_dec("failed", checks_failed);
    rt_end();
    return checks_failed;
}
