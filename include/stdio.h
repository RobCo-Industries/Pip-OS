#ifndef __STDIO_H__
#define __STDIO_H__

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

char getchar(void);
void putcchar(char c);
void puts(const char * s);

#ifdef __cplusplus
}
#endif

#endif // __STDIO_H__