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
#define cpuid(func,ax,bx,cx,dx)\
    __asm__ __volatile__ ("cpuid":\
    "=a" (ax), "=b" (bx), "=c" (cx), "=d" (dx) : "a" (func));
#endif

/* from pam.h */

typedef struct {
    unsigned char r, g, b, a;
} rgb_pixel;

typedef struct {
    float a, r, g, b;
}  __attribute__ ((aligned (16))) f_pixel;

static const float internal_gamma = 0.45455;

/**
 Converts scalar color to internal gamma and premultiplied alpha.
 (premultiplied color space is much better for blending of semitransparent colors)
 */
inline static f_pixel to_f_scalar(float gamma, f_pixel px)
{
    if (gamma != internal_gamma) {
        px.r = powf(px.r, internal_gamma/gamma);
        px.g = powf(px.g, internal_gamma/gamma);
        px.b = powf(px.b, internal_gamma/gamma);
    }

    px.r *= px.a;
    px.g *= px.a;
    px.b *= px.a;

    return px;
}

/**
  Converts 8-bit RGB with given gamma to scalar RGB
 */
inline static f_pixel to_f(float gamma, rgb_pixel px)
{
    return to_f_scalar(gamma, (f_pixel){
        .a = px.a/255.0f,
        .r = px.r/255.0f,
        .g = px.g/255.0f,
        .b = px.b/255.0f,
    });
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

inline static float colordifference_ch(const float x, const float y, const float alphas)
{
    // maximum of channel blended on white, and blended on black
    // premultiplied alpha and backgrounds 0/1 shorten the formula
    const float black = x-y, white = black+alphas;
    return MAX(black*black, white*white);
}

inline static float colordifference_stdc(const f_pixel px, const f_pixel py)
{
    const float alphas = py.a-px.a;
    return colordifference_ch(px.r, py.r, alphas) +
           colordifference_ch(px.g, py.g, alphas) +
           colordifference_ch(px.b, py.b, alphas);
}

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
} hist;

typedef struct {
    f_pixel acolor;
    float popularity;
} colormap_item;

typedef struct colormap {
    colormap_item *palette;
    struct colormap *subset_palette;
    int colors;
} colormap;

struct acolorhist_list_item {
    union rgb_as_long color;
    struct acolorhist_list_item *next;
    float perceptual_weight;
};

typedef struct {
    struct mempool *mempool;
    struct acolorhist_list_item **buckets;
} *acolorhash_table;

hist *pam_computeacolorhist(const rgb_pixel*const apixels[], int cols, int rows, double gamma, int maxacolors, int ignorebits, const float *imp);
void pam_freeacolorhist(hist *h);

colormap *pam_colormap(int colors);
void pam_freecolormap(colormap *c);
