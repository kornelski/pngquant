
#include "mempool.h"
#include <stdlib.h>
#include <assert.h>

#define MEMPOOL_RESERVED ((sizeof(struct mempool)+15UL) & ~0xFUL)

struct mempool {
    struct mempool *next;
    unsigned int used, size;
};

void* mempool_new(mempool *mptr, unsigned int size, unsigned int max_size)
{
    assert(size <= max_size || max_size==0);

    if (*mptr && ((*mptr)->used+size) <= (*mptr)->size) {
        unsigned int prevused = (*mptr)->used;
        (*mptr)->used += (size+15UL) & ~0xFUL;
        return ((char*)(*mptr)) + prevused;
    }

    mempool old = *mptr;
    if (!max_size) max_size = size > (1<<17) ? size : 1<<17;

    (*mptr) = (mempool)calloc(MEMPOOL_RESERVED + max_size, 1);
    (*mptr)->size = MEMPOOL_RESERVED + max_size;
    (*mptr)->used = MEMPOOL_RESERVED;
    (*mptr)->next = old;

    return mempool_new(mptr, size, max_size);
}

void mempool_free(mempool m)
{
    while (m) {
        mempool next = m->next;
        free(m);
        m = next;
    }
}
