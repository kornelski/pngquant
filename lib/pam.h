/* pam.h - pam (portable alpha map) utility library
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

#ifndef PAM_H
#define PAM_H

#include <math.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>

#ifndef MAX
#  define MAX(a,b)  ((a) > (b)? (a) : (b))
#  define MIN(a,b)  ((a) < (b)? (a) : (b))
#endif

#define MAX_DIFF 1e20

// it's safe to assume that 64-bit x86 has SSE2.
#ifndef USE_SSE
#  if defined(__SSE2__) && (defined(__x86_64__) || defined(__amd64) || defined(WIN32) || defined(__WIN32__))
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
#define NEVER_INLINE __attribute__ ((noinline))
#else
#define ALWAYS_INLINE
#define NEVER_INLINE
#endif

/* from pam.h */

typedef struct {
    unsigned char r, g, b, a;
} rgba_pixel;

typedef struct {
    float a, r, g, b;
} SSE_ALIGN f_pixel;

static const double internal_gamma = 0.5499;

LIQ_PRIVATE void to_f_set_gamma(float gamma_lut[], const double gamma);

/**
 Converts 8-bit color to internal gamma and premultiplied alpha.
 (premultiplied color space is much better for blending of semitransparent colors)
 */
inline static f_pixel to_f(const float gamma_lut[], const rgba_pixel px) ALWAYS_INLINE;
inline static f_pixel to_f(const float gamma_lut[], const rgba_pixel px)
{
    float a = px.a/255.f;

    return (f_pixel) {
        .a = a,
        .r = gamma_lut[px.r]*a,
        .g = gamma_lut[px.g]*a,
        .b = gamma_lut[px.b]*a,
    };
}

inline static rgba_pixel to_rgb(const float gamma, const f_pixel px)
{
    if (px.a < 1.f/256.f) {
        return (rgba_pixel){0,0,0,0};
    }

    float r = px.r / px.a,
          g = px.g / px.a,
          b = px.b / px.a,
          a = px.a;

    r = powf(r, gamma/internal_gamma);
    g = powf(g, gamma/internal_gamma);
    b = powf(b, gamma/internal_gamma);

    // 256, because numbers are in range 1..255.9999… rounded down
    r *= 256.f;
    g *= 256.f;
    b *= 256.f;
    a *= 256.f;

    return (rgba_pixel){
        .r = r>=255.f ? 255 : r,
        .g = g>=255.f ? 255 : g,
        .b = b>=255.f ? 255 : b,
        .a = a>=255.f ? 255 : a,
    };
}

inline static double colordifference_ch(const double x, const double y, const double alphas) ALWAYS_INLINE;
inline static double colordifference_ch(const double x, const double y, const double alphas)
{
    // maximum of channel blended on white, and blended on black
    // premultiplied alpha and backgrounds 0/1 shorten the formula
    const double black = x-y, white = black+alphas;
    return black*black + white*white;
}

inline static float colordifference_stdc(const f_pixel px, const f_pixel py) ALWAYS_INLINE;
inline static float colordifference_stdc(const f_pixel px, const f_pixel py)
{
    // px_b.rgb = px.rgb + 0*(1-px.a) // blend px on black
    // px_b.a   = px.a   + 1*(1-px.a)
    // px_w.rgb = px.rgb + 1*(1-px.a) // blend px on white
    // px_w.a   = px.a   + 1*(1-px.a)

    // px_b.rgb = px.rgb              // difference same as in opaque RGB
    // px_b.a   = 1
    // px_w.rgb = px.rgb - px.a       // difference simplifies to formula below
    // px_w.a   = 1

    // (px.rgb - px.a) - (py.rgb - py.a)
    // (px.rgb - py.rgb) + (py.a - px.a)

    const double alphas = py.a-px.a;
    return colordifference_ch(px.r, py.r, alphas) +
           colordifference_ch(px.g, py.g, alphas) +
           colordifference_ch(px.b, py.b, alphas);
}

inline static double min_colordifference_ch(const double x, const double y, const double alphas) ALWAYS_INLINE;
inline static double min_colordifference_ch(const double x, const double y, const double alphas)
{
    const double black = x-y, white = black+alphas;
    return MIN(black*black , white*white) * 2.f;
}

/* least possible difference between colors (difference varies depending on background they're blended on) */
inline static float min_colordifference(const f_pixel px, const f_pixel py) ALWAYS_INLINE;
inline static float min_colordifference(const f_pixel px, const f_pixel py)
{
    const double alphas = py.a-px.a;
    return min_colordifference_ch(px.r, py.r, alphas) +
           min_colordifference_ch(px.g, py.g, alphas) +
           min_colordifference_ch(px.b, py.b, alphas);
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

    __m128 onblack = _mm_sub_ps(vpx, vpy); // x - y
    __m128 onwhite = _mm_add_ps(onblack, alphas); // x - y + (y.a - x.a)

    onblack = _mm_mul_ps(onblack, onblack);
    onwhite = _mm_mul_ps(onwhite, onwhite);
    const __m128 max = _mm_add_ps(onwhite, onblack);

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
union rgba_as_int {
    rgba_pixel rgba;
    unsigned int l;
};

typedef struct {
    f_pixel acolor;
    float adjusted_weight,   // perceptual weight changed to tweak how mediancut selects colors
          perceptual_weight; // number of pixels weighted by importance of different areas of the picture

    float color_weight;      // these two change every time histogram subset is sorted
    unsigned int sort_value;
    unsigned char likely_colormap_index;
} hist_item;

typedef struct {
    hist_item *achv;
    void (*free)(void*);
    double total_perceptual_weight;
    unsigned int size;
} histogram;

typedef struct {
    f_pixel acolor;
    float popularity;
} colormap_item;

typedef struct colormap {
    colormap_item *palette;
    struct colormap *subset_palette;
    unsigned int colors;
    void* (*malloc)(size_t);
    void (*free)(void*);
} colormap;

struct acolorhist_arr_item {
    union rgba_as_int color;
    float perceptual_weight;
};

struct acolorhist_arr_head {
    unsigned int used, capacity;
    struct acolorhist_arr_item *other_items;
    struct {
        union rgba_as_int color;
        float perceptual_weight;
    } inline1, inline2;
};

struct acolorhash_table {
    struct mempool *mempool;
    struct acolorhist_arr_head *buckets;
    unsigned int ignorebits, maxcolors, colors, rows;
    struct acolorhist_arr_item *freestack[512];
    unsigned int freestackp;
    unsigned int hash_size;
};

LIQ_PRIVATE void pam_freeacolorhash(struct acolorhash_table *acht);
LIQ_PRIVATE struct acolorhash_table *pam_allocacolorhash(unsigned int maxcolors, unsigned int surface, unsigned int ignorebits, void* (*malloc)(size_t), void (*free)(void*));
LIQ_PRIVATE histogram *pam_acolorhashtoacolorhist(const struct acolorhash_table *acht, const double gamma, void* (*malloc)(size_t), void (*free)(void*));
LIQ_PRIVATE bool pam_computeacolorhash(struct acolorhash_table *acht, const rgba_pixel *const pixels[], unsigned int cols, unsigned int rows, const unsigned char *importance_map);

LIQ_PRIVATE void pam_freeacolorhist(histogram *h);

LIQ_PRIVATE colormap *pam_colormap(unsigned int colors, void* (*malloc)(size_t), void (*free)(void*));
LIQ_PRIVATE colormap *pam_duplicate_colormap(colormap *map);
LIQ_PRIVATE void pam_freecolormap(colormap *c);

#endif
