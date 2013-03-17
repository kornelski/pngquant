/* pngquant.c - quantize the colors in an alphamap down to a specified number
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>

#ifdef _OPENMP
#include <omp.h>
#else
#define omp_get_max_threads() 1
#define omp_get_thread_num() 0
#endif

#include "pam.h"
#include "mediancut.h"
#include "nearest.h"
#include "blur.h"
#include "viter.h"

#include "libimagequant.h"

// each structure has a pointer as a unique identifier that allows type checking at run time
static const char *const liq_attr_magic = "a", *const liq_image_magic = "i",
     *const liq_result_magic = "q", *const liq_remapping_result_magic = "r",
     *const liq_freed_magic = "X";
#define CHECK_STRUCT_TYPE(attr, kind) (attr && (attr->magic_header != liq_freed_magic || memory_used_after_free()) && attr->magic_header == kind ## _magic)

struct liq_attr {
    const char *magic_header;
    void* (*malloc)(size_t);
    void (*free)(void*);

    double target_mse, max_mse, voronoi_iteration_limit;
    float min_opaque_val;
    bool last_index_transparent, use_contrast_maps, use_dither_map;
    unsigned int max_colors, max_histogram_entries, min_posterization, voronoi_iterations, feedback_loop_trials;

    liq_log_callback_function *log_callback;
    void *log_callback_user_info;
    liq_log_flush_callback_function *log_flush_callback;
    void *log_flush_callback_user_info;
};

struct pngquant_options {
    liq_attr *liq;
    liq_image *fixed_palette_image;
    bool floyd, using_stdin, force, ie_mode;
};

struct liq_image {
    const char *magic_header;
    void* (*malloc)(size_t);
    void (*free)(void*);

    f_pixel *f_pixels;
    rgba_pixel **rows;
    double gamma;
    int width, height;
    float *noise, *edges, *dither_map;
    rgba_pixel *pixels, *temp_row;
    liq_image_get_rgba_row_callback *row_callback;
    void *row_callback_user_info;
    bool free_rows, free_pixels;
};

struct liq_remapping_result {
    const char *magic_header;
    void* (*malloc)(size_t);
    void (*free)(void*);

    unsigned char *pixels;
    colormap *palette;
    liq_palette int_palette;
    double gamma, palette_error;
    float min_opaque_val, dither_level;
    bool use_dither_map;
};

struct liq_result {
    const char *magic_header;
    void* (*malloc)(size_t);
    void (*free)(void*);

    colormap *palette;
    double gamma, palette_error;
    float min_opaque_val, dither_level;
    bool use_dither_map;
};

static liq_result *pngquant_quantize(histogram *hist, const liq_attr *options);
static void modify_alpha(liq_image *input_image, const float min_opaque_val);
static void contrast_maps(liq_image *image);
static histogram *get_histogram(liq_image *input_image, liq_attr *options);
static rgba_pixel *liq_image_get_row_rgba(liq_image *input_image, unsigned int row);
static f_pixel *liq_image_get_row_f(liq_image *input_image, unsigned int row);
static void liq_remapping_result_destroy(liq_remapping_result *result);

static void liq_verbose_printf(const liq_attr *context, const char *fmt, ...)
{
    if (context->log_callback) {
        va_list va;
        va_start(va, fmt);
        int required_space = vsnprintf(NULL, 0, fmt, va)+1; // +\0
        va_end(va);

        char buf[required_space];
        va_start(va, fmt);
        vsnprintf(buf, required_space, fmt, va);
        va_end(va);

        context->log_callback(context, buf, context->log_callback_user_info);
    }
}

inline static void verbose_print(const liq_attr *attr, const char *msg)
{
    if (attr->log_callback) attr->log_callback(attr, msg, attr->log_callback_user_info);
}

static void liq_verbose_printf_flush(liq_attr *attr)
{
    if (attr->log_flush_callback) attr->log_flush_callback(attr, attr->log_flush_callback_user_info);
}

#if USE_SSE
inline static bool is_sse2_available()
{
#if (defined(__x86_64__) || defined(__amd64))
    return true;
#else
    int a,b,c,d;
        cpuid(1, a, b, c, d);
    return d & (1<<26); // edx bit 26 is set when SSE2 is present
#endif
}
#endif

static bool memory_used_after_free(void)
{
    fputs("memory_used_after_free", stderr);
    abort(); // program should have crashed already, so don't blame me.
    return false;
}

static double quality_to_mse(long quality)
{
    if (quality == 0) return MAX_DIFF;

    // curve fudged to be roughly similar to quality of libjpeg
    return 2.5/pow(210.0 + quality, 1.2) * (100.1-quality)/100.0;
}

LIQ_EXPORT liq_error liq_set_quality(liq_attr* attr, int target, int minimum)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) return LIQ_INVALID_POINTER;
    if (target < 0 || target > 100 || target < minimum || minimum < 0) return LIQ_VALUE_OUT_OF_RANGE;

    attr->target_mse = quality_to_mse(target);
    attr->max_mse = quality_to_mse(minimum);
    return LIQ_OK;
}

LIQ_EXPORT liq_error liq_set_max_colors(liq_attr* attr, int colors)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) return LIQ_INVALID_POINTER;
    if (colors < 2 || colors > 256) return LIQ_VALUE_OUT_OF_RANGE;

    attr->max_colors = colors;
    return LIQ_OK;
}

LIQ_EXPORT liq_error liq_set_speed(liq_attr* attr, int speed)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) return LIQ_INVALID_POINTER;
    if (speed < 1 || speed > 10) return LIQ_VALUE_OUT_OF_RANGE;

    int iterations = MAX(8-speed,0); iterations += iterations * iterations/2;
    attr->voronoi_iterations = iterations;
    attr->voronoi_iteration_limit = 1.0/(double)(1<<(23-speed));
    attr->feedback_loop_trials = MAX(56-9*speed, 0);

    attr->max_histogram_entries = (1<<17) + (1<<18)*(10-speed);
    attr->min_posterization = (speed >= 8) ? 1 : 0;
    attr->use_dither_map = (speed <= (omp_get_max_threads() > 1 ? 7 : 5)); // parallelized dither map might speed up floyd remapping
    attr->use_contrast_maps = (speed <= 7) || attr->use_dither_map;

    return LIQ_OK;
}

LIQ_EXPORT liq_error liq_set_output_gamma(liq_result* res, double gamma)
{
    if (!CHECK_STRUCT_TYPE(res, liq_result)) return LIQ_INVALID_POINTER;
    if (gamma <= 0 || gamma >= 1.0) return LIQ_VALUE_OUT_OF_RANGE;

    res->gamma = gamma;
    return LIQ_OK;
}

LIQ_EXPORT liq_error liq_set_min_opacity(liq_attr* attr, int min)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) return LIQ_INVALID_POINTER;
    if (min < 0 || min > 255) return LIQ_VALUE_OUT_OF_RANGE;

    attr->min_opaque_val = (double)min/255.0;
    return LIQ_OK;
}

LIQ_EXPORT void liq_set_last_index_transparent(liq_attr* attr, int is_last)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) return;

    attr->last_index_transparent = !!is_last;
}

LIQ_EXPORT void liq_set_log_callback(liq_attr *attr, liq_log_callback_function *callback, void* user_info)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) return;

    liq_verbose_printf_flush(attr);
    attr->log_callback = callback;
    attr->log_callback_user_info = user_info;
}

LIQ_EXPORT void liq_set_log_flush_callback(liq_attr *attr, liq_log_flush_callback_function *callback, void* user_info)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) return;

    attr->log_flush_callback = callback;
    attr->log_flush_callback_user_info = user_info;
}

LIQ_EXPORT liq_attr* liq_attr_create()
{
    return liq_attr_create_with_allocator(malloc, free);
}

LIQ_EXPORT void liq_attr_destroy(liq_attr *attr)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) return;

    liq_verbose_printf_flush(attr);

    attr->magic_header = liq_freed_magic;
    attr->free(attr);
}

LIQ_EXPORT liq_attr* liq_attr_copy(liq_attr *orig)
{
    if (!CHECK_STRUCT_TYPE(orig, liq_attr)) return NULL;

    liq_attr *attr = orig->malloc(sizeof(liq_attr));
    *attr = *orig;
    return attr;
}

LIQ_EXPORT liq_attr* liq_attr_create_with_allocator(void* (*malloc)(size_t), void (*free)(void*))
{
#if USE_SSE
    if (!is_sse2_available()) {
        return NULL;
    }
#endif
    if ((malloc != NULL) != (free != NULL)) return NULL; // either specify both or none

    liq_attr *attr = malloc(sizeof(liq_attr));
    *attr = (liq_attr) {
        .magic_header = liq_attr_magic,
        .malloc = malloc,
        .free = free,
        .max_colors = 256,
        .min_opaque_val = 1, // whether preserve opaque colors for IE (1.0=no, does not affect alpha)
        .last_index_transparent = false, // puts transparent color at last index. This is workaround for blu-ray subtitles.
        .target_mse = 0,
        .max_mse = MAX_DIFF,
    };
    liq_set_speed(attr, 3);
    return attr;
}

static liq_image *liq_image_create_internal(liq_attr *attr, rgba_pixel* rows[], liq_image_get_rgba_row_callback *row_callback, void *row_callback_user_info, int width, int height, double gamma)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr) || width <= 0 || height <= 0 || gamma < 0 || gamma > 1.0) return NULL;
    if (!rows && !row_callback) return NULL;

    liq_image *img = attr->malloc(sizeof(liq_image));
    *img = (liq_image){
        .magic_header = liq_image_magic,
        .malloc = attr->malloc,
        .free = attr->free,
        .width = width, .height = height,
        .gamma = gamma ? gamma : 0.45455,
        .f_pixels = attr->malloc(sizeof(img->f_pixels[0]) * width * height),
        .temp_row = rows ? 0 : attr->malloc(sizeof(img->temp_row[0]) * width),
        .rows = rows,
        .row_callback = row_callback,
        .row_callback_user_info = row_callback_user_info,
    };

    if (attr->min_opaque_val <= 254.f/255.f) {
        verbose_print(attr, "  Working around IE6 bug by making image less transparent...");
        modify_alpha(img, attr->min_opaque_val);
    }

    to_f_set_gamma(img->gamma);

    for(unsigned int row=0; row < height; row++) {
        rgba_pixel *row_pixels;
        if (img->rows) {
            row_pixels = img->rows[row];
        } else {
            row_pixels = img->temp_row;
            row_callback((liq_color*)row_pixels, row, width, row_callback_user_info);
        }

        for(unsigned int col=0; col < width; col++) {
            img->f_pixels[row*width + col] = to_f(row_pixels[col]);
        }
    }

    if (attr->use_contrast_maps && img->width >= 4 && img->height >= 4) {
        contrast_maps(img);
    }

    return img;
}

LIQ_EXPORT liq_error liq_image_set_memory_ownership(liq_image *img, int ownership_flags)
{
    if (!CHECK_STRUCT_TYPE(img, liq_image)) return LIQ_INVALID_POINTER;
    if (!img->rows || !ownership_flags || (ownership_flags & ~(LIQ_OWN_ROWS|LIQ_OWN_PIXELS))) {
        return LIQ_VALUE_OUT_OF_RANGE;
    }

    if (ownership_flags & LIQ_OWN_ROWS) {
        img->free_rows = true;
    }

    if (ownership_flags & LIQ_OWN_PIXELS) {
        img->free_pixels = true;
        if (!img->pixels) {
            // for simplicity of this API there's no explicit bitmap argument,
            // so the row with the lowest address is assumed to be at the start of the bitmap
            img->pixels = img->rows[0];
            for(int i=1; i < img->height; i++) {
                img->pixels = MIN(img->pixels, img->rows[i]);
            }
        }
    }

    return LIQ_OK;
}

LIQ_EXPORT liq_image *liq_image_create_custom(liq_attr *attr, liq_image_get_rgba_row_callback *row_callback, void* user_info, int width, int height, double gamma)
{
    return liq_image_create_internal(attr, NULL, row_callback, user_info, width, height, gamma);
}

LIQ_EXPORT liq_image *liq_image_create_rgba_rows(liq_attr *attr, void* rows[], int width, int height, double gamma)
{
    return liq_image_create_internal(attr, (rgba_pixel**)rows, NULL, NULL, width, height, gamma);
}

LIQ_EXPORT liq_image *liq_image_create_rgba(liq_attr *attr, void* bitmap, int width, int height, double gamma)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) return NULL;
    if (width <= 0 || height <= 0 || gamma < 0 || gamma > 1.0 || !attr || !bitmap) return NULL;

    rgba_pixel *pixels = bitmap;
    rgba_pixel **rows = attr->malloc(sizeof(rows[0])*height);
    for(int i=0; i < height; i++) {
        rows[i] = pixels + width * i;
    }

    liq_image *image = liq_image_create_internal(attr, rows, NULL, NULL, width, height, gamma);
    liq_image_set_memory_ownership(image, LIQ_OWN_ROWS);
    return image;
}

static rgba_pixel *liq_image_get_row_rgba(liq_image *img, unsigned int row)
{
    if (img->rows) {
        return img->rows[row];
    }

    img->row_callback((liq_color*)img->temp_row, row, img->width, img->row_callback_user_info);
    return img->temp_row;
}

static f_pixel *liq_image_get_row_f(liq_image *input_image, unsigned int row)
{
    return input_image->f_pixels + input_image->width * row;
}

LIQ_EXPORT int liq_image_get_width(const liq_image *input_image)
{
    if (!CHECK_STRUCT_TYPE(input_image, liq_image)) return -1;
    return input_image->width;
}

LIQ_EXPORT int liq_image_get_height(const liq_image *input_image)
{
    if (!CHECK_STRUCT_TYPE(input_image, liq_image)) return -1;
    return input_image->height;
}

static void liq_image_free_rgba_source(liq_image *input_image)
{
    if (input_image->free_pixels && input_image->pixels) {
        input_image->free(input_image->pixels);
        input_image->pixels = NULL;
    }

    if (input_image->free_rows && input_image->rows) {
        input_image->free(input_image->rows);
        input_image->rows = NULL;
    }
}

LIQ_EXPORT void liq_image_destroy(liq_image *input_image)
{
    if (!CHECK_STRUCT_TYPE(input_image, liq_image)) return;

    liq_image_free_rgba_source(input_image);

    if (input_image->noise) {
        input_image->free(input_image->noise);
    }

    if (input_image->edges) {
        input_image->free(input_image->edges);
    }

    if (input_image->dither_map) {
        input_image->free(input_image->dither_map);
    }

    if (input_image->f_pixels) {
        input_image->free(input_image->f_pixels);
    }

    if (input_image->temp_row) {
        input_image->free(input_image->temp_row);
    }

    input_image->magic_header = liq_freed_magic;
    input_image->free(input_image);
}

LIQ_EXPORT liq_result *liq_quantize_image(liq_attr *attr, liq_image *img)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) return NULL;
    if (!CHECK_STRUCT_TYPE(img, liq_image)) return NULL;

    histogram *hist = get_histogram(img, attr);

    liq_result *result = pngquant_quantize(hist, attr);

    pam_freeacolorhist(hist);
    return result;
}

LIQ_EXPORT liq_error liq_set_dithering_level(liq_result *res, float dither_level)
{
    if (!CHECK_STRUCT_TYPE(res, liq_result)) return LIQ_INVALID_POINTER;

    if (res->dither_level < 0 || res->dither_level > 1.0f) return LIQ_VALUE_OUT_OF_RANGE;
    res->dither_level = dither_level;
    return LIQ_OK;
}

LIQ_EXPORT liq_remapping_result *liq_remap(liq_result *result, liq_image *image)
{
    if (!CHECK_STRUCT_TYPE(result, liq_result)) return NULL;

    liq_remapping_result *res = result->malloc(sizeof(liq_remapping_result));
    *res = (liq_remapping_result) {
        .magic_header = liq_remapping_result_magic,
        .malloc = result->malloc,
        .free = result->free,
        .dither_level = result->dither_level,
        .use_dither_map = result->use_dither_map,
        .palette_error = result->palette_error,
        .gamma = result->gamma,
        .min_opaque_val = result->min_opaque_val,
        .palette = pam_duplicate_colormap(result->palette),
    };
    return res;
}

LIQ_EXPORT double liq_get_output_gamma(const liq_remapping_result *result)
{
    if (!CHECK_STRUCT_TYPE(result, liq_remapping_result)) return -1;

    return result->gamma;
}

LIQ_EXPORT void liq_remapping_result_destroy(liq_remapping_result *result)
{
    if (!CHECK_STRUCT_TYPE(result, liq_remapping_result)) return;

    if (result->palette) pam_freecolormap(result->palette);
    if (result->pixels) result->free(result->pixels);

    result->magic_header = liq_freed_magic;
    result->free(result);
}

LIQ_EXPORT void liq_result_destroy(liq_result *res)
{
    if (!CHECK_STRUCT_TYPE(res, liq_result)) return;

    pam_freecolormap(res->palette);

    res->magic_header = liq_freed_magic;
    res->free(res);
}

LIQ_EXPORT double liq_get_remapping_error(liq_remapping_result *result)
{
    if (!CHECK_STRUCT_TYPE(result, liq_remapping_result)) return -1;

    return result->palette_error >= 0 ? result->palette_error*65536.0/6.0 : result->palette_error;
}

LIQ_EXPORT const liq_palette *liq_get_remapped_palette(liq_remapping_result *result)
{
    if (!CHECK_STRUCT_TYPE(result, liq_remapping_result)) return NULL;

    return &result->int_palette;
}

static int compare_popularity(const void *ch1, const void *ch2)
{
    const float v1 = ((const colormap_item*)ch1)->popularity;
    const float v2 = ((const colormap_item*)ch2)->popularity;
    return v1 > v2 ? 1 : -1;
}

static void sort_palette(colormap *map, const liq_attr *options)
{
    /*
    ** Step 3.5 [GRR]: remap the palette colors so that all entries with
    ** the maximal alpha value (i.e., fully opaque) are at the end and can
    ** therefore be omitted from the tRNS chunk.
    */

    if (options->last_index_transparent) for(unsigned int i=0; i < map->colors; i++) {
        if (map->palette[i].acolor.a < 1.0/256.0) {
            const unsigned int old = i, transparent_dest = map->colors-1;

            const colormap_item tmp = map->palette[transparent_dest];
            map->palette[transparent_dest] = map->palette[old];
            map->palette[old] = tmp;

            /* colors sorted by popularity make pngs slightly more compressible */
            qsort(map->palette, map->colors-1, sizeof(map->palette[0]), compare_popularity);
            return;
            }
        }

    /* move transparent colors to the beginning to shrink trns chunk */
    unsigned int num_transparent=0;
    for(unsigned int i=0; i < map->colors; i++) {
        if (map->palette[i].acolor.a < 255.0/256.0) {
            // current transparent color is swapped with earlier opaque one
            if (i != num_transparent) {
                const colormap_item tmp = map->palette[num_transparent];
                map->palette[num_transparent] = map->palette[i];
                map->palette[i] = tmp;
                i--;
            }
            num_transparent++;
        }
    }

    liq_verbose_printf(options, "  eliminated opaque tRNS-chunk entries...%d entr%s transparent", num_transparent, (num_transparent == 1)? "y" : "ies");

    /* colors sorted by popularity make pngs slightly more compressible
     * opaque and transparent are sorted separately
     */
    qsort(map->palette, num_transparent, sizeof(map->palette[0]), compare_popularity);
    qsort(map->palette+num_transparent, map->colors-num_transparent, sizeof(map->palette[0]), compare_popularity);
}

static void set_rounded_palette(liq_remapping_result *result)
{
    liq_palette *dest = &result->int_palette;
    colormap *const map = result->palette;

    to_f_set_gamma(result->gamma);

    dest->count = map->colors;
    for(unsigned int x = 0; x < map->colors; ++x) {
        rgba_pixel px = to_rgb(result->gamma, map->palette[x].acolor);
        map->palette[x].acolor = to_f(px); /* saves rounding error introduced by to_rgb, which makes remapping & dithering more accurate */

        dest->entries[x] = (liq_color){.r=px.r,.g=px.g,.b=px.b,.a=px.a};
    }
}

static float remap_to_palette(liq_image *input_image, unsigned char *const output_pixels[], colormap *const map, const float min_opaque_val)
{
    const int rows = input_image->height;
    const unsigned int cols = input_image->width;

    int remapped_pixels=0;
    float remapping_error=0;

    struct nearest_map *const n = nearest_init(map);
    const unsigned int transparent_ind = nearest_search(n, (f_pixel){0,0,0,0}, min_opaque_val, NULL);

    const unsigned int max_threads = omp_get_max_threads();
    viter_state average_color[map->colors * max_threads];
    viter_init(map, max_threads, average_color);

    #pragma omp parallel for if (rows*cols > 3000) \
        default(none) shared(average_color) reduction(+:remapping_error) reduction(+:remapped_pixels)
    for(int row = 0; row < rows; ++row) {
        f_pixel *row_pixels = liq_image_get_row_f(input_image, row);
        for(unsigned int col = 0; col < cols; ++col) {

            f_pixel px = row_pixels[col];
            unsigned int match;

            if (px.a < 1.0/256.0) {
                match = transparent_ind;
            } else {
                float diff;
                match = nearest_search(n, px, min_opaque_val, &diff);

                remapped_pixels++;
                remapping_error += diff;
            }

            output_pixels[row][col] = match;

            viter_update_color(px, 1.0, map, match, omp_get_thread_num(), average_color);
        }
    }

    viter_finalize(map, max_threads, average_color);

    nearest_free(n);

    return remapping_error / MAX(1,remapped_pixels);
}

static float distance_from_closest_other_color(const colormap *map, const unsigned int i)
{
    float second_best=MAX_DIFF;
    for(unsigned int j=0; j < map->colors; j++) {
        if (i == j) continue;
        float diff = colordifference(map->palette[i].acolor, map->palette[j].acolor);
        if (diff <= second_best) {
            second_best = diff;
        }
    }
    return second_best;
}

inline static float min_4(float a, float b, float c, float d)
{
    float x = MIN(a,b), y = MIN(c,d);
    return MIN(x,y);
}

inline static f_pixel get_dithered_pixel(const float dither_level, const float max_dither_error, const f_pixel thiserr, const f_pixel px)
{
    /* Use Floyd-Steinberg errors to adjust actual color. */
    const float sr = thiserr.r * dither_level,
                sg = thiserr.g * dither_level,
                sb = thiserr.b * dither_level,
                sa = thiserr.a * dither_level;

    float ratio = 1.0;

    // allowing some overflow prevents undithered bands caused by clamping of all channels
         if (px.r + sr > 1.03) ratio = MIN(ratio, (1.03-px.r)/sr);
    else if (px.r + sr < 0)    ratio = MIN(ratio, px.r/-sr);
         if (px.g + sg > 1.03) ratio = MIN(ratio, (1.03-px.g)/sg);
    else if (px.g + sg < 0)    ratio = MIN(ratio, px.g/-sg);
         if (px.b + sb > 1.03) ratio = MIN(ratio, (1.03-px.b)/sb);
    else if (px.b + sb < 0)    ratio = MIN(ratio, px.b/-sb);
         if (px.a + sa > 1.03) ratio = MIN(ratio, (1.03-px.a)/sa);
    else if (px.a + sa < 0)    ratio = MIN(ratio, px.a/-sa);

     // If dithering error is crazy high, don't propagate it that much
     // This prevents crazy geen pixels popping out of the blue (or red or black! ;)
     const float dither_error = sr*sr + sg*sg + sb*sb + sa*sa;
     if (dither_error > max_dither_error) {
         ratio *= 0.8;
     } else if (dither_error < 2.f/256.f/256.f) {
        // don't dither areas that don't have noticeable error — makes file smaller
        return px;
     }

     return (f_pixel){
         .r=px.r + sr * ratio,
         .g=px.g + sg * ratio,
         .b=px.b + sb * ratio,
         .a=px.a + sa * ratio,
     };
}

/**
  Uses edge/noise map to apply dithering only to flat areas. Dithering on edges creates jagged lines, and noisy areas are "naturally" dithered.

  If output_image_is_remapped is true, only pixels noticeably changed by error diffusion will be written to output image.
 */
static void remap_to_palette_floyd(liq_image *input_image, unsigned char *const output_pixels[], const colormap *map, const float min_opaque_val, bool use_dither_map, bool output_image_is_remapped, const float max_dither_error)
{
    const unsigned int rows = input_image->height, cols = input_image->width;
    const float *dither_map = use_dither_map ? (input_image->dither_map ? input_image->dither_map : input_image->edges) : NULL;

    const colormap_item *acolormap = map->palette;

    struct nearest_map *const n = nearest_init(map);
    const unsigned int transparent_ind = nearest_search(n, (f_pixel){0,0,0,0}, min_opaque_val, NULL);

    float difference_tolerance[map->colors];

    if (output_image_is_remapped) for(unsigned int i=0; i < map->colors; i++) {
            difference_tolerance[i] = distance_from_closest_other_color(map,i) / 4.f; // half of squared distance
    }

    /* Initialize Floyd-Steinberg error vectors. */
    f_pixel *restrict thiserr, *restrict nexterr;
    thiserr = input_image->malloc((cols + 2) * sizeof(*thiserr) * 2); // +2 saves from checking out of bounds access
    nexterr = thiserr + (cols + 2);
    srand(12345); /* deterministic dithering is better for comparing results */

    for (unsigned int col = 0; col < cols + 2; ++col) {
        const double rand_max = RAND_MAX;
        thiserr[col].r = ((double)rand() - rand_max/2.0)/rand_max/255.0;
        thiserr[col].g = ((double)rand() - rand_max/2.0)/rand_max/255.0;
        thiserr[col].b = ((double)rand() - rand_max/2.0)/rand_max/255.0;
        thiserr[col].a = ((double)rand() - rand_max/2.0)/rand_max/255.0;
    }

    bool fs_direction = true;
    for (unsigned int row = 0; row < rows; ++row) {
        memset(nexterr, 0, (cols + 2) * sizeof(*nexterr));

        unsigned int col = (fs_direction) ? 0 : (cols - 1);
        f_pixel *row_pixels = liq_image_get_row_f(input_image, row);

        do {
            float dither_level = dither_map ? dither_map[row*cols + col] : 15.f/16.f;
            const f_pixel spx = get_dithered_pixel(dither_level, max_dither_error, thiserr[col + 1], row_pixels[col]);

            unsigned int ind;
            if (spx.a < 1.0/256.0) {
                ind = transparent_ind;
            } else {
                unsigned int curr_ind = output_pixels[row][col];
                if (output_image_is_remapped && colordifference(map->palette[curr_ind].acolor, spx) < difference_tolerance[curr_ind]) {
                    ind = curr_ind;
                } else {
                    ind = nearest_search(n, spx, min_opaque_val, NULL);
                }
            }

            output_pixels[row][col] = ind;

            const f_pixel xp = acolormap[ind].acolor;
            f_pixel err = {
                .r = (spx.r - xp.r),
                .g = (spx.g - xp.g),
                .b = (spx.b - xp.b),
                .a = (spx.a - xp.a),
            };

            // If dithering error is crazy high, don't propagate it that much
            // This prevents crazy geen pixels popping out of the blue (or red or black! ;)
            if (err.r*err.r + err.g*err.g + err.b*err.b + err.a*err.a > max_dither_error) {
                dither_level *= 0.75;
            }

            const float colorimp = (3.0f + acolormap[ind].acolor.a)/4.0f * dither_level;
            err.r *= colorimp;
            err.g *= colorimp;
            err.b *= colorimp;
            err.a *= dither_level;

            /* Propagate Floyd-Steinberg error terms. */
            if (fs_direction) {
                thiserr[col + 2].a += (err.a * 7.0f) / 16.0f;
                thiserr[col + 2].r += (err.r * 7.0f) / 16.0f;
                thiserr[col + 2].g += (err.g * 7.0f) / 16.0f;
                thiserr[col + 2].b += (err.b * 7.0f) / 16.0f;

                nexterr[col    ].a += (err.a * 3.0f) / 16.0f;
                nexterr[col    ].r += (err.r * 3.0f) / 16.0f;
                nexterr[col    ].g += (err.g * 3.0f) / 16.0f;
                nexterr[col    ].b += (err.b * 3.0f) / 16.0f;

                nexterr[col + 1].a += (err.a * 5.0f) / 16.0f;
                nexterr[col + 1].r += (err.r * 5.0f) / 16.0f;
                nexterr[col + 1].g += (err.g * 5.0f) / 16.0f;
                nexterr[col + 1].b += (err.b * 5.0f) / 16.0f;

                nexterr[col + 2].a += (err.a       ) / 16.0f;
                nexterr[col + 2].r += (err.r       ) / 16.0f;
                nexterr[col + 2].g += (err.g       ) / 16.0f;
                nexterr[col + 2].b += (err.b       ) / 16.0f;
            } else {
                thiserr[col    ].a += (err.a * 7.0f) / 16.0f;
                thiserr[col    ].r += (err.r * 7.0f) / 16.0f;
                thiserr[col    ].g += (err.g * 7.0f) / 16.0f;
                thiserr[col    ].b += (err.b * 7.0f) / 16.0f;

                nexterr[col    ].a += (err.a       ) / 16.0f;
                nexterr[col    ].r += (err.r       ) / 16.0f;
                nexterr[col    ].g += (err.g       ) / 16.0f;
                nexterr[col    ].b += (err.b       ) / 16.0f;

                nexterr[col + 1].a += (err.a * 5.0f) / 16.0f;
                nexterr[col + 1].r += (err.r * 5.0f) / 16.0f;
                nexterr[col + 1].g += (err.g * 5.0f) / 16.0f;
                nexterr[col + 1].b += (err.b * 5.0f) / 16.0f;

                nexterr[col + 2].a += (err.a * 3.0f) / 16.0f;
                nexterr[col + 2].r += (err.r * 3.0f) / 16.0f;
                nexterr[col + 2].g += (err.g * 3.0f) / 16.0f;
                nexterr[col + 2].b += (err.b * 3.0f) / 16.0f;
            }

            // remapping is done in zig-zag
            if (fs_direction) {
                ++col;
                if (col >= cols) break;
            } else {
                if (col <= 0) break;
                --col;
            }
        }
        while(1);

        f_pixel *const temperr = thiserr;
        thiserr = nexterr;
        nexterr = temperr;
        fs_direction = !fs_direction;
    }

    input_image->free(MIN(thiserr, nexterr)); // MIN because pointers were swapped
    nearest_free(n);
}


/* histogram contains information how many times each color is present in the image, weighted by importance_map */
static histogram *get_histogram(liq_image *input_image, liq_attr *options)
{
    unsigned int ignorebits=options->min_posterization;
    const unsigned int cols = input_image->width, rows = input_image->height;

   /*
    ** Step 2: attempt to make a histogram of the colors, unclustered.
    ** If at first we don't succeed, increase ignorebits to increase color
    ** coherence and try again.
    */

    unsigned int maxcolors = options->max_histogram_entries;

    struct acolorhash_table *acht;
    do {
        acht = pam_allocacolorhash(maxcolors, rows*cols, ignorebits, options->malloc, options->free);

        // histogram uses noise contrast map for importance. Color accuracy in noisy areas is not very important.
        // noise map does not include edges to avoid ruining anti-aliasing
        for(unsigned int row=0; row < rows; row++) {
            const rgba_pixel* rows_p[1] = { liq_image_get_row_rgba(input_image, row) };
            if (!pam_computeacolorhash(acht, rows_p, cols, 1, input_image->noise ? &input_image->noise[row * cols] : NULL)) {

                ignorebits++;
                liq_verbose_printf(options, "  too many colors! Scaling colors to improve clustering... %d", ignorebits);
                pam_freeacolorhash(acht);
                acht = NULL;
                break;
            }
        }
    } while(!acht);

    if (input_image->noise) {
        input_image->free(input_image->noise);
        input_image->noise = NULL;
    }

    liq_image_free_rgba_source(input_image); // creation if liq_image makes copy of source pixels

    histogram *hist = pam_acolorhashtoacolorhist(acht, input_image->gamma, options->malloc, options->free);
    pam_freeacolorhash(acht);

    liq_verbose_printf(options, "  made histogram...%d colors found", hist->size);
    return hist;
}

static void modify_alpha(liq_image *input_image, const float min_opaque_val)
{
    /* IE6 makes colors with even slightest transparency completely transparent,
       thus to improve situation in IE, make colors that are less than ~10% transparent
       completely opaque */

    const unsigned int rows = input_image->height, cols = input_image->width;
    const float almost_opaque_val = min_opaque_val * 169.f/256.f;

    for(unsigned int row = 0; row < rows; ++row) {
        f_pixel *row_pixels = liq_image_get_row_f(input_image, row);
        for(unsigned int col = 0; col < cols; col++) {
            float al = row_pixels[col].a;

            /* ie bug: to avoid visible step caused by forced opaqueness, linearily raise opaqueness of almost-opaque colors */
            if (al > almost_opaque_val) {
                al = almost_opaque_val + (al-almost_opaque_val) * (1.f-almost_opaque_val) / (min_opaque_val-almost_opaque_val);
                row_pixels[col].a = MIN(1.f, al);
            }
        }
    }
}

/**
 Builds two maps:
    noise - approximation of areas with high-frequency noise, except straight edges. 1=flat, 0=noisy.
    edges - noise map including all edges
 */
static void contrast_maps(liq_image *image)
{
    const int cols = image->width, rows = image->height;
    float *restrict noise = image->malloc(sizeof(float)*cols*rows);
    float *restrict edges = image->malloc(sizeof(float)*cols*rows);
    float *restrict tmp = image->malloc(sizeof(float)*cols*rows);

    f_pixel *curr_row, *prev_row, *next_row;
    curr_row = prev_row = next_row = liq_image_get_row_f(image, 0);

    for (unsigned int j=0; j < rows; j++) {
        prev_row = curr_row;
        curr_row = next_row;
        next_row = liq_image_get_row_f(image, MIN(rows-1,j+1));

        f_pixel prev, curr = curr_row[0], next=curr;
        for (unsigned int i=0; i < cols; i++) {
            prev=curr;
            curr=next;
            next = curr_row[MIN(cols-1,i+1)];

            // contrast is difference between pixels neighbouring horizontally and vertically
            const float a = fabsf(prev.a+next.a - curr.a*2.f),
            r = fabsf(prev.r+next.r - curr.r*2.f),
            g = fabsf(prev.g+next.g - curr.g*2.f),
            b = fabsf(prev.b+next.b - curr.b*2.f);

            const f_pixel prevl = prev_row[i];
            const f_pixel nextl = next_row[i];

            const float a1 = fabsf(prevl.a+nextl.a - curr.a*2.f),
            r1 = fabsf(prevl.r+nextl.r - curr.r*2.f),
            g1 = fabsf(prevl.g+nextl.g - curr.g*2.f),
            b1 = fabsf(prevl.b+nextl.b - curr.b*2.f);

            const float horiz = MAX(MAX(a,r),MAX(g,b));
            const float vert = MAX(MAX(a1,r1),MAX(g1,b1));
            const float edge = MAX(horiz,vert);
            float z = edge - fabsf(horiz-vert)*.5f;
            z = 1.f - MAX(z,MIN(horiz,vert));
            z *= z; // noise is amplified
            z *= z;

            noise[j*cols+i] = z;
            edges[j*cols+i] = 1.f-edge;
        }
    }

    // noise areas are shrunk and then expanded to remove thin edges from the map
    max3(noise, tmp, cols, rows);
    max3(tmp, noise, cols, rows);

    blur(noise, tmp, noise, cols, rows, 3);

    max3(noise, tmp, cols, rows);

    min3(tmp, noise, cols, rows);
    min3(noise, tmp, cols, rows);
    min3(tmp, noise, cols, rows);

    min3(edges, tmp, cols, rows);
    max3(tmp, edges, cols, rows);
    for(unsigned int i=0; i < cols*rows; i++) edges[i] = MIN(noise[i], edges[i]);

    image->free(tmp);

    image->noise = noise;
    image->edges = edges;
}

/**
 * Builds map of neighbor pixels mapped to the same palette entry
 *
 * For efficiency/simplicity it mainly looks for same consecutive pixels horizontally
 * and peeks 1 pixel above/below. Full 2d algorithm doesn't improve it significantly.
 * Correct flood fill doesn't have visually good properties.
 */
static void update_dither_map(unsigned char *const *const row_pointers, liq_image *input_image)
{
    const unsigned int width = input_image->width;
    const unsigned int height = input_image->height;
    float *const edges = input_image->edges;

    for(unsigned int row=0; row < height; row++) {
        unsigned char lastpixel = row_pointers[row][0];
        unsigned int lastcol=0;

        for(unsigned int col=1; col < width; col++) {
            const unsigned char px = row_pointers[row][col];

            if (px != lastpixel || col == width-1) {
                float neighbor_count = 2.5f + col-lastcol;

                unsigned int i=lastcol;
                while(i < col) {
                    if (row > 0) {
                        unsigned char pixelabove = row_pointers[row-1][i];
                        if (pixelabove == lastpixel) neighbor_count += 1.f;
                    }
                    if (row < height-1) {
                        unsigned char pixelbelow = row_pointers[row+1][i];
                        if (pixelbelow == lastpixel) neighbor_count += 1.f;
                    }
                    i++;
                }

                while(lastcol <= col) {
                    edges[row*width + lastcol++] *= 1.f - 2.5f/neighbor_count;
                }
                lastpixel = px;
            }
        }
    }
    input_image->dither_map = input_image->edges;
    input_image->edges = NULL;
}

static void adjust_histogram_callback(hist_item *item, float diff)
{
    item->adjusted_weight = (item->perceptual_weight+item->adjusted_weight) * (sqrtf(1.f+diff));
}

/**
 Repeats mediancut with different histogram weights to find palette with minimum error.

 feedback_loop_trials controls how long the search will take. < 0 skips the iteration.
 */
static colormap *find_best_palette(histogram *hist, const liq_attr *options, double *palette_error_p)
{
    unsigned int max_colors = options->max_colors;
    const double target_mse = options->target_mse;
    int feedback_loop_trials = options->feedback_loop_trials;
    colormap *acolormap = NULL;
    double least_error = MAX_DIFF;
    double target_mse_overshoot = feedback_loop_trials>0 ? 1.05 : 1.0;
    const double percent = (double)(feedback_loop_trials>0?feedback_loop_trials:1)/100.0;

    do {
        colormap *newmap = mediancut(hist, options->min_opaque_val, max_colors, target_mse * target_mse_overshoot, MAX(MAX(90.0/65536.0, target_mse), least_error)*1.2);

        if (feedback_loop_trials <= 0) {
            return newmap;
        }

        // after palette has been created, total error (MSE) is calculated to keep the best palette
        // at the same time Voronoi iteration is done to improve the palette
        // and histogram weights are adjusted based on remapping error to give more weight to poorly matched colors

        const bool first_run_of_target_mse = !acolormap && target_mse > 0;
        double total_error = viter_do_iteration(hist, newmap, options->min_opaque_val, first_run_of_target_mse ? NULL : adjust_histogram_callback);

        // goal is to increase quality or to reduce number of colors used if quality is good enough
        if (!acolormap || total_error < least_error || (total_error <= target_mse && newmap->colors < max_colors)) {
            if (acolormap) pam_freecolormap(acolormap);
            acolormap = newmap;

            if (total_error < target_mse && total_error > 0) {
                // voronoi iteration improves quality above what mediancut aims for
                // this compensates for it, making mediancut aim for worse
                target_mse_overshoot = MIN(target_mse_overshoot*1.25, target_mse/total_error);
            }

            least_error = total_error;

            // if number of colors could be reduced, try to keep it that way
            // but allow extra color as a bit of wiggle room in case quality can be improved too
            max_colors = MIN(newmap->colors+1, max_colors);

            feedback_loop_trials -= 1; // asymptotic improvement could make it go on forever
        } else {
            for(unsigned int j=0; j < hist->size; j++) {
                hist->achv[j].adjusted_weight = (hist->achv[j].perceptual_weight + hist->achv[j].adjusted_weight)/2.0;
            }

            target_mse_overshoot = 1.0;
            feedback_loop_trials -= 6;
            // if error is really bad, it's unlikely to improve, so end sooner
            if (total_error > least_error*4) feedback_loop_trials -= 3;
            pam_freecolormap(newmap);
        }

        liq_verbose_printf(options, "  selecting colors...%d%%",100-MAX(0,(int)(feedback_loop_trials/percent)));
    }
    while(feedback_loop_trials > 0);

    *palette_error_p = least_error;
    return acolormap;
}

static liq_result *pngquant_quantize(histogram *hist, const liq_attr *options)
{
    colormap *acolormap;
    double palette_error = -1;

    // If image has few colors to begin with (and no quality degradation is required)
    // then it's possible to skip quantization entirely
    if (hist->size <= options->max_colors && options->target_mse == 0) {
        acolormap = pam_colormap(hist->size);
        for(unsigned int i=0; i < hist->size; i++) {
            acolormap->palette[i].acolor = hist->achv[i].acolor;
            acolormap->palette[i].popularity = hist->achv[i].perceptual_weight;
        }
        palette_error = 0;
    } else {
        acolormap = find_best_palette(hist, options, &palette_error);

    // Voronoi iteration approaches local minimum for the palette
    const double max_mse = options->max_mse;
    const double iteration_limit = options->voronoi_iteration_limit;
    unsigned int iterations = options->voronoi_iterations;

        if (!iterations && palette_error < 0 && max_mse < MAX_DIFF) iterations = 1; // otherwise total error is never calculated and MSE limit won't work

        if (iterations) {
            verbose_print(options, "  moving colormap towards local minimum");

            double previous_palette_error = MAX_DIFF;

            for(unsigned int i=0; i < iterations; i++) {
                palette_error = viter_do_iteration(hist, acolormap, options->min_opaque_val, NULL);

                if (fabs(previous_palette_error-palette_error) < iteration_limit) {
                    break;
                }

                if (palette_error > max_mse*1.5) { // probably hopeless
                    if (palette_error > max_mse*3.0) break; // definitely hopeless
                    iterations++;
                }

                previous_palette_error = palette_error;
            }
        }

        if (palette_error > max_mse) {
            liq_verbose_printf(options, "  image degradation MSE=%.3f exceeded limit of %.3f", palette_error*65536.0/6.0, max_mse*65536.0/6.0);
            pam_freecolormap(acolormap);
            return NULL;
        }
    }

    sort_palette(acolormap, options);

    liq_result *result = options->malloc(sizeof(liq_result));
    *result = (liq_result){
        .magic_header = liq_result_magic,
        .malloc = options->malloc,
        .free = options->free,
        .palette = acolormap,
        .palette_error = palette_error,
        .use_dither_map = options->use_dither_map,
        .min_opaque_val = options->min_opaque_val,
        .gamma = 0.45455, // fixed gamma ~2.2 for the web. PNG can't store exact 1/2.2
    };
    return result;
}

LIQ_EXPORT liq_error liq_write_remapped_image(liq_remapping_result *result, liq_image *input_image, void *buffer, size_t buffer_size)
{
    if (!CHECK_STRUCT_TYPE(result, liq_remapping_result)) return LIQ_INVALID_POINTER;
    if (!CHECK_STRUCT_TYPE(input_image, liq_image)) return LIQ_INVALID_POINTER;

    const size_t required_size = input_image->width * input_image->height;
    if (buffer_size < required_size) {
        return LIQ_BUFFER_TOO_SMALL;
    }

    unsigned char *rows[input_image->height];
    unsigned char *buffer_bytes = buffer;
    for(unsigned int i=0; i < input_image->height; i++) {
        rows[i] = &buffer_bytes[input_image->width * i];
    }
    return liq_write_remapped_image_rows(result, input_image, rows);
}

LIQ_EXPORT liq_error liq_write_remapped_image_rows(liq_remapping_result *result, liq_image *input_image, unsigned char **row_pointers)
{
    if (!CHECK_STRUCT_TYPE(result, liq_remapping_result)) return LIQ_INVALID_POINTER;
    if (!CHECK_STRUCT_TYPE(input_image, liq_image)) return LIQ_INVALID_POINTER;

    /*
     ** Step 4: map the colors in the image to their closest match in the
     ** new colormap, and write 'em out.
     */

    float remapping_error = result->palette_error;
    if (result->dither_level == 0) {
        set_rounded_palette(result);
        remapping_error = remap_to_palette(input_image, row_pointers, result->palette, result->min_opaque_val);
    } else {
        const bool generate_dither_map = result->use_dither_map && (input_image->edges && !input_image->dither_map);
        if (generate_dither_map) {
            // If dithering (with dither map) is required, this image is used to find areas that require dithering
            remapping_error = remap_to_palette(input_image, row_pointers, result->palette, result->min_opaque_val);
            update_dither_map(row_pointers, input_image);
        }

        // remapping above was the last chance to do voronoi iteration, hence the final palette is set after remapping
        set_rounded_palette(result);

        remap_to_palette_floyd(input_image, row_pointers, result->palette, result->min_opaque_val, result->use_dither_map, generate_dither_map, MAX(remapping_error*2.4, 16.f/256.f));
    }

    // remapping error from dithered image is absurd, so always non-dithered value is used
    // palette_error includes some perceptual weighting from histogram which is closer correlated with dssim
    // so that should be used when possible.
    if (result->palette_error < 0) {
        result->palette_error = remapping_error;
    }

    return LIQ_OK;
}
