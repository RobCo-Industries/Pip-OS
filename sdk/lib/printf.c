/* Formatted output on top of RobCo-Core's k_vsnprintf. */
#include <robco/klibc.h>

#include <pipos/printf.h>
#include <pipos/syscall.h>

int pip_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    return k_vsnprintf(buf, size, fmt, ap);
}

int pip_snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = k_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int pip_printf(const char *fmt, ...)
{
    char out[PIP_PRINTF_MAX + 1];
    char piece[PIP_LOG_MAX];
    va_list ap;
    int32_t err;
    int len, done, rc, i;

    va_start(ap, fmt);
    len = k_vsnprintf(out, sizeof(out), fmt, ap);
    va_end(ap);
    if (len > PIP_PRINTF_MAX) {
        len = PIP_PRINTF_MAX;
    }
    for (done = 0; done < len; done += rc) {
        rc = len - done;
        if (rc > PIP_LOG_MAX - 1) {
            rc = PIP_LOG_MAX - 1;
        }
        k_memcpy(piece, out + done, (size_t)rc);
        piece[rc] = '\0';
        /* The log call stops at a NUL; print an embedded one as '?'. */
        for (i = 0; i < rc; i++) {
            if (piece[i] == '\0') {
                piece[i] = '?';
            }
        }
        err = pip_log(piece);
        if (err < 0) {
            return err;
        }
    }
    return len;
}
