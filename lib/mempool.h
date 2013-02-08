#ifndef MEMPOOL_H
#define MEMPOOL_H

#include <sys/types.h>

struct mempool;
typedef struct mempool *mempool;

void* mempool_new(mempool *mptr, unsigned int size, unsigned int capacity);
void mempool_free(mempool m);

#endif
