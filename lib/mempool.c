/*
© 2011-2015 by Kornel Lesiński.

This file is part of libimagequant.

libimagequant is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

libimagequant is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with libimagequant. If not, see <http://www.gnu.org/licenses/>.
*/

#include "libimagequant.h"
#include "mempool.h"
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>

#define ALIGN_MASK 15UL
#define MEMPOOL_RESERVED ((sizeof(struct mempool)+ALIGN_MASK) & ~ALIGN_MASK)

struct mempool {
    unsigned int used, size;
    void* (*malloc)(size_t);
    void (*free)(void*);
    struct mempool *next;
};
LIQ_PRIVATE void* mempool_create(mempool *mptr, const unsigned int size, unsigned int max_size, void* (*malloc)(size_t), void (*free)(void*))
{
    if (*mptr && ((*mptr)->used+size) <= (*mptr)->size) {
        unsigned int prevused = (*mptr)->used;
        (*mptr)->used += (size+15UL) & ~0xFUL;
        return ((char*)(*mptr)) + prevused;
    }

    mempool old = *mptr;
    if (!max_size) max_size = (1<<17);
    max_size = size+ALIGN_MASK > max_size ? size+ALIGN_MASK : max_size;

    *mptr = malloc(MEMPOOL_RESERVED + max_size);
    if (!*mptr) return NULL;
    **mptr = (struct mempool){
        .malloc = malloc,
        .free = free,
        .size = MEMPOOL_RESERVED + max_size,
        .used = sizeof(struct mempool),
        .next = old,
    };
    uintptr_t mptr_used_start = (uintptr_t)(*mptr) + (*mptr)->used;
    (*mptr)->used += (ALIGN_MASK + 1 - (mptr_used_start & ALIGN_MASK)) & ALIGN_MASK; // reserve bytes required to make subsequent allocations aligned
    assert(!(((uintptr_t)(*mptr) + (*mptr)->used) & ALIGN_MASK));

    return mempool_alloc(mptr, size, size);
}

LIQ_PRIVATE void* mempool_alloc(mempool *mptr, const unsigned int size, const unsigned int max_size)
{
    if (((*mptr)->used+size) <= (*mptr)->size) {
        unsigned int prevused = (*mptr)->used;
        (*mptr)->used += (size + ALIGN_MASK) & ~ALIGN_MASK;
        return ((char*)(*mptr)) + prevused;
    }

    return mempool_create(mptr, size, max_size, (*mptr)->malloc, (*mptr)->free);
}

LIQ_PRIVATE void mempool_destroy(mempool m)
{
    while (m) {
        mempool next = m->next;
        m->free(m);
        m = next;
    }
}
