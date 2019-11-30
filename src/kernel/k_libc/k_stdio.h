#ifndef __K_STDIO_H__
#define __K_STDIO_H__

void k_putchar(char c);

int k_printf(const char *format, ...);
int k_sprintf(char *out, const char *format, ...);

#endif // __K_STDIO_H__