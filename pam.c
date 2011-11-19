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

#include <stdlib.h>

#include "pam.h"
#include "mempool.h"

#define PAM_EQUAL(p,q) ((p).a == (q).a && (p).r == (q).r && (p).g == (q).g && (p).b == (q).b)

static hist *pam_acolorhashtoacolorhist(acolorhash_table acht, int maxacolors);
static acolorhash_table pam_computeacolorhash(const rgb_pixel*const* apixels, int cols, int rows, double gamma, int maxacolors, int ignorebits, const float *importance, int* acolorsP);
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

inline static unsigned long pam_hashapixel(f_pixel px)
{
    unsigned long hash = px.a * 256.0*5.0 + px.r * 256.0*179.0 + px.g * 256.0*17.0 + px.b * 256.0*30047.0;
    return hash % HASH_SIZE;
}

/**
 * Builds color histogram no larger than maxacolors. Ignores (posterizes) ignorebits lower bits in each color.
 * perceptual_weight of each entry is increased by value from importance_map
 */
hist *pam_computeacolorhist(const rgb_pixel*const apixels[], int cols, int rows, double gamma, int maxacolors, int ignorebits, const float *importance_map)
{
    acolorhash_table acht;
    hist *achv;

    int hist_size=0;
    acht = pam_computeacolorhash(apixels, cols, rows, gamma, maxacolors, ignorebits, importance_map, &hist_size);
    if (!acht) return 0;

    achv = pam_acolorhashtoacolorhist(acht, hist_size);
    pam_freeacolorhash(acht);
    return achv;
}

inline static f_pixel posterize_pixel(rgb_pixel px, int maxval, float gamma)
{
    if (maxval == 255) {
        return to_f(gamma, px);
    } else {
        return to_f_scalar(gamma, (f_pixel){
            .a = (px.a * maxval / 255) / (float)maxval,
            .r = (px.r * maxval / 255) / (float)maxval,
            .g = (px.g * maxval / 255) / (float)maxval,
            .b = (px.b * maxval / 255) / (float)maxval,
        });
    }
}

static acolorhash_table pam_computeacolorhash(const rgb_pixel*const* apixels, int cols, int rows, double gamma, int maxacolors, int ignorebits, const float *importance_map, int* acolorsP)
{
    acolorhash_table acht;
    struct acolorhist_list_item *achl, **buckets;
    int col, row, hash;
    const int maxval = 255>>ignorebits;
    acht = pam_allocacolorhash();
    buckets = acht->buckets;
    int colors=0;

    /* Go through the entire image, building a hash table of colors. */
    for (row = 0; row < rows; ++row) {

        float boost=1.0;
        for (col = 0; col < cols; ++col) {
            if (importance_map) {
                boost = 0.5+*importance_map++;
            }

            f_pixel curr = posterize_pixel(apixels[row][col], maxval, gamma);
            hash = pam_hashapixel(curr);

            for (achl = buckets[hash]; achl != NULL; achl = achl->next)
                if (PAM_EQUAL(achl->acolor, curr))
                    break;
            if (achl != NULL) {
                achl->perceptual_weight += boost;
            } else {
                if (++colors > maxacolors) {
                    pam_freeacolorhash(acht);
                    return NULL;
                }
                achl = mempool_new(&acht->mempool, sizeof(struct acolorhist_list_item));

                achl->acolor = curr;
                achl->perceptual_weight = boost;
                achl->next = buckets[hash];
                buckets[hash] = achl;
            }
        }

    }
    *acolorsP = colors;
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

static hist *pam_acolorhashtoacolorhist(acolorhash_table acht, int hist_size)
{
    hist *hist;
    struct acolorhist_list_item *achl;
    int i, j;

    hist = malloc(sizeof(hist));
    hist->achv = malloc(hist_size * sizeof(hist->achv[0]));
    hist->size = hist_size;

    /* Loop through the hash table. */
    j = 0;
    for (i = 0; i < HASH_SIZE; ++i)
        for (achl = acht->buckets[i]; achl != NULL; achl = achl->next) {
            /* Add the new entry. */
            hist->achv[j].acolor = achl->acolor;
            hist->achv[j].adjusted_weight = hist->achv[j].perceptual_weight = achl->perceptual_weight;
            ++j;
        }

    /* All done. */
    return hist;
}


static void pam_freeacolorhash(acolorhash_table acht)
{
    mempool_free(acht->mempool);
}

void pam_freeacolorhist(hist *hist)
{
    free(hist->achv);
    free(hist);
}

colormap *pam_colormap(int colors)
{
    colormap *map = malloc(sizeof(colormap));
    map->palette = calloc(colors, sizeof(map->palette[0]));
    map->colors = colors;
    return map;
}

void pam_freecolormap(colormap *c)
{
    free(c->palette); free(c);
}


