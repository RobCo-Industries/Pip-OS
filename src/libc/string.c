#include <string.h>
#include <k_string.h>

uint32_t strlen(char * s)
{
    return k_strlen(s);
}

void *memcpy(void *dest, const void * src, size_t n)
{
    k_memcpy(dest, src, n);
}