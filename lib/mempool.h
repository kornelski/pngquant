#ifndef MEMPOOL_H
#define MEMPOOL_H

#include <stddef.h>

struct mempool;
typedef struct mempool *mempool;

void* mempool_create(mempool *mptr, unsigned int size, unsigned int capacity, void* (*malloc)(size_t), void (*free)(void*));
void* mempool_alloc(mempool *mptr, unsigned int size, unsigned int capacity);
void mempool_destroy(mempool m);

#endif
