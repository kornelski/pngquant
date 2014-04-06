/*
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
#include <stddef.h>

#include "libimagequant.h"
#include "pam.h"
#include "mediancut.h"

#define index_of_channel(ch) (offsetof(f_pixel,ch)/sizeof(float))

static f_pixel averagepixels(unsigned int clrs, const hist_item achv[], float min_opaque_val, const f_pixel center);

struct box {
    f_pixel color;
    f_pixel variance;
    double sum, total_error, max_error;
    unsigned int ind;
    unsigned int colors;
};

ALWAYS_INLINE static double variance_diff(double val, const double good_enough);
inline static double variance_diff(double val, const double good_enough)
{
    val *= val;
    if (val < good_enough*good_enough) return val*0.25;
    return val;
}

/** Weighted per-channel variance of the box. It's used to decide which channel to split by */
static f_pixel box_variance(const hist_item achv[], const struct box *box)
{
    f_pixel mean = box->color;
    double variancea=0, variancer=0, varianceg=0, varianceb=0;

    for(unsigned int i = 0; i < box->colors; ++i) {
        f_pixel px = achv[box->ind + i].acolor;
        double weight = achv[box->ind + i].adjusted_weight;
        variancea += variance_diff(mean.a - px.a, 2.0/256.0)*weight;
        variancer += variance_diff(mean.r - px.r, 1.0/256.0)*weight;
        varianceg += variance_diff(mean.g - px.g, 1.0/256.0)*weight;
        varianceb += variance_diff(mean.b - px.b, 1.0/256.0)*weight;
    }

    return (f_pixel){
        .a = variancea*(4.0/16.0),
        .r = variancer*(7.0/16.0),
        .g = varianceg*(9.0/16.0),
        .b = varianceb*(5.0/16.0),
    };
}

static double box_max_error(const hist_item achv[], const struct box *box)
{
    f_pixel mean = box->color;
    double max_error = 0;

    for(unsigned int i = 0; i < box->colors; ++i) {
        const double diff = colordifference(mean, achv[box->ind + i].acolor);
        if (diff > max_error) {
            max_error = diff;
        }
    }
    return max_error;
}

ALWAYS_INLINE static double color_weight(f_pixel median, hist_item h);

static inline void hist_item_swap(hist_item *l, hist_item *r)
{
    if (l != r) {
        hist_item t = *l;
        *l = *r;
        *r = t;
    }
}

ALWAYS_INLINE static unsigned int qsort_pivot(const hist_item *const base, const unsigned int len);
inline static unsigned int qsort_pivot(const hist_item *const base, const unsigned int len)
{
    if (len < 32) {
        return len/2;
    }

    const unsigned int aidx=8, bidx=len/2, cidx=len-1;
    const unsigned int a=base[aidx].sort_value, b=base[bidx].sort_value, c=base[cidx].sort_value;
    return (a < b) ? ((b < c) ? bidx : ((a < c) ? cidx : aidx ))
                   : ((b > c) ? bidx : ((a < c) ? aidx : cidx ));
}

ALWAYS_INLINE static unsigned int qsort_partition(hist_item *const base, const unsigned int len);
inline static unsigned int qsort_partition(hist_item *const base, const unsigned int len)
{
    unsigned int l = 1, r = len;
    if (len >= 8) {
        hist_item_swap(&base[0], &base[qsort_pivot(base,len)]);
    }

    const unsigned int pivot_value = base[0].sort_value;
    while (l < r) {
        if (base[l].sort_value >= pivot_value) {
            l++;
        } else {
            while(l < --r && base[r].sort_value <= pivot_value) {}
            hist_item_swap(&base[l], &base[r]);
        }
    }
    l--;
    hist_item_swap(&base[0], &base[l]);

    return l;
}

/** quick select algorithm */
static void hist_item_sort_range(hist_item *base, unsigned int len, unsigned int sort_start)
{
    for(;;) {
        const unsigned int l = qsort_partition(base, len), r = l+1;

        if (l > 0 && sort_start < l) {
            len = l;
        }
        else if (r < len && sort_start > r) {
            base += r; len -= r; sort_start -= r;
        }
        else break;
    }
}

/** sorts array to make sum of weights lower than halfvar one side, returns edge between <halfvar and >halfvar parts of the set */
static hist_item *hist_item_sort_halfvar(hist_item *base, unsigned int len, double *const lowervar, const double halfvar)
{
    do {
        const unsigned int l = qsort_partition(base, len), r = l+1;

        // check if sum of left side is smaller than half,
        // if it is, then it doesn't need to be sorted
        unsigned int t = 0; double tmpsum = *lowervar;
        while (t <= l && tmpsum < halfvar) tmpsum += base[t++].color_weight;

        if (tmpsum < halfvar) {
            *lowervar = tmpsum;
        } else {
            if (l > 0) {
                hist_item *res = hist_item_sort_halfvar(base, l, lowervar, halfvar);
                if (res) return res;
            } else {
                // End of left recursion. This will be executed in order from the first element.
                *lowervar += base[0].color_weight;
                if (*lowervar > halfvar) return &base[0];
            }
        }

        if (len > r) {
            base += r; len -= r; // tail-recursive "call"
        } else {
            *lowervar += base[r].color_weight;
            return (*lowervar > halfvar) ? &base[r] : NULL;
        }
    } while(1);
}

static f_pixel get_median(const struct box *b, hist_item achv[]);

typedef struct {
    unsigned int chan; float variance;
} channelvariance;

static int comparevariance(const void *ch1, const void *ch2)
{
    return ((const channelvariance*)ch1)->variance > ((const channelvariance*)ch2)->variance ? -1 :
          (((const channelvariance*)ch1)->variance < ((const channelvariance*)ch2)->variance ? 1 : 0);
}

/** Finds which channels need to be sorted first and preproceses achv for fast sort */
static double prepare_sort(struct box *b, hist_item achv[])
{
    /*
     ** Sort dimensions by their variance, and then sort colors first by dimension with highest variance
     */
    channelvariance channels[4] = {
        {index_of_channel(r), b->variance.r},
        {index_of_channel(g), b->variance.g},
        {index_of_channel(b), b->variance.b},
        {index_of_channel(a), b->variance.a},
    };

    qsort(channels, 4, sizeof(channels[0]), comparevariance);

    for(unsigned int i=0; i < b->colors; i++) {
        const float *chans = (const float *)&achv[b->ind + i].acolor;
        // Only the first channel really matters. When trying median cut many times
        // with different histogram weights, I don't want sort randomness to influence outcome.
        achv[b->ind + i].sort_value = ((unsigned int)(chans[channels[0].chan]*65535.0)<<16) |
                                       (unsigned int)((chans[channels[2].chan] + chans[channels[1].chan]/2.0 + chans[channels[3].chan]/4.0)*65535.0);
    }

    const f_pixel median = get_median(b, achv);

    // box will be split to make color_weight of each side even
    const unsigned int ind = b->ind, end = ind+b->colors;
    double totalvar = 0;
    for(unsigned int j=ind; j < end; j++) totalvar += (achv[j].color_weight = color_weight(median, achv[j]));
    return totalvar / 2.0;
}

/** finds median in unsorted set by sorting only minimum required */
static f_pixel get_median(const struct box *b, hist_item achv[])
{
    const unsigned int median_start = (b->colors-1)/2;

    hist_item_sort_range(&(achv[b->ind]), b->colors,
                         median_start);

    if (b->colors&1) return achv[b->ind + median_start].acolor;

    // technically the second color is not guaranteed to be sorted correctly
    // but most of the time it is good enough to be useful
    return averagepixels(2, &achv[b->ind + median_start], 1.0, (f_pixel){0.5,0.5,0.5,0.5});
}

/*
 ** Find the best splittable box. -1 if no boxes are splittable.
 */
static int best_splittable_box(struct box* bv, unsigned int boxes, const double max_mse)
{
    int bi=-1; double maxsum=0;
    for(unsigned int i=0; i < boxes; i++) {
        if (bv[i].colors < 2) {
            continue;
        }

        // looks only at max variance, because it's only going to split by it
        const double cv = MAX(bv[i].variance.r, MAX(bv[i].variance.g,bv[i].variance.b));
        double thissum = bv[i].sum * MAX(bv[i].variance.a, cv);

        if (bv[i].max_error > max_mse) {
            thissum = thissum* bv[i].max_error/max_mse;
        }

        if (thissum > maxsum) {
            maxsum = thissum;
            bi = i;
        }
    }
    return bi;
}

inline static double color_weight(f_pixel median, hist_item h)
{
    float diff = colordifference(median, h.acolor);
    // if color is "good enough", don't split further
    if (diff < 2.f/256.f/256.f) diff /= 2.f;
    return sqrt(diff) * (sqrt(1.0+h.adjusted_weight)-1.0);
}

static void set_colormap_from_boxes(colormap *map, struct box* bv, unsigned int boxes, hist_item *achv);
static void adjust_histogram(hist_item *achv, const colormap *map, const struct box* bv, unsigned int boxes);

double box_error(const struct box *box, const hist_item achv[])
{
    f_pixel avg = box->color;

    double total_error=0;
    for (unsigned int i = 0; i < box->colors; ++i) {
        total_error += colordifference(avg, achv[box->ind + i].acolor) * achv[box->ind + i].perceptual_weight;
    }

    return total_error;
}


static bool total_box_error_below_target(double target_mse, struct box bv[], unsigned int boxes, const histogram *hist)
{
    target_mse *= hist->total_perceptual_weight;
    double total_error=0;

    for(unsigned int i=0; i < boxes; i++) {
        // error is (re)calculated lazily
        if (bv[i].total_error >= 0) {
            total_error += bv[i].total_error;
        }
        if (total_error > target_mse) return false;
    }

    for(unsigned int i=0; i < boxes; i++) {
        if (bv[i].total_error < 0) {
            bv[i].total_error = box_error(&bv[i], hist->achv);
            total_error += bv[i].total_error;
        }
        if (total_error > target_mse) return false;
    }

    return true;
}

/*
 ** Here is the fun part, the median-cut colormap generator.  This is based
 ** on Paul Heckbert's paper, "Color Image Quantization for Frame Buffer
 ** Display," SIGGRAPH 1982 Proceedings, page 297.
 */
LIQ_PRIVATE colormap *mediancut(histogram *hist, const float min_opaque_val, unsigned int newcolors, const double target_mse, const double max_mse, void* (*malloc)(size_t), void (*free)(void*))
{
    hist_item *achv = hist->achv;
    struct box bv[newcolors];

    /*
     ** Set up the initial box.
     */
    bv[0].ind = 0;
    bv[0].colors = hist->size;
    bv[0].color = averagepixels(bv[0].colors, &achv[bv[0].ind], min_opaque_val, (f_pixel){0.5,0.5,0.5,0.5});
    bv[0].variance = box_variance(achv, &bv[0]);
    bv[0].max_error = box_max_error(achv, &bv[0]);
    bv[0].sum = 0;
    bv[0].total_error = -1;
    for(unsigned int i=0; i < bv[0].colors; i++) bv[0].sum += achv[i].adjusted_weight;

    unsigned int boxes = 1;

    // remember smaller palette for fast searching
    colormap *representative_subset = NULL;
    unsigned int subset_size = ceilf(powf(newcolors,0.7f));

    /*
     ** Main loop: split boxes until we have enough.
     */
    while (boxes < newcolors) {

        if (boxes == subset_size) {
            representative_subset = pam_colormap(boxes, malloc, free);
            set_colormap_from_boxes(representative_subset, bv, boxes, achv);
        }

        // first splits boxes that exceed quality limit (to have colors for things like odd green pixel),
        // later raises the limit to allow large smooth areas/gradients get colors.
        const double current_max_mse = max_mse + (boxes/(double)newcolors)*16.0*max_mse;
        const int bi = best_splittable_box(bv, boxes, current_max_mse);
        if (bi < 0)
            break;        /* ran out of colors! */

        unsigned int indx = bv[bi].ind;
        unsigned int clrs = bv[bi].colors;

        /*
         Classic implementation tries to get even number of colors or pixels in each subdivision.

         Here, instead of popularity I use (sqrt(popularity)*variance) metric.
         Each subdivision balances number of pixels (popular colors) and low variance -
         boxes can be large if they have similar colors. Later boxes with high variance
         will be more likely to be split.

         Median used as expected value gives much better results than mean.
         */

        const double halfvar = prepare_sort(&bv[bi], achv);
        double lowervar=0;

        // hist_item_sort_halfvar sorts and sums lowervar at the same time
        // returns item to break at …minus one, which does smell like an off-by-one error.
        hist_item *break_p = hist_item_sort_halfvar(&achv[indx], clrs, &lowervar, halfvar);
        unsigned int break_at = MIN(clrs-1, break_p - &achv[indx] + 1);

        /*
         ** Split the box.
         */
        double sm = bv[bi].sum;
        double lowersum = 0;
        for(unsigned int i=0; i < break_at; i++) lowersum += achv[indx + i].adjusted_weight;

        const f_pixel previous_center = bv[bi].color;
        bv[bi].colors = break_at;
        bv[bi].sum = lowersum;
        bv[bi].color = averagepixels(bv[bi].colors, &achv[bv[bi].ind], min_opaque_val, previous_center);
        bv[bi].total_error = -1;
        bv[bi].variance = box_variance(achv, &bv[bi]);
        bv[bi].max_error = box_max_error(achv, &bv[bi]);
        bv[boxes].ind = indx + break_at;
        bv[boxes].colors = clrs - break_at;
        bv[boxes].sum = sm - lowersum;
        bv[boxes].color = averagepixels(bv[boxes].colors, &achv[bv[boxes].ind], min_opaque_val, previous_center);
        bv[boxes].total_error = -1;
        bv[boxes].variance = box_variance(achv, &bv[boxes]);
        bv[boxes].max_error = box_max_error(achv, &bv[boxes]);

        ++boxes;

        if (total_box_error_below_target(target_mse, bv, boxes, hist)) {
            break;
        }
    }

    colormap *map = pam_colormap(boxes, malloc, free);
    set_colormap_from_boxes(map, bv, boxes, achv);

    map->subset_palette = representative_subset;
    adjust_histogram(achv, map, bv, boxes);

    return map;
}

static void set_colormap_from_boxes(colormap *map, struct box* bv, unsigned int boxes, hist_item *achv)
{
    /*
     ** Ok, we've got enough boxes.  Now choose a representative color for
     ** each box.  There are a number of possible ways to make this choice.
     ** One would be to choose the center of the box; this ignores any structure
     ** within the boxes.  Another method would be to average all the colors in
     ** the box - this is the method specified in Heckbert's paper.
     */

    for(unsigned int bi = 0; bi < boxes; ++bi) {
        map->palette[bi].acolor = bv[bi].color;

        /* store total color popularity (perceptual_weight is approximation of it) */
        map->palette[bi].popularity = 0;
        for(unsigned int i=bv[bi].ind; i < bv[bi].ind+bv[bi].colors; i++) {
            map->palette[bi].popularity = achv[i].perceptual_weight;
        }
    }
}

/* increase histogram popularity by difference from the final color (this is used as part of feedback loop) */
static void adjust_histogram(hist_item *achv, const colormap *map, const struct box* bv, unsigned int boxes)
{
    for(unsigned int bi = 0; bi < boxes; ++bi) {
        for(unsigned int i=bv[bi].ind; i < bv[bi].ind+bv[bi].colors; i++) {
            achv[i].adjusted_weight *= sqrt(1.0 +colordifference(map->palette[bi].acolor, achv[i].acolor)/4.0);
            achv[i].likely_colormap_index = bi;
        }
    }
}

static f_pixel averagepixels(unsigned int clrs, const hist_item achv[], const float min_opaque_val, const f_pixel center)
{
    double r = 0, g = 0, b = 0, a = 0, new_a=0, sum = 0;
    float maxa = 0;

    // first find final opacity in order to blend colors at that opacity
    for(unsigned int i = 0; i < clrs; ++i) {
        const f_pixel px = achv[i].acolor;
        new_a += px.a * achv[i].adjusted_weight;
        sum += achv[i].adjusted_weight;

        /* find if there are opaque colors, in case we're supposed to preserve opacity exactly (ie_bug) */
        if (px.a > maxa) maxa = px.a;
    }

    if (sum) new_a /= sum;

    /** if there was at least one completely opaque color, "round" final color to opaque */
    if (new_a >= min_opaque_val && maxa >= (255.0/256.0)) new_a = 1;

    sum=0;
    // reverse iteration for cache locality with previous loop
    for(int i = clrs-1; i >= 0; i--) {
        double tmp, weight = 1.0f;
        f_pixel px = achv[i].acolor;

        /* give more weight to colors that are further away from average
         this is intended to prevent desaturation of images and fading of whites
         */
        tmp = (center.r - px.r);
        weight += tmp*tmp;
        tmp = (center.g - px.g);
        weight += tmp*tmp;
        tmp = (center.b - px.b);
        weight += tmp*tmp;

        weight *= achv[i].adjusted_weight;
        sum += weight;

        if (px.a) {
            px.r /= px.a;
            px.g /= px.a;
            px.b /= px.a;
        }

        r += px.r * new_a * weight;
        g += px.g * new_a * weight;
        b += px.b * new_a * weight;
        a += new_a * weight;
    }

    if (sum) {
    a /= sum;
    r /= sum;
    g /= sum;
    b /= sum;
    }

    assert(!isnan(r) && !isnan(g) && !isnan(b) && !isnan(a));

    return (f_pixel){.r=r, .g=g, .b=b, .a=a};
}
