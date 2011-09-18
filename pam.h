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

#include <math.h>
#ifndef MAX
#  define MAX(a,b)  ((a) > (b)? (a) : (b))
#  define MIN(a,b)  ((a) < (b)? (a) : (b))
#endif

#ifdef __SSE3__
#define USE_SSE
#endif

#ifdef USE_SSE
#include <pmmintrin.h>
#endif


/* from pam.h */

typedef struct {
    unsigned char r, g, b, a;
} rgb_pixel;

typedef struct {
    float a, r, g, b;
}  __attribute__ ((aligned (16))) f_pixel;

static const float internal_gamma = 0.45455;

/*
 Converts 8-bit RGB with given gamma to scalar RGB with internal gamma and premultiplied by alpha.
 Premultiplied color space is much better for blending of semitransparent colors.
 */
inline static f_pixel to_f(float gamma, rgb_pixel px)
{
    gamma /= internal_gamma;

    float r = powf(px.r/255.0f, 1.0f/gamma),
          g = powf(px.g/255.0f, 1.0f/gamma),
          b = powf(px.b/255.0f, 1.0f/gamma),
          a = px.a/255.0f;

    return (f_pixel){.r=r*a, .g=g*a, .b=b*a, .a=a};
}

inline static rgb_pixel to_rgb(float gamma, f_pixel px)
{
    if (px.a < 1.0/256.0) {
        return (rgb_pixel){0,0,0,0};
    }

    float r,g,b,a;

    gamma /= internal_gamma;

    // 256, because numbers are in range 1..255.9999… rounded down
    r = powf(px.r/px.a, gamma)*256.0f;
    g = powf(px.g/px.a, gamma)*256.0f;
    b = powf(px.b/px.a, gamma)*256.0f;
    a = px.a*256.0;

    return (rgb_pixel){
        .r = r>=255 ? 255 : (r<=0 ? 0 : r),
        .g = g>=255 ? 255 : (g<=0 ? 0 : g),
        .b = b>=255 ? 255 : (b<=0 ? 0 : b),
        .a = a>=255 ? 255 : a,
    };
}


inline static float colordifference_stdc(f_pixel px, f_pixel py)
{
    return (px.a - py.a) * (px.a - py.a) * 3.0 +
           (px.r - py.r) * (px.r - py.r) +
           (px.g - py.g) * (px.g - py.g) +
           (px.b - py.b) * (px.b - py.b);
}

inline static float colordifference(f_pixel px, f_pixel py)
{
#ifdef USE_SSE
    __m128 vpx = _mm_load_ps((const float*)&px);
    __m128 vpy = _mm_load_ps((const float*)&py);

    __m128 tmp = _mm_sub_ps(vpx, vpy); // t = px - py
    tmp = _mm_mul_ps(tmp, tmp); // t = t * t
    tmp = _mm_mul_ss(tmp, _mm_set_ss(3.0)); // alpha * 3.0

    tmp = _mm_hadd_ps(tmp,tmp); // 0+1 2+3 0+1 2+3
    __m128 rev = _mm_shuffle_ps(tmp, tmp, 0x1B); // reverses vector 2+3 0+1 2+3 0+1
    tmp = _mm_add_ss(tmp, rev); // 0+1 + 2+3

    float res = _mm_cvtss_f32(tmp);
    assert(fabs(res - colordifference_stdc(px,py)) < 0.001);
    return res;
#else
    return colordifference_stdc(px,py);
#endif
}

/* from pamcmap.h */

typedef struct {
    f_pixel acolor;
    float adjusted_weight, perceptual_weight;
} hist_item;

typedef struct {
    f_pixel acolor;
    float popularity;
} colormap_item;

typedef struct acolorhist_list_item *acolorhist_list;
struct acolorhist_list_item {
    acolorhist_list next;
    hist_item ch;
};

typedef struct {
    struct mempool *mempool;
    acolorhist_list *buckets;
} *acolorhash_table;


typedef unsigned char pixval; /* GRR: hardcoded for now; later add 16-bit support */


hist_item *pam_computeacolorhist(const rgb_pixel*const apixels[], int cols, int rows, double gamma, int maxacolors, int ignorebits, int use_contrast, int* acolorsP);
void pam_freeacolorhist(hist_item *achv);
