/*
 * CRC-32 of libpip.a, RobCo-Core's src/lib/crc32.c under the pip_ prefix.
 *
 * CRC-32/ISO-HDLC, the zlib crc32(): reflected, polynomial 0xEDB88320,
 * init and xorout 0xFFFFFFFF. It is the CRC of the image header, so a
 * ROM can check a holotape or a save the same way. pip_crc32("123456789", 9)
 * is 0xCBF43926.
 */
#ifndef PIPOS_CRC32_H
#define PIPOS_CRC32_H

#include <stddef.h>
#include <stdint.h>

/* The CRC of `n` bytes at `data`. */
uint32_t pip_crc32(const void *data, size_t n);

/*
 * Streams: start with crc = 0 and pass each result back in. The result
 * after the last piece equals pip_crc32() of all the bytes.
 */
uint32_t pip_crc32_update(uint32_t crc, const void *data, size_t n);

#endif
