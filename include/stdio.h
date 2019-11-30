#ifndef __STDIO_H__
#define __STDIO_H__

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

char getc(void);
void putc(char c);
void puts(const char * s);

uint32_t strlen(char * s);
void strrev(char * s);
void itoa(int n, char * s);


#ifdef __cplusplus
}
#endif

#endif // __STDIO_H__