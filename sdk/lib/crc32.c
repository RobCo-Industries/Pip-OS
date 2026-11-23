/* CRC-32 on top of RobCo-Core's crc32.c, built into the same archive. */
#include <robco/crc32.h>

#include <pipos/crc32.h>

uint32_t pip_crc32(const void *data, size_t n)
{
    return crc32(data, n);
}

uint32_t pip_crc32_update(uint32_t crc, const void *data, size_t n)
{
    return crc32_update(crc, data, n);
}
