/*
 * Formatted output of libpip.a.
 *
 * Conversions: %d %u %x %X %c %s %p %%, as in C, with the '-' and '0'
 * flags, a field width and a precision. '*' reads the width or precision
 * from an int argument. Integer precision is the minimum digit count.
 * A NULL string prints "(null)". %p prints 0x and 8 hex digits and
 * ignores flags, width and precision.
 * Not supported: '+', ' ' and '#' flags, length modifiers such as l or
 * ll, and floating point. An unknown conversion prints as it is written.
 */
#ifndef PIPOS_PRINTF_H
#define PIPOS_PRINTF_H

#include <stdarg.h>
#include <stddef.h>

/*
 * Writes at most size - 1 characters and a NUL into buf, when size > 0.
 * Returns the length of the whole output: a result >= size means it was cut.
 * A NUL from %c with 0 counts in the result but ends the string in buf.
 */
int pip_snprintf(char *buf, size_t size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int pip_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);

/* Longest output of one pip_printf call; the rest is dropped. */
#define PIP_PRINTF_MAX  1023

/*
 * Formats into a buffer, then prints it with the log call, in pieces of
 * at most 255 bytes. A NUL in the output, as from %c with 0, prints as '?'.
 * Returns the number of bytes printed, at most PIP_PRINTF_MAX, or the
 * negative error of the log call that failed.
 */
int pip_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
