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
#ifndef PAM_H
#define PAM_H

#include <math.h>
#include <assert.h>

#ifndef MAX
#  define MAX(a,b)  ((a) > (b)? (a) : (b))
#  define MIN(a,b)  ((a) < (b)? (a) : (b))
#endif

// it's safe to assume that 64-bit x86 has SSE2.
#ifndef USE_SSE
#  if defined(__SSE2__) && (defined(__x86_64__) || defined(__amd64))
#    define USE_SSE 1
#  else
#    define USE_SSE 0
#  endif
#endif

#if USE_SSE
#include <emmintrin.h>
#define SSE_ALIGN __attribute__ ((aligned (16)))
#define cpuid(func,ax,bx,cx,dx)\
    __asm__ __volatile__ ("cpuid":\
    "=a" (ax), "=b" (bx), "=c" (cx), "=d" (dx) : "a" (func));
#else
#define SSE_ALIGN
#endif

#if defined(__GNUC__) || defined (__llvm__)
#define ALWAYS_INLINE __attribute__((always_inline))
#else
#define ALWAYS_INLINE
#endif

/* from pam.h */

typedef struct {
    unsigned char r, g, b, a;
} rgb_pixel;

typedef struct {
    float a, r, g, b;
} SSE_ALIGN f_pixel;

static const float internal_gamma = 0.45455f;

/**
 Converts 8-bit color to internal gamma and premultiplied alpha.
 (premultiplied color space is much better for blending of semitransparent colors)
 */
inline static f_pixel to_f(float gamma, rgb_pixel px) ALWAYS_INLINE;
inline static f_pixel to_f(float gamma, rgb_pixel px)
{
    float r = px.r/255.f,
          g = px.g/255.f,
          b = px.b/255.f,
          a = px.a/255.f;

    if (gamma != internal_gamma) {
        r = powf(r, internal_gamma/gamma);
        g = powf(g, internal_gamma/gamma);
        b = powf(b, internal_gamma/gamma);
    }


    return (f_pixel) {
        .a = a,
        .r = r*a,
        .g = g*a,
        .b = b*a,
    };
}

inline static rgb_pixel to_rgb(float gamma, f_pixel px)
{
    if (px.a < 1.f/256.f) {
        return (rgb_pixel){0,0,0,0};
    }

    float r = px.r / px.a,
          g = px.g / px.a,
          b = px.b / px.a,
          a = px.a;

    if (gamma != internal_gamma) {
        r = powf(r, gamma/internal_gamma);
        g = powf(g, gamma/internal_gamma);
        b = powf(b, gamma/internal_gamma);
    }

    // 256, because numbers are in range 1..255.9999… rounded down
    r *= 256.f;
    g *= 256.f;
    b *= 256.f;
    a *= 256.f;

    return (rgb_pixel){
        .r = r>=255.f ? 255 : (r<=0.f ? 0 : r),
        .g = g>=255.f ? 255 : (g<=0.f ? 0 : g),
        .b = b>=255.f ? 255 : (b<=0.f ? 0 : b),
        .a = a>=255.f ? 255 : a,
    };
}

inline static double colordifference_ch(const double x, const double y, const double alphas) ALWAYS_INLINE;
inline static double colordifference_ch(const double x, const double y, const double alphas)
{
    // maximum of channel blended on white, and blended on black
    // premultiplied alpha and backgrounds 0/1 shorten the formula
    const double black = x-y, white = black+alphas;
    return MAX(black*black, white*white);
}

inline static float colordifference_stdc(const f_pixel px, const f_pixel py) ALWAYS_INLINE;
inline static float colordifference_stdc(const f_pixel px, const f_pixel py)
{
    const double alphas = py.a-px.a;
    return colordifference_ch(px.r, py.r, alphas) +
           colordifference_ch(px.g, py.g, alphas) +
           colordifference_ch(px.b, py.b, alphas);
}

inline static float colordifference(f_pixel px, f_pixel py) ALWAYS_INLINE;
inline static float colordifference(f_pixel px, f_pixel py)
{
#if USE_SSE
    const __m128 vpx = _mm_load_ps((const float*)&px);
    const __m128 vpy = _mm_load_ps((const float*)&py);

    // y.a - x.a
    __m128 alphas = _mm_sub_ss(vpy, vpx);
    alphas = _mm_shuffle_ps(alphas,alphas,0); // copy first to all four

    const __m128 black = _mm_sub_ps(vpx, vpy);

    // x - y + (y.a - x.a)
    const __m128 white = _mm_add_ps(black, alphas);

    const __m128 max = _mm_max_ps(_mm_mul_ps(black, black), _mm_mul_ps(white, white));

    // add rgb, not a
    const __m128 maxhl = _mm_movehl_ps(max, max);
    const __m128 tmp = _mm_add_ps(max, maxhl);
    const __m128 sum = _mm_add_ss(maxhl, _mm_shuffle_ps(tmp, tmp, 1));

    const float res = _mm_cvtss_f32(sum);
    assert(fabs(res - colordifference_stdc(px,py)) < 0.001);
    return res;
#else
    return colordifference_stdc(px,py);
#endif
}

/* from pamcmap.h */
union rgb_as_long {
    rgb_pixel rgb;
    unsigned long l;
};

typedef struct {
    f_pixel acolor;
    float adjusted_weight, perceptual_weight;
} hist_item;

typedef struct {
    hist_item *achv;
    double total_perceptual_weight;
    int size;
} histogram;

typedef struct {
    f_pixel acolor;
    float popularity;
} colormap_item;

typedef struct colormap {
    colormap_item *palette;
    struct colormap *subset_palette;
    int colors;
} colormap;

struct acolorhist_arr_item {
    union rgb_as_long color;
    float perceptual_weight;
};

struct acolorhist_arr_head {
    int used, capacity;
    struct acolorhist_arr_item *other_items;
    union rgb_as_long color1, color2;
    float perceptual_weight1, perceptual_weight2;
};

typedef struct {
    struct mempool *mempool;
    struct acolorhist_arr_head *buckets;
} *acolorhash_table;

histogram *pam_computeacolorhist(const rgb_pixel*const apixels[], int cols, int rows, float gamma, int maxacolors, int ignorebits, const float *imp);
void pam_freeacolorhist(histogram *h);

colormap *pam_colormap(int colors);
void pam_freecolormap(colormap *c);

#endif
