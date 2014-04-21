/* pam.c - pam (portable alpha map) utility library
**
** Copyright (C) 1989, 1991 by Jef Poskanzer.
** Copyright (C) 1997, 2000, 2002 by Greg Roelofs; based on an idea by
**                                Stefan Schneider.
** © 2009-2013 by Kornel Lesinski.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <stdlib.h>
#include <string.h>

#include "libimagequant.h"
#include "pam.h"
#include "mempool.h"

LIQ_PRIVATE bool pam_computeacolorhash(struct acolorhash_table *acht, const rgba_pixel *const pixels[], unsigned int cols, unsigned int rows, const unsigned char *importance_map)
{
    const unsigned int maxacolors = acht->maxcolors, ignorebits = acht->ignorebits;
    const unsigned int channel_mask = 255U>>ignorebits<<ignorebits;
    const unsigned int channel_hmask = (255U>>ignorebits) ^ 0xFFU;
    const unsigned int posterize_mask = channel_mask << 24 | channel_mask << 16 | channel_mask << 8 | channel_mask;
    const unsigned int posterize_high_mask = channel_hmask << 24 | channel_hmask << 16 | channel_hmask << 8 | channel_hmask;
    struct acolorhist_arr_head *const buckets = acht->buckets;

    unsigned int colors = acht->colors;
    const unsigned int hash_size = acht->hash_size;

    const unsigned int stacksize = sizeof(acht->freestack)/sizeof(acht->freestack[0]);
    struct acolorhist_arr_item **freestack = acht->freestack;
    unsigned int freestackp=acht->freestackp;

    /* Go through the entire image, building a hash table of colors. */
    for(unsigned int row = 0; row < rows; ++row) {

        float boost=1.0;
        for(unsigned int col = 0; col < cols; ++col) {
            if (importance_map) {
                boost = 0.5f+ (double)*importance_map++/255.f;
            }

            // RGBA color is casted to long for easier hasing/comparisons
            union rgba_as_int px = {pixels[row][col]};
            unsigned int hash;
            if (!px.rgba.a) {
                // "dirty alpha" has different RGBA values that end up being the same fully transparent color
                px.l=0; hash=0;
            } else {
                // mask posterizes all 4 channels in one go
                px.l = (px.l & posterize_mask) | ((px.l & posterize_high_mask) >> (8-ignorebits));
                // fancier hashing algorithms didn't improve much
                hash = px.l % hash_size;
            }

            /* head of the hash function stores first 2 colors inline (achl->used = 1..2),
               to reduce number of allocations of achl->other_items.
             */
            struct acolorhist_arr_head *achl = &buckets[hash];
            if (achl->inline1.color.l == px.l && achl->used) {
                achl->inline1.perceptual_weight += boost;
                continue;
            }
            if (achl->used) {
                if (achl->used > 1) {
                    if (achl->inline2.color.l == px.l) {
                        achl->inline2.perceptual_weight += boost;
                        continue;
                    }
                    // other items are stored as an array (which gets reallocated if needed)
                    struct acolorhist_arr_item *other_items = achl->other_items;
                    unsigned int i = 0;
                    for (; i < achl->used-2; i++) {
                        if (other_items[i].color.l == px.l) {
                            other_items[i].perceptual_weight += boost;
                            goto continue_outer_loop;
                        }
                    }

                    // the array was allocated with spare items
                    if (i < achl->capacity) {
                        other_items[i] = (struct acolorhist_arr_item){
                            .color = px,
                            .perceptual_weight = boost,
                        };
                        achl->used++;
                        ++colors;
                        continue;
                    }

                    if (++colors > maxacolors) {
                        acht->colors = colors;
                        acht->freestackp = freestackp;
                        return false;
                    }

                    struct acolorhist_arr_item *new_items;
                    unsigned int capacity;
                    if (!other_items) { // there was no array previously, alloc "small" array
                        capacity = 8;
                        if (freestackp <= 0) {
                            // estimate how many colors are going to be + headroom
                            const int mempool_size = ((acht->rows + rows-row) * 2 * colors / (acht->rows + row + 1) + 1024) * sizeof(struct acolorhist_arr_item);
                            new_items = mempool_alloc(&acht->mempool, sizeof(struct acolorhist_arr_item)*capacity, mempool_size);
                        } else {
                            // freestack stores previously freed (reallocated) arrays that can be reused
                            // (all pesimistically assumed to be capacity = 8)
                            new_items = freestack[--freestackp];
                        }
                    } else {
                        // simply reallocs and copies array to larger capacity
                        capacity = achl->capacity*2 + 16;
                        if (freestackp < stacksize-1) {
                            freestack[freestackp++] = other_items;
                        }
                        const int mempool_size = ((acht->rows + rows-row) * 2 * colors / (acht->rows + row + 1) + 32*capacity) * sizeof(struct acolorhist_arr_item);
                        new_items = mempool_alloc(&acht->mempool, sizeof(struct acolorhist_arr_item)*capacity, mempool_size);
                        if (!new_items) return false;
                        memcpy(new_items, other_items, sizeof(other_items[0])*achl->capacity);
                    }

                    achl->other_items = new_items;
                    achl->capacity = capacity;
                    new_items[i] = (struct acolorhist_arr_item){
                        .color = px,
                        .perceptual_weight = boost,
                    };
                    achl->used++;
                } else {
                    // these are elses for first checks whether first and second inline-stored colors are used
                    achl->inline2.color.l = px.l;
                    achl->inline2.perceptual_weight = boost;
                    achl->used = 2;
                    ++colors;
                }
            } else {
                achl->inline1.color.l = px.l;
                achl->inline1.perceptual_weight = boost;
                achl->used = 1;
                ++colors;
            }

            continue_outer_loop:;
        }

    }
    acht->colors = colors;
    acht->rows += rows;
    acht->freestackp = freestackp;
    return true;
}

LIQ_PRIVATE struct acolorhash_table *pam_allocacolorhash(unsigned int maxcolors, unsigned int surface, unsigned int ignorebits, void* (*malloc)(size_t), void (*free)(void*))
{
    const unsigned int estimated_colors = MIN(maxcolors, surface/(ignorebits + (surface > 512*512 ? 5 : 4)));
    const unsigned int hash_size = estimated_colors < 66000 ? 6673 : (estimated_colors < 200000 ? 12011 : 24019);

    mempool m = NULL;
    const unsigned int mempool_size = sizeof(struct acolorhash_table) + hash_size * sizeof(struct acolorhist_arr_head) + estimated_colors * sizeof(struct acolorhist_arr_item);
    struct acolorhash_table *t = mempool_create(&m, sizeof(*t), mempool_size, malloc, free);
    if (!t) return NULL;
    void* buckets = mempool_alloc(&m, hash_size * sizeof(struct acolorhist_arr_head), 0);
    *t = (struct acolorhash_table){
        .buckets = buckets,
        .mempool = m,
        .hash_size = hash_size,
        .maxcolors = maxcolors,
        .ignorebits = ignorebits,
    };
    if (!t->buckets) return NULL;
    memset(t->buckets, 0, hash_size * sizeof(struct acolorhist_arr_head));
    return t;
}

#define PAM_ADD_TO_HIST(entry) { \
    hist->achv[j].acolor = to_f(gamma_lut, entry.color.rgba); \
    hist->achv[j].adjusted_weight = hist->achv[j].perceptual_weight = entry.perceptual_weight; \
    ++j; \
    total_weight += entry.perceptual_weight; \
}

LIQ_PRIVATE histogram *pam_acolorhashtoacolorhist(const struct acolorhash_table *acht, const double gamma, void* (*malloc)(size_t), void (*free)(void*))
{
    histogram *hist = malloc(sizeof(hist[0]));
    if (!hist || !acht) return NULL;
    *hist = (histogram){
        .achv = malloc(acht->colors * sizeof(hist->achv[0])),
        .size = acht->colors,
        .free = free,
        .ignorebits = acht->ignorebits,
    };
    if (!hist->achv) return NULL;

    float gamma_lut[256];
    to_f_set_gamma(gamma_lut, gamma);

    double total_weight=0;
    for(unsigned int j=0, i=0; i < acht->hash_size; ++i) {
        const struct acolorhist_arr_head *const achl = &acht->buckets[i];
        if (achl->used) {
            PAM_ADD_TO_HIST(achl->inline1);

            if (achl->used > 1) {
                PAM_ADD_TO_HIST(achl->inline2);

                for(unsigned int k=0; k < achl->used-2; k++) {
                    PAM_ADD_TO_HIST(achl->other_items[k]);
                }
            }
        }
    }

    hist->total_perceptual_weight = total_weight;
    return hist;
}


LIQ_PRIVATE void pam_freeacolorhash(struct acolorhash_table *acht)
{
    mempool_destroy(acht->mempool);
}

LIQ_PRIVATE void pam_freeacolorhist(histogram *hist)
{
    hist->free(hist->achv);
    hist->free(hist);
}

LIQ_PRIVATE colormap *pam_colormap(unsigned int colors, void* (*malloc)(size_t), void (*free)(void*))
{
    colormap *map = malloc(sizeof(colormap));
    if (!map) return NULL;
    *map = (colormap){
        .malloc = malloc,
        .free = free,
        .palette = malloc(colors * sizeof(map->palette[0])),
        .subset_palette = NULL,
        .colors = colors,
    };
    if (!map->palette) {
        free(map); return NULL;
    }
    memset(map->palette, 0, colors * sizeof(map->palette[0]));
    return map;
}

LIQ_PRIVATE colormap *pam_duplicate_colormap(colormap *map)
{
    colormap *dupe = pam_colormap(map->colors, map->malloc, map->free);
    for(unsigned int i=0; i < map->colors; i++) {
        dupe->palette[i] = map->palette[i];
    }
    if (map->subset_palette) {
        dupe->subset_palette = pam_duplicate_colormap(map->subset_palette);
    }
    return dupe;
}

LIQ_PRIVATE void pam_freecolormap(colormap *c)
{
    if (c->subset_palette) pam_freecolormap(c->subset_palette);
    c->free(c->palette);
    c->free(c);
}

LIQ_PRIVATE void to_f_set_gamma(float gamma_lut[], const double gamma)
{
    for(int i=0; i < 256; i++) {
        gamma_lut[i] = pow((double)i/255.0, internal_gamma/gamma);
    }
}

