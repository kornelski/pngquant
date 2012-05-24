/*
 **
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
#include <stddef.h>

#include "pam.h"
#include "mediancut.h"

#define index_of_channel(ch) (offsetof(f_pixel,ch)/sizeof(float))

static f_pixel averagepixels(int clrs, const hist_item achv[static clrs], float min_opaque_val);

struct box {
    f_pixel color;
    f_pixel variance;
    double sum;
    int ind;
    int colors;
};

inline static double variance_diff(double val, const double good_enough) ALWAYS_INLINE;
inline static double variance_diff(double val, const double good_enough)
{
    val *= val;
    if (val < good_enough*good_enough) return val / 2.f;
    return val;
}

/** Weighted per-channel variance of the box. It's used to decide which channel to split by */
static f_pixel box_variance(const hist_item achv[], const struct box *box)
{
    f_pixel mean = box->color;
    double variancea=0, variancer=0, varianceg=0, varianceb=0;

    for (int i = 0; i < box->colors; ++i) {
        f_pixel px = achv[box->ind + i].acolor;
        double weight = achv[box->ind + i].adjusted_weight;
        variancea += variance_diff(mean.a - px.a, 1.f/256.f)*weight*0.95;
        variancer += variance_diff(mean.r - px.r, 1.f/512.f)*weight;
        varianceg += variance_diff(mean.g - px.g, 1.f/512.f)*weight;
        varianceb += variance_diff(mean.b - px.b, 1.f/512.f)*weight;
    }

    return (f_pixel){
        .a = variancea,
        .r = variancer,
        .g = varianceg,
        .b = varianceb,
    };
}

inline static double color_weight(f_pixel median, hist_item h) ALWAYS_INLINE;

static inline void hist_item_swap(hist_item *l, hist_item *r)
{
    if (l != r) {
        hist_item t = *l;
        *l = *r;
        *r = t;
    }
}

inline static int qsort_pivot(const hist_item *const base, const unsigned int len) ALWAYS_INLINE;
inline static int qsort_pivot(const hist_item *const base, const unsigned int len)
{
    if (len < 32) return len/2;

    const int aidx=8, bidx=len/2, cidx=len-1;
    const unsigned long a=base[aidx].sort_value, b=base[bidx].sort_value, c=base[cidx].sort_value;
    return (a < b) ? ((b < c) ? bidx : ((a < c) ? cidx : aidx ))
                   : ((b > c) ? bidx : ((a < c) ? aidx : cidx ));
}

inline static int qsort_partition(hist_item *const base, const unsigned int len) ALWAYS_INLINE;
inline static int qsort_partition(hist_item *const base, const unsigned int len)
{
    int l = 1, r = len;
    if (len >= 8) {
        hist_item_swap(&base[0], &base[qsort_pivot(base,len)]);
    }

    const unsigned long pivot_value = base[0].sort_value;
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

/** this is a simple qsort that completely sorts only elements between sort_start and +sort_len. Used to find median of the set. */
static void hist_item_sort_range(hist_item *base, unsigned int len, int sort_start, const unsigned int sort_len)
{
    do {
        const int l = qsort_partition(base, len), r = l+1;

        if (sort_start+sort_len > 0 && l > sort_start && l > 0) {
            hist_item_sort_range(base, l, sort_start, sort_len);
        }
        if (len > r && r < sort_start+sort_len && len > sort_start) {
            base += r; len -= r; sort_start -= r; // tail-recursive "call"
        } else return;
    } while(1);
}

/** sorts array to make sum of weights lower than halfvar one side, returns edge between <halfvar and >halfvar parts of the set */
static hist_item *hist_item_sort_halfvar(hist_item *base, int len, double *const lowervar, const double halfvar)
{
    do {
        const int l = qsort_partition(base, len), r = l+1;

        // check if sum of left side is smaller than half,
        // if it is, then it doesn't need to be sorted
        int t = 0; double tmpsum = *lowervar;
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

    for (int i=0; i < b->colors; i++) {
        const float *chans = (const float *)&achv[b->ind + i].acolor;
        // Only the first channel really matters. When trying median cut many times
        // with different histogram weights, I don't want sort randomness to influence outcome.
        achv[b->ind + i].sort_value = ((unsigned long)(chans[channels[0].chan]*65535.0)<<16) |
                                       (unsigned long)((chans[channels[2].chan] + chans[channels[1].chan]/2.0 + chans[channels[3].chan]/4.0)*65535.0);
    }

    const f_pixel median = get_median(b, achv);

    // box will be split to make color_weight of each side even
    const int ind = b->ind, end = ind+b->colors;
    double totalvar = 0;
    for(int j=ind; j < end; j++) totalvar += (achv[j].color_weight = color_weight(median, achv[j]));
    return totalvar / 2.0;
}

/** finds median in unsorted set by sorting only minimum required */
static f_pixel get_median(const struct box *b, hist_item achv[])
{
    const int median_start = (b->colors-1)/2;

    hist_item_sort_range(&(achv[b->ind]), b->colors,
                    median_start,
                    b->colors&1 ? 1 : 2);

    if (b->colors&1) return achv[b->ind + median_start].acolor;
    return averagepixels(2, &achv[b->ind + median_start], 1.0);
}

/*
 ** Find the best splittable box. -1 if no boxes are splittable.
 */
static int best_splittable_box(struct box* bv, int boxes)
{
    int bi=-1; double maxsum=0;
    for (int i=0; i < boxes; i++) {
        if (bv[i].colors < 2) continue;

        // looks only at max variance, because it's only going to split by it
        const double cv = MAX(bv[i].variance.r, MAX(bv[i].variance.g,bv[i].variance.b));

        // perfect shadows are not that important
        double av = bv[i].variance.a * 12.f/16.f;
        if (av < 6.f/256.f/256.f) av /= 2.f;

        const double thissum = bv[i].sum * MAX(av,cv);

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
    if (diff < 1.f/256.f/256.f) diff /= 2.f;
    return sqrt(diff) * (sqrt(1.0+h.adjusted_weight)-1.0);
}

static colormap *colormap_from_boxes(struct box* bv,int boxes,hist_item *achv,float min_opaque_val);
static void adjust_histogram(hist_item *achv, const colormap *map, const struct box* bv, int boxes);

/*
 ** Here is the fun part, the median-cut colormap generator.  This is based
 ** on Paul Heckbert's paper, "Color Image Quantization for Frame Buffer
 ** Display," SIGGRAPH 1982 Proceedings, page 297.
 */
colormap *mediancut(histogram *hist, float min_opaque_val, int newcolors)
{
    hist_item *achv = hist->achv;
    struct box bv[newcolors];

    /*
     ** Set up the initial box.
     */
    bv[0].ind = 0;
    bv[0].colors = hist->size;
    bv[0].color = averagepixels(bv[0].colors, &achv[bv[0].ind], min_opaque_val);
    bv[0].variance = box_variance(achv, &bv[0]);
    bv[0].sum = 0;
    for(int i=0; i < bv[0].colors; i++) bv[0].sum += achv[i].adjusted_weight;

    int boxes = 1;

    // remember smaller palette for fast searching
    colormap *representative_subset = NULL;
    int subset_size = ceilf(powf(newcolors,0.7f));

    /*
     ** Main loop: split boxes until we have enough.
     */
    while (boxes < newcolors) {

        if (boxes == subset_size) {
            representative_subset = colormap_from_boxes(bv, boxes, achv, min_opaque_val);
        }

        int bi= best_splittable_box(bv, boxes);
        if (bi < 0)
            break;        /* ran out of colors! */

        int indx = bv[bi].ind;
        int clrs = bv[bi].colors;

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
        int break_at = MIN(clrs-1, break_p - &achv[indx] + 1);

        /*
         ** Split the box.
         */
        double sm = bv[bi].sum;
        double lowersum = 0;
        for (int i=0; i < break_at; i++) lowersum += achv[indx + i].adjusted_weight;

        bv[bi].colors = break_at;
        bv[bi].sum = lowersum;
        bv[bi].color = averagepixels(bv[bi].colors, &achv[bv[bi].ind], min_opaque_val);
        bv[bi].variance = box_variance(achv, &bv[bi]);
        bv[boxes].ind = indx + break_at;
        bv[boxes].colors = clrs - break_at;
        bv[boxes].sum = sm - lowersum;
        bv[boxes].color = averagepixels(bv[boxes].colors, &achv[bv[boxes].ind], min_opaque_val);
        bv[boxes].variance = box_variance(achv, &bv[boxes]);
        ++boxes;
    }

    colormap *map = colormap_from_boxes(bv, boxes, achv, min_opaque_val);
    map->subset_palette = representative_subset;
    adjust_histogram(achv, map, bv, boxes);

    return map;
}

static colormap *colormap_from_boxes(struct box* bv, int boxes, hist_item *achv, float min_opaque_val)
{
    /*
     ** Ok, we've got enough boxes.  Now choose a representative color for
     ** each box.  There are a number of possible ways to make this choice.
     ** One would be to choose the center of the box; this ignores any structure
     ** within the boxes.  Another method would be to average all the colors in
     ** the box - this is the method specified in Heckbert's paper.
     */

    colormap *map = pam_colormap(boxes);

    for (int bi = 0; bi < boxes; ++bi) {
        map->palette[bi].acolor = bv[bi].color;

        /* store total color popularity (perceptual_weight is approximation of it) */
        map->palette[bi].popularity = 0;
        for(int i=bv[bi].ind; i < bv[bi].ind+bv[bi].colors; i++) {
            map->palette[bi].popularity += achv[i].perceptual_weight;
        }
    }

    return map;
}

/* increase histogram popularity by difference from the final color (this is used as part of feedback loop) */
static void adjust_histogram(hist_item *achv, const colormap *map, const struct box* bv, int boxes)
{
    for (int bi = 0; bi < boxes; ++bi) {
        for(int i=bv[bi].ind; i < bv[bi].ind+bv[bi].colors; i++) {
            achv[i].adjusted_weight *= sqrt(1.0 +colordifference(map->palette[bi].acolor, achv[i].acolor)/2.0);
        }
    }
}

static f_pixel averagepixels(int clrs, const hist_item achv[static clrs], float min_opaque_val)
{
    double r = 0, g = 0, b = 0, a = 0, sum = 0;
    float maxa = 0;

    for (int i = 0; i < clrs; ++i) {
        f_pixel px = achv[i].acolor;
        double tmp, weight = 1.0f;

        /* give more weight to colors that are further away from average
         this is intended to prevent desaturation of images and fading of whites
         */
        tmp = (0.5f - px.r);
        weight += tmp*tmp;
        tmp = (0.5f - px.g);
        weight += tmp*tmp;
        tmp = (0.5f - px.b);
        weight += tmp*tmp;

        weight *= achv[i].adjusted_weight;
        sum += weight;

        r += px.r * weight;
        g += px.g * weight;
        b += px.b * weight;
        a += px.a * weight;

        /* find if there are opaque colors, in case we're supposed to preserve opacity exactly (ie_bug) */
        if (px.a > maxa) maxa = px.a;
    }

    /* Colors are in premultiplied alpha colorspace, so they'll blend OK
     even if different opacities were mixed together */
    if (!sum) sum=1;
    a /= sum;
    r /= sum;
    g /= sum;
    b /= sum;

    assert(!isnan(r) && !isnan(g) && !isnan(b) && !isnan(a));

    /** if there was at least one completely opaque color, "round" final color to opaque */
    if (a >= min_opaque_val && maxa >= (255.0/256.0)) a = 1;

    return (f_pixel){.r=r, .g=g, .b=b, .a=a};
}

