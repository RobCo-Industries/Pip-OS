#ifndef __STRING_H__
#define __STRING_H__

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t strlen(char * s);

void *memcpy(void *dest, const void * src, size_t n);

#ifdef __cplusplus
}
#endif

#endif // __STRING_H__