/*
 * String functions. memcpy, memmove, memset and memcmp come from
 * RobCo-Core's k_string.c, built into the same archive.
 */
#include <robco/klibc.h>

#include <pipos/string.h>

size_t strlen(const char *s)
{
    return k_strlen(s);
}

size_t strnlen(const char *s, size_t max)
{
    size_t n = 0;

    while (n < max && s[n] != '\0') {
        n++;
    }
    return n;
}

int strcmp(const char *a, const char *b)
{
    return k_strcmp(a, b);
}

int strncmp(const char *a, const char *b, size_t n)
{
    return k_strncmp(a, b, n);
}

char *strchr(const char *s, int c)
{
    char ch = (char)c;

    for (;; s++) {
        if (*s == ch) {
            return (char *)s;
        }
        if (*s == '\0') {
            return NULL;
        }
    }
}

size_t strlcpy(char *dst, const char *src, size_t size)
{
    size_t len = k_strlen(src);

    if (size > 0) {
        size_t n = len < size - 1 ? len : size - 1;

        k_memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return len;
}
