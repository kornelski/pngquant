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
#include <assert.h>
#include <stddef.h>

#include "png.h"    /* libpng header; includes zlib.h */
#include "rwpng.h"  /* typedefs, common macros, public prototypes */
#include "pam.h"
#include "mediancut.h"

#define index_of_channel(ch) (offsetof(f_pixel,ch)/sizeof(float))

static int weightedcompare_r(const void *ch1, const void *ch2);
static int weightedcompare_g(const void *ch1, const void *ch2);
static int weightedcompare_b(const void *ch1, const void *ch2);
static int weightedcompare_a(const void *ch1, const void *ch2);

static f_pixel averagepixels(int indx, int clrs, const hist_item achv[], float min_opaque_val);

struct box {
    float variance;
    int sum;
    int ind;
    int colors;
};

typedef struct {
    int chan; float variance;
} channelvariance;

static int comparevariance(const void *ch1, const void *ch2)
{
    return ((channelvariance*)ch1)->variance > ((channelvariance*)ch2)->variance ? -1 :
          (((channelvariance*)ch1)->variance < ((channelvariance*)ch2)->variance ? 1 : 0);
};

static channelvariance channel_sort_order[4];

inline static int weightedcompare_other(const float *restrict c1p, const float *restrict c2p)
{
    // other channels are sorted backwards
    if (c1p[channel_sort_order[1].chan] > c2p[channel_sort_order[1].chan]) return -1;
    if (c1p[channel_sort_order[1].chan] < c2p[channel_sort_order[1].chan]) return 1;

    if (c1p[channel_sort_order[2].chan] > c2p[channel_sort_order[2].chan]) return -1;
    if (c1p[channel_sort_order[2].chan] < c2p[channel_sort_order[2].chan]) return 1;

    if (c1p[channel_sort_order[3].chan] > c2p[channel_sort_order[3].chan]) return -1;
    if (c1p[channel_sort_order[3].chan] < c2p[channel_sort_order[3].chan]) return 1;

    return 0;
}

/** these are specialised functions to make first comparison faster without lookup in channel_sort_order[] */
static int weightedcompare_r(const void *ch1, const void *ch2)
{
    const float *c1p = (const float *)&((hist_item*)ch1)->acolor;
    const float *c2p = (const float *)&((hist_item*)ch2)->acolor;

    if (c1p[index_of_channel(r)] > c2p[index_of_channel(r)]) return 1;
    if (c1p[index_of_channel(r)] < c2p[index_of_channel(r)]) return -1;

    return weightedcompare_other(c1p, c2p);
}

static int weightedcompare_g(const void *ch1, const void *ch2)
{
    const float *c1p = (const float *)&((hist_item*)ch1)->acolor;
    const float *c2p = (const float *)&((hist_item*)ch2)->acolor;

    if (c1p[index_of_channel(g)] > c2p[index_of_channel(g)]) return 1;
    if (c1p[index_of_channel(g)] < c2p[index_of_channel(g)]) return -1;

    return weightedcompare_other(c1p, c2p);
}

static int weightedcompare_b(const void *ch1, const void *ch2)
{
    const float *c1p = (const float *)&((hist_item*)ch1)->acolor;
    const float *c2p = (const float *)&((hist_item*)ch2)->acolor;

    if (c1p[index_of_channel(b)] > c2p[index_of_channel(b)]) return 1;
    if (c1p[index_of_channel(b)] < c2p[index_of_channel(b)]) return -1;

    return weightedcompare_other(c1p, c2p);
}

static int weightedcompare_a(const void *ch1, const void *ch2)
{
    const float *c1p = (const float *)&((hist_item*)ch1)->acolor;
    const float *c2p = (const float *)&((hist_item*)ch2)->acolor;

    if (c1p[index_of_channel(a)] > c2p[index_of_channel(a)]) return 1;
    if (c1p[index_of_channel(a)] < c2p[index_of_channel(a)]) return -1;

    return weightedcompare_other(c1p, c2p);
}

f_pixel channel_variance(const hist_item achv[], int indx, int clrs, float min_opaque_val)
{
    f_pixel mean = averagepixels(indx, clrs, achv, min_opaque_val);
    f_pixel variance = (f_pixel){0,0,0,0};

    for (int i = 0; i < clrs; ++i) {
        f_pixel px = achv[indx + i].acolor;
        variance.a += (mean.a - px.a)*(mean.a - px.a);
        variance.r += (mean.r - px.r)*(mean.r - px.r);
        variance.g += (mean.g - px.g)*(mean.g - px.g);
        variance.b += (mean.b - px.b)*(mean.b - px.b);
    }
    return variance;
}

static void sort_colors_by_variance(f_pixel variance, hist_item achv[], int indx, int clrs)
{
    /*
     ** Sort dimensions by their variance, and then sort colors first by dimension with highest variance
     */

    channel_sort_order[0] = (channelvariance){index_of_channel(r), variance.r};
    channel_sort_order[1] = (channelvariance){index_of_channel(g), variance.g};
    channel_sort_order[2] = (channelvariance){index_of_channel(b), variance.b};
    channel_sort_order[3] = (channelvariance){index_of_channel(a), variance.a};

    qsort(channel_sort_order, 4, sizeof(channel_sort_order[0]), comparevariance);


    int (*comp)(const void *, const void *); // comp variable that is a pointer to a function

         if (channel_sort_order[0].chan == index_of_channel(r)) comp = weightedcompare_r;
    else if (channel_sort_order[0].chan == index_of_channel(g)) comp = weightedcompare_g;
    else if (channel_sort_order[0].chan == index_of_channel(b)) comp = weightedcompare_b;
    else comp = weightedcompare_a;

    qsort(&(achv[indx]), clrs, sizeof(achv[0]), comp);
}


/*
 ** Here is the fun part, the median-cut colormap generator.  This is based
 ** on Paul Heckbert's paper, "Color Image Quantization for Frame Buffer
 ** Display," SIGGRAPH 1982 Proceedings, page 297.
 */

colormap_item *mediancut(hist *hist, float min_opaque_val, int newcolors)
{
    hist_item *achv = hist->achv;
    struct box bv[newcolors];
    colormap_item *acolormap = calloc(newcolors, sizeof(acolormap[0]));
    if (!acolormap) {
        return NULL;
    }

    /*
     ** Set up the initial box.
     */
    bv[0].ind = 0;
    bv[0].colors = hist->size;
    bv[0].variance = 1.0;
    for(int i=0; i < bv[0].colors; i++) bv[0].sum += achv[i].adjusted_weight;

    int boxes = 1;

    /*
     ** Main loop: split boxes until we have enough.
     */
    while (boxes < newcolors) {

        /*
         ** Find the best splittable box.
         */
        int bi=-1; float maxsum=0;
        for (int i=0; i < boxes; i++) {
            if (bv[i].colors < 2) continue;

            float thissum = bv[i].sum*bv[i].variance;

            if (thissum > maxsum) {
                maxsum = thissum;
                bi = i;
            }
        }
        if (bi < 0)
            break;        /* ran out of colors! */

        int indx = bv[bi].ind;
        int clrs = bv[bi].colors;

        sort_colors_by_variance(channel_variance(achv, indx, clrs, min_opaque_val), achv, indx, clrs);

        /*
         Classic implementation tries to get even number of colors or pixels in each subdivision.

         Here, instead of popularity I use (sqrt(popularity)*variance) metric.
         Each subdivision balances number of pixels (popular colors) and low variance -
         boxes can be large if they have similar colors. Later boxes with high variance
         will be more likely to be split.

         Median used as expected value gives much better results than mean.
         */

        f_pixel median = averagepixels(indx+(clrs-1)/2, clrs&1 ? 1 : 2, achv, min_opaque_val);

        int lowersum = 0;
        float halfvar = 0, lowervar = 0;
        for(int i=0; i < clrs -1; i++) {
            halfvar += sqrtf(colordifference(median, achv[indx+i].acolor)) * sqrtf(achv[indx+i].adjusted_weight);
        }
        halfvar /= 2.0f;

        int break_at;
        for (break_at = 0; break_at < clrs - 1; ++break_at) {
            if (lowervar >= halfvar)
                break;

            lowervar += sqrtf(colordifference(median, achv[indx+break_at].acolor)) * sqrtf(achv[indx+break_at].adjusted_weight);
            lowersum += achv[indx + break_at].adjusted_weight;
        }

        /*
         ** Split the box. Sum*variance is then used to find "largest" box to split.
         */
        int sm = bv[bi].sum;
        bv[bi].colors = break_at;
        bv[bi].sum = lowersum;
        bv[bi].variance = lowervar;
        bv[boxes].ind = indx + break_at;
        bv[boxes].colors = clrs - break_at;
        bv[boxes].sum = sm - lowersum;
        bv[boxes].variance = halfvar*2.0-lowervar;
        ++boxes;
    }

    /*
     ** Ok, we've got enough boxes.  Now choose a representative color for
     ** each box.  There are a number of possible ways to make this choice.
     ** One would be to choose the center of the box; this ignores any structure
     ** within the boxes.  Another method would be to average all the colors in
     ** the box - this is the method specified in Heckbert's paper.  A third
     ** method is to average all the pixels in the box.  You can switch which
     ** method is used by switching the commenting on the REP_ defines at
     ** the beginning of this source file.
     */
    for (int bi = 0; bi < boxes; ++bi) {
        acolormap[bi].acolor = averagepixels(bv[bi].ind, bv[bi].colors, achv, min_opaque_val);

        for(int i=bv[bi].ind; i < bv[bi].ind+bv[bi].colors; i++) {
            /* increase histogram popularity by difference from the final color (this is used as part of feedback loop) */
            achv[i].adjusted_weight *= 1.0 + sqrt(colordifference(acolormap[bi].acolor, achv[i].acolor))/2.0;

            /* store total color popularity (perceptual_weight is approximation of it) */
            acolormap[bi].popularity += achv[i].perceptual_weight;
        }
    }

    /*
     ** All done.
     */
    return acolormap;
}

static f_pixel averagepixels(int indx, int clrs, const hist_item achv[], float min_opaque_val)
{
    float r = 0, g = 0, b = 0, a = 0, sum = 0;
    float maxa = 0;
    int i;

    for (i = 0; i < clrs; ++i) {
        float weight = 1.0f;
        f_pixel px = achv[indx + i].acolor;
        float tmp;

        /* give more weight to colors that are further away from average
         this is intended to prevent desaturation of images and fading of whites
         */
        tmp = (0.5f - px.r);
        weight += tmp*tmp;
        tmp = (0.5f - px.g);
        weight += tmp*tmp;
        tmp = (0.5f - px.b);
        weight += tmp*tmp;

        weight *= achv[indx + i].adjusted_weight;
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


    /** if there was at least one completely opaque color, "round" final color to opaque */
    if (a >= min_opaque_val && maxa >= (255.0/256.0)) a = 1;

    return (f_pixel){.r=r, .g=g, .b=b, .a=a};
}

