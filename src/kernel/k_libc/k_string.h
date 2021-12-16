#ifndef __K_STRING_H__
#define __K_STRING_H__

#include <stdint.h>
#include <stddef.h>

uint32_t k_strlen(char * s);

void *k_memcpy(void *dest, const void * src, size_t n);

#endif // __K_STRING_H__