#ifndef __STRING_H__
#define __STRING_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t strlen(char * s);
void strrev(char * s);

#ifdef __cplusplus
}
#endif

#endif // __STRING_H__