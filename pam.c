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
static acolorhash_table pam_computeacolorhash(const rgb_pixel*const* apixels, int cols, int rows, double gamma, int maxacolors, int ignorebits, int use_contrast, int* acolorsP);
static void pam_freeacolorhash(acolorhash_table acht);
static acolorhash_table pam_allocacolorhash(void);

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


inline static unsigned long pam_hashapixel(f_pixel px)
{
    unsigned long hash = px.a * 256.0*5.0 + px.r * 256.0*179.0 + px.g * 256.0*17.0 + px.b * 256.0*30047.0;
    return hash % HASH_SIZE;
}

#define PAM_SCALE(p, oldmaxval, newmaxval) ((int)(p) >= (oldmaxval) ? (newmaxval) : (int)(p) * ((newmaxval)+1) / (oldmaxval))

hist_item *pam_computeacolorhist(const rgb_pixel*const apixels[], int cols, int rows, double gamma, int maxacolors, int ignorebits, int use_contrast, int* acolorsP)
{
    acolorhash_table acht;
    hist_item *achv;

    acht = pam_computeacolorhash(apixels, cols, rows, gamma, maxacolors, ignorebits, use_contrast, acolorsP);
    if (!acht) return 0;

    achv = pam_acolorhashtoacolorhist(acht, maxacolors);
    pam_freeacolorhash(acht);
    return achv;
}

inline static f_pixel posterize_pixel(rgb_pixel px, int maxval, float gamma)
{
    if (maxval != 255) {
        px.r = PAM_SCALE(px.r, 255, maxval); px.r = PAM_SCALE(px.r, maxval, 255);
        px.g = PAM_SCALE(px.g, 255, maxval); px.g = PAM_SCALE(px.g, maxval, 255);
        px.b = PAM_SCALE(px.b, 255, maxval); px.b = PAM_SCALE(px.b, maxval, 255);
        px.a = PAM_SCALE(px.a, 255, maxval); px.a = PAM_SCALE(px.a, maxval, 255);
    }

        return to_f(gamma, px);
    }

float boost_from_contrast(f_pixel prev, f_pixel fpx, f_pixel next, f_pixel above, f_pixel below, float prev_boost)
{
    float r = fabsf(fpx.r*2.0f - (prev.r+next.r)),
          g = fabsf(fpx.g*2.0f - (prev.g+next.g)),
          b = fabsf(fpx.b*2.0f - (prev.b+next.b)),
          a = fabsf(fpx.a*2.0f - (prev.a+next.a));

    float r1 = fabsf(fpx.r*2.0f - (above.r+below.r)),
          g1 = fabsf(fpx.g*2.0f - (above.g+below.g)),
          b1 = fabsf(fpx.b*2.0f - (above.b+below.b)),
          a1 = fabsf(fpx.a*2.0f - (above.a+below.a));

    r = MAX(r, r1); // maximum of vertical or horizontal contrast. It's better at picking up noise.
    g = MAX(g, g1);
    b = MAX(b, b1);
    a = MAX(a, a1);

    float contrast = r+g+b+2.0f*a; // oddly a*2 works better than *3

    // 0.6-contrast avoids boosting flat areas
    contrast = MIN(1.0f,fabsf(0.6f-contrast/3.0f))*(1.0f/0.6f);
    // I want only really high contrast (noise, edges) to influence
    contrast *= contrast;

    // it's "smeared" to spread influence of edges to neighboring pixels
    return (contrast < prev_boost) ? contrast : (prev_boost+prev_boost+contrast)/3.0f;
}

static acolorhash_table pam_computeacolorhash(const rgb_pixel*const* apixels, int cols, int rows, double gamma, int maxacolors, int ignorebits, int use_contrast, int* acolorsP)
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
        f_pixel curr = posterize_pixel(apixels[row][0], maxval, gamma),
                next = posterize_pixel(apixels[row][MIN(cols-1,1)], maxval, gamma), prev;
        float boost=0.5;
        for (col = 0; col < cols; ++col) {
            prev = curr;
            curr = next;
            next = posterize_pixel(apixels[row][MIN(cols-1,col+1)], maxval, gamma);

            if (use_contrast) {
                const rgb_pixel *restrict prevline = apixels[MAX(0,row-1)];
                const rgb_pixel *restrict nextline = apixels[MIN(rows-1,row+1)];
                f_pixel above = posterize_pixel(prevline[col], maxval, gamma);
                f_pixel below = posterize_pixel(nextline[col], maxval, gamma);

                boost = boost_from_contrast(prev,curr,next,above,below,boost);
            }

            hash = pam_hashapixel(curr);

            for (achl = buckets[hash]; achl != NULL; achl = achl->next)
                if (PAM_EQUAL(achl->ch.acolor, curr))
                    break;
            if (achl != NULL) {
                achl->ch.perceptual_weight += 1.0f+boost;
            } else {
                if (++(*acolorsP) > maxacolors) {
                    pam_freeacolorhash(acht);
                    return NULL;
                }
                achl = mempool_new(&acht->mempool, sizeof(struct acolorhist_list_item));

                achl->ch.acolor = curr;
                achl->ch.perceptual_weight = 1.0f+boost;
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


