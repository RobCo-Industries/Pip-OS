#include <kernel/atags.h>

#define tag_next(t) ((atag_t *)((uint32_t *)(t) + (t)->header.size))

int get_mem(atag_t tags[], atag_mem_t *mem) {
    int result = -1;
    while (tags->header.tag != ATAG_NONE) {
       if (tags->header.tag == ATAG_MEM) {
           result = 0;
           mem->size = tags->mem.size;
           mem->start = tags->mem.start;
       }
       tags = tag_next(tags);
   }
   return result;
}