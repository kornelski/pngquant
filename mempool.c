
#include "mempool.h"
#include <stdlib.h>
#include <assert.h>

#define MEMPOOL_RESERVED ((sizeof(struct mempool)+15) & ~0xF)
#define MEMPOOL_SIZE (1<<19)

struct mempool {
    struct mempool *next;
    size_t used;
};

void* mempool_new(mempool *mptr, size_t size)
{
    assert(size < MEMPOOL_SIZE-MEMPOOL_RESERVED);

    if (*mptr && ((*mptr)->used+size) <= MEMPOOL_SIZE) {
        size_t prevused = (*mptr)->used;
        (*mptr)->used += (size+15) & ~0xF;
        return ((char*)(*mptr)) + prevused;
    }

    mempool old = mptr ? *mptr : NULL;
    char *mem = calloc(MEMPOOL_SIZE, 1);

    (*mptr) = (mempool)mem;
    (*mptr)->used = MEMPOOL_RESERVED;
    (*mptr)->next = old;

    return mempool_new(mptr, size);
}

void mempool_free(mempool m)
{
    while (m) {
        mempool next = m->next;
        free(m);
        m = next;
    }
}


