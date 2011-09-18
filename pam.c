/**
 ** Copyright (C) 1989, 1991 by Jef Poskanzer.
 ** Copyright (C) 1997, 2000, 2002 by Greg Roelofs; based on an idea by
 **                                Stefan Schneider.
 ** (C) 2011 by Kornel Lesinski.
 **
 ** Permission to use, copy, modify, and distribute this software and its
 ** documentation for any purpose and without fee is hereby granted, provided
 ** that the above copyright notice appear in all copies and that both that
 ** copyright notice and this permission notice appear in supporting
 ** documentation.  This software is provided "as is" without express or
 ** implied warranty.
 */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>

#include "pam.h"

#ifdef __LP64__
typedef unsigned long long u_register_t;
#else
typedef unsigned long u_register_t;
#endif

#ifdef __LP64__
#define PAM_EQUAL(p,q)                  \
((union {\
    f_pixel f;\
    struct {u_register_t l1, l2;} l;\
}){p}.l.l1 == \
(union {\
    f_pixel f;\
struct {u_register_t l1, l2;} l;\
}){q}.l.l1 &&\
(union {\
    f_pixel f;\
    struct {u_register_t l1, l2;} l;\
}){p}.l.l2 == \
(union {\
    f_pixel f;\
struct {u_register_t l1, l2;} l;\
}){q}.l.l2)
#else
    #define PAM_EQUAL(p,q) \
    ((p).r == (q).r && (p).g == (q).g && (p).b == (q).b && (p).a == (q).a)
#endif

static hist_item *pam_acolorhashtoacolorhist(acolorhash_table acht, int maxacolors);
static acolorhash_table pam_computeacolorhash(const rgb_pixel*const* apixels, int cols, int rows, double gamma, int maxacolors, int ignorebits, int* acolorsP);
static void pam_freeacolorhash(acolorhash_table acht);
static acolorhash_table pam_allocacolorhash(void);


/*

 libpam3.c:
 pam_computeacolorhist( )
 NOTUSED pam_addtoacolorhist( )
 pam_computeacolorhash( )
 pam_allocacolorhash( )
 pam_addtoacolorhash( )
 pam_acolorhashtoacolorhist( )
 NOTUSED pam_acolorhisttoacolorhash( )
 pam_lookupacolor( )
 pam_freeacolorhist( )
 pam_freeacolorhash( )

 libpbm1.c:
 pm_freearray( )
 pm_allocrow( )

 pam.h:
 pam_freearray( )
 */


/*===========================================================================*/


/* libpam3.c - pam (portable alpha map) utility library part 3
 **
 ** Colormap routines.
 **
 ** Copyright (C) 1989, 1991 by Jef Poskanzer.
 ** Copyright (C) 1997 by Greg Roelofs.
 **
 ** Permission to use, copy, modify, and distribute this software and its
 ** documentation for any purpose and without fee is hereby granted, provided
 ** that the above copyright notice appear in all copies and that both that
 ** copyright notice and this permission notice appear in supporting
 ** documentation.  This software is provided "as is" without express or
 ** implied warranty.
 */

/*
 #include "pam.h"
 #include "pamcmap.h"
 */

#define HASH_SIZE 30029

typedef struct mempool {
    struct mempool *next;
    size_t used;
} *mempool;

#define MEMPOOL_RESERVED ((sizeof(struct mempool)+15) & ~0xF)
#define MEMPOOL_SIZE (1<<18)

static void* mempool_new(mempool *mptr, size_t size)
{
    assert(size < MEMPOOL_SIZE-MEMPOOL_RESERVED);

    if (*mptr && ((*mptr)->used+size) <= MEMPOOL_SIZE) {
        int prevused = (*mptr)->used;
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

static void mempool_free(mempool m)
{
    while (m) {
        mempool next = m->next;
        free(m);
        m = next;
    }
}


#define ROTL(x, n) ( (u_register_t)((x) << (n)) | (u_register_t)((x) >> (sizeof(u_register_t)*8-(n))) )
inline static  unsigned long pam_hashapixel(f_pixel p)
{
    assert(sizeof(u_register_t) == sizeof(register_t));

#ifdef __LP64__
    union {
        f_pixel f;
        struct {register_t l1, l2;} l;
    } h = {p};

    assert(sizeof(h.l) == sizeof(f_pixel));

    return (ROTL(h.l.l1,13) ^ h.l.l2) % HASH_SIZE;
#else
    union {
        f_pixel f;
        struct {register_t l1, l2, l3, l4;} l;
    } h = {p};

    assert(sizeof(h.l) == sizeof(f_pixel));

    return (ROTL(h.l.l1,3) ^ ROTL(h.l.l2,7) ^ ROTL(h.l.l3,11) ^ h.l.l4) % HASH_SIZE;
#endif
}

#define PAM_SCALE(p, oldmaxval, newmaxval) ((int)(p) >= (oldmaxval) ? (newmaxval) : (int)(p) * ((newmaxval)+1) / (oldmaxval))

hist_item *pam_computeacolorhist(const rgb_pixel*const apixels[], int cols, int rows, double gamma, int maxacolors, int ignorebits, int* acolorsP)
{
    acolorhash_table acht;
    hist_item *achv;

    acht = pam_computeacolorhash(apixels, cols, rows, gamma, maxacolors, ignorebits, acolorsP);
    if (!acht) return 0;

    achv = pam_acolorhashtoacolorhist(acht, maxacolors);
    pam_freeacolorhash(acht);
    return achv;
}

static acolorhash_table pam_computeacolorhash(const rgb_pixel*const* apixels, int cols, int rows, double gamma, int maxacolors, int ignorebits, int* acolorsP)
{
    acolorhash_table acht; acolorhist_list *buckets;
    acolorhist_list achl;
    int col, row, hash;
    const int maxval = 255>>ignorebits;
    acht = pam_allocacolorhash();
    buckets = acht->buckets;
    *acolorsP = 0;

    /* Go through the entire image, building a hash table of colors. */
    for (row = 0; row < rows; ++row) {
        for (col = 0; col < cols; ++col) {

            rgb_pixel px = apixels[row][col];

            if (maxval != 255) {
                px.r = PAM_SCALE(px.r, 255, maxval); px.r = PAM_SCALE(px.r, maxval, 255);
                px.g = PAM_SCALE(px.g, 255, maxval); px.g = PAM_SCALE(px.g, maxval, 255);
                px.b = PAM_SCALE(px.b, 255, maxval); px.b = PAM_SCALE(px.b, maxval, 255);
                px.a = PAM_SCALE(px.a, 255, maxval); px.a = PAM_SCALE(px.a, maxval, 255);
            }

            f_pixel fpx = to_f(gamma, px);

            hash = pam_hashapixel(fpx);

            for (achl = buckets[hash]; achl != NULL; achl = achl->next)
                if (PAM_EQUAL(achl->ch.acolor, fpx))
                    break;
            if (achl != NULL) {
                ++(achl->ch.perceptual_weight);
            } else {
                if (++(*acolorsP) > maxacolors) {
                    pam_freeacolorhash(acht);
                    return NULL;
                }
                achl = mempool_new(&acht->mempool, sizeof(struct acolorhist_list_item));

                achl->ch.acolor = fpx;
                achl->ch.perceptual_weight = 1;
                achl->next = buckets[hash];
                buckets[hash] = achl;
            }
        }

    }
    return acht;
}

static acolorhash_table pam_allocacolorhash()
{
    mempool m = NULL;
    acolorhash_table t = mempool_new(&m, sizeof(*t));
    t->buckets = mempool_new(&m, HASH_SIZE * sizeof(t->buckets[0]));
    t->mempool = m;
    return t;
}

static hist_item *pam_acolorhashtoacolorhist(acolorhash_table acht, int maxacolors)
{
    hist_item *achv;
    acolorhist_list achl;
    int i, j;

    /* Now collate the hash table into a simple acolorhist array. */
    achv = (hist_item *) malloc(maxacolors * sizeof(achv[0]));

    /* Loop through the hash table. */
    j = 0;
    for (i = 0; i < HASH_SIZE; ++i)
        for (achl = acht->buckets[i]; achl != NULL; achl = achl->next) {
            /* Add the new entry. */
            achv[j] = achl->ch;
            ++j;
        }

    /* All done. */
    return achv;
}


static void pam_freeacolorhash(acolorhash_table acht)
{
    mempool_free(acht->mempool);
}



void pam_freeacolorhist(hist_item *achv)
{
    free(achv);
}


