
#include "mempool.h"
#include <stdlib.h>
#include <assert.h>

#define MEMPOOL_RESERVED ((sizeof(struct mempool)+15UL) & ~0xFUL)

struct mempool {
    unsigned int used, size;
    void* (*malloc)(size_t);
    void (*free)(void*);
    struct mempool *next;
};

void* mempool_create(mempool *mptr, unsigned int size, unsigned int max_size, void* (*malloc)(size_t), void (*free)(void*))
{
    assert(size <= max_size || max_size==0);

    if (*mptr && ((*mptr)->used+size) <= (*mptr)->size) {
        unsigned int prevused = (*mptr)->used;
        (*mptr)->used += (size+15UL) & ~0xFUL;
        return ((char*)(*mptr)) + prevused;
    }

    mempool old = *mptr;
    if (!max_size) max_size = size > (1<<17) ? size : 1<<17;

    *mptr = malloc(MEMPOOL_RESERVED + max_size);
    **mptr = (struct mempool){
        .malloc = malloc,
        .free = free,
        .size = MEMPOOL_RESERVED + max_size,
        .used = MEMPOOL_RESERVED,
        .next = old,
    };

    return mempool_alloc(mptr, size, max_size);
}

void *mempool_alloc(mempool *mptr, unsigned int size, unsigned int max_size)
{
    if (((*mptr)->used+size) <= (*mptr)->size) {
        unsigned int prevused = (*mptr)->used;
        (*mptr)->used += (size+15UL) & ~0xFUL;
        return ((char*)(*mptr)) + prevused;
    }

    return mempool_create(mptr, size, max_size, (*mptr)->malloc, (*mptr)->free);
}

void mempool_destroy(mempool m)
{
    while (m) {
        mempool next = m->next;
        m->free(m);
        m = next;
    }
}
