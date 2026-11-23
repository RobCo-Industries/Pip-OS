/* Marker lines for self-checks, see pipos/selftest.h. */
#include <stddef.h>

#include <pipos/printf.h>
#include <pipos/selftest.h>
#include <pipos/syscall.h>

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

void pip_marker_begin(const char *ev)
{
    len = 0;
    put("@RC stage=testrom ev=%s", ev, 0);
}

void pip_marker_hex(const char *key, uint32_t v)
{
    put(" %s=0x%08x", key, v);
}

void pip_marker_dec(const char *key, int32_t v)
{
    put(" %s=%d", key, (uint32_t)v);
}

void pip_marker_end(void)
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
    pip_marker_begin("check");
    put(" name=%s", name, 0);
    pip_marker_dec("ok", ok ? 1 : 0);
    return ok;
}

int pip_check(const char *name, int ok)
{
    ok = check_line(name, ok);
    pip_marker_end();
    return ok;
}

int pip_check_eq(const char *name, int32_t got, int32_t expected)
{
    int ok = check_line(name, got == expected);

    pip_marker_dec("got", got);
    pip_marker_dec("expected", expected);
    pip_marker_end();
    return ok;
}

int pip_checks_done(void)
{
    pip_marker_begin("checks");
    pip_marker_dec("total", checks_total);
    pip_marker_dec("failed", checks_failed);
    pip_marker_end();
    return checks_failed;
}

void pip_marker_time(const char *ev)
{
    pip_marker_begin(ev);
    pip_marker_dec("t_ms", (int32_t)(pip_ticks_ms() & 0x7fffffffu));
    pip_marker_end();
}
