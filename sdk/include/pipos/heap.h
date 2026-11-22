/*
 * The heap of libpip.a, in the region the startup block names:
 * heap_base and heap_size, 0x007F0000 bytes for a ROM. The first call
 * sets it up from mem_info.
 *
 * Blocks are 8-byte aligned. Each block costs a 16-byte header, and a
 * request is rounded up to a multiple of 8, at least 8. The largest
 * request that can work is heap_size - 16. First fit; freed neighbours
 * merge.
 * Not thread-safe: there is one thread.
 */
#ifndef PIPOS_HEAP_H
#define PIPOS_HEAP_H

#include <stddef.h>

/* NULL when n is 0 or no free block is large enough. */
void *pip_malloc(size_t n);

/* n * size bytes set to 0. NULL when n * size overflows or does not fit. */
void *pip_calloc(size_t n, size_t size);

/* Frees p. NULL, and pointers the heap did not hand out, are ignored. */
void pip_free(void *p);

/* Free bytes in all free blocks, headers not counted. One block may hold less. */
size_t pip_heap_free(void);

/*
 * Puts the heap on [base, base + size) instead and forgets every block.
 * Optional: the first call sets up the default heap.
 */
void pip_heap_init(void *base, size_t size);

#endif
