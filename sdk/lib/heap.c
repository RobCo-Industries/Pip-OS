/*
 * The heap: RobCo-Core's first-fit allocator in alloc.c. The first call
 * sets it up on the region mem_info names, which is the startup block's
 * heap_base and heap_size.
 */
#include <stdint.h>

#include <robco/alloc.h>

#include <pipos/heap.h>
#include <pipos/syscall.h>

static int heap_ready;

static void heap_setup(void)
{
    pip_mem_info_t mi;

    if (heap_ready) {
        return;
    }
    heap_ready = 1;
    if (pip_mem_info(&mi) == 0) {
        k_heap_init((void *)(uintptr_t)mi.heap_base, mi.heap_size);
    } else {
        k_heap_init(NULL, 0);
    }
}

void pip_heap_init(void *base, size_t size)
{
    heap_ready = 1;
    k_heap_init(base, size);
}

void *pip_malloc(size_t n)
{
    heap_setup();
    return k_malloc(n);
}

void *pip_calloc(size_t n, size_t size)
{
    heap_setup();
    return k_calloc(n, size);
}

void pip_free(void *p)
{
    heap_setup();
    k_free(p);
}

size_t pip_heap_free(void)
{
    k_heap_stats_t st;

    heap_setup();
    k_heap_stats(&st);
    return st.free;
}
