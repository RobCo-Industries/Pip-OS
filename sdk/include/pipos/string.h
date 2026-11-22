/*
 * The C string functions of libpip.a, with their standard names and
 * meaning. GCC emits calls to memcpy, memmove, memset and memcmp on its
 * own, so they must exist even if the image never calls them.
 */
#ifndef PIPOS_STRING_H
#define PIPOS_STRING_H

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);    /* the areas may overlap */
void *memset(void *dst, int value, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t max);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strchr(const char *s, int c);

/*
 * Copies src into dst, at most size - 1 bytes, and always ends dst with
 * a NUL when size > 0. Returns strlen(src): a result >= size means the
 * copy was cut.
 */
size_t strlcpy(char *dst, const char *src, size_t size);

#endif
