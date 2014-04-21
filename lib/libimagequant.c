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
#include <stdint.h>
#include <limits.h>

#if !(defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199900L) && !(defined(_MSC_VER) && _MSC_VER >= 1800)
#error "This program requires C99, e.g. -std=c99 switch in GCC or it requires MSVC 18.0 or higher."
#error "Ignore torrent of syntax errors that may follow. It's only because compiler is set to use too old C version."
#endif

#ifdef _OPENMP
#include <omp.h>
#else
#define omp_get_max_threads() 1
#define omp_get_thread_num() 0
#endif

#include "libimagequant.h"

#include "pam.h"
#include "mediancut.h"
#include "nearest.h"
#include "blur.h"
#include "viter.h"

#define LIQ_HIGH_MEMORY_LIMIT (1<<26)  /* avoid allocating buffers larger than 64MB */

// each structure has a pointer as a unique identifier that allows type checking at run time
static const char *const liq_attr_magic = "liq_attr", *const liq_image_magic = "liq_image",
     *const liq_result_magic = "liq_result", *const liq_remapping_result_magic = "liq_remapping_result",
     *const liq_freed_magic = "free";
#define CHECK_STRUCT_TYPE(attr, kind) liq_crash_if_invalid_handle_pointer_given((const liq_attr*)attr, kind ## _magic)
#define CHECK_USER_POINTER(ptr) liq_crash_if_invalid_pointer_given(ptr)

struct liq_attr {
    const char *magic_header;
    void* (*malloc)(size_t);
    void (*free)(void*);

    double target_mse, max_mse, voronoi_iteration_limit;
    float min_opaque_val;
    unsigned int max_colors, max_histogram_entries;
    unsigned int min_posterization_output /* user setting */, min_posterization_input /* speed setting */;
    unsigned int voronoi_iterations, feedback_loop_trials;
    bool last_index_transparent, use_contrast_maps, use_dither_map, fast_palette;
    unsigned int speed;
    liq_log_callback_function *log_callback;
    void *log_callback_user_info;
    liq_log_flush_callback_function *log_flush_callback;
    void *log_flush_callback_user_info;
};

struct liq_image {
    const char *magic_header;
    void* (*malloc)(size_t);
    void (*free)(void*);

    f_pixel *f_pixels;
    rgba_pixel **rows;
    double gamma;
    unsigned int width, height;
    unsigned char *noise, *edges, *dither_map;
    rgba_pixel *pixels, *temp_row;
    f_pixel *temp_f_row;
    liq_image_get_rgba_row_callback *row_callback;
    void *row_callback_user_info;
    float min_opaque_val;
    bool free_pixels, free_rows, free_rows_internal;
};

typedef struct liq_remapping_result {
    const char *magic_header;
    void* (*malloc)(size_t);
    void (*free)(void*);

    unsigned char *pixels;
    colormap *palette;
    liq_palette int_palette;
    double gamma, palette_error;
    float dither_level;
    bool use_dither_map;
} liq_remapping_result;

struct liq_result {
    const char *magic_header;
    void* (*malloc)(size_t);
    void (*free)(void*);

    liq_remapping_result *remapping;
    colormap *palette;
    liq_palette int_palette;
    float dither_level;
    double gamma, palette_error;
    int min_posterization_output;
    bool use_dither_map, fast_palette;
};

static liq_result *pngquant_quantize(histogram *hist, const liq_attr *options, double gamma);
static void modify_alpha(liq_image *input_image, rgba_pixel *const row_pixels);
static void contrast_maps(liq_image *image);
static histogram *get_histogram(liq_image *input_image, const liq_attr *options);
static const rgba_pixel *liq_image_get_row_rgba(liq_image *input_image, unsigned int row);
static const f_pixel *liq_image_get_row_f(liq_image *input_image, unsigned int row);
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
    if (attr->log_callback) {
        attr->log_callback(attr, msg, attr->log_callback_user_info);
    }
}

static void liq_verbose_printf_flush(liq_attr *attr)
{
    if (attr->log_flush_callback) {
        attr->log_flush_callback(attr, attr->log_flush_callback_user_info);
    }
}

#if USE_SSE
inline static bool is_sse_available()
{
#if (defined(__x86_64__) || defined(__amd64))
    return true;
#else
    int a,b,c,d;
        cpuid(1, a, b, c, d);
    return d & (1<<25); // edx bit 25 is set when SSE is present
#endif
}
#endif

/* make it clear in backtrace when user-supplied handle points to invalid memory */
NEVER_INLINE LIQ_EXPORT bool liq_crash_if_invalid_handle_pointer_given(const liq_attr *user_supplied_pointer, const char *const expected_magic_header);
LIQ_EXPORT bool liq_crash_if_invalid_handle_pointer_given(const liq_attr *user_supplied_pointer, const char *const expected_magic_header)
{
    if (!user_supplied_pointer) {
        return false;
    }

    if (user_supplied_pointer->magic_header == liq_freed_magic) {
        fprintf(stderr, "%s used after being freed", expected_magic_header);
        // this is not normal error handling, this is programmer error that should crash the program.
        // program cannot safely continue if memory has been used after it's been freed.
        // abort() is nasty, but security vulnerability may be worse.
        abort();
    }

    return user_supplied_pointer->magic_header == expected_magic_header;
}

NEVER_INLINE LIQ_EXPORT bool liq_crash_if_invalid_pointer_given(void *pointer);
LIQ_EXPORT bool liq_crash_if_invalid_pointer_given(void *pointer)
{
    if (!pointer) {
        return false;
    }
    // Force a read from the given (potentially invalid) memory location in order to check early whether this crashes the program or not.
    // It doesn't matter what value is read, the code here is just to shut the compiler up about unused read.
    char test_access = *((volatile char *)pointer);
    return test_access || true;
}

static void liq_log_error(const liq_attr *attr, const char *msg) {
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) return;
    liq_verbose_printf(attr, "  error: %s", msg);
}

static double quality_to_mse(long quality)
{
    if (quality == 0) {
        return MAX_DIFF;
    }
    if (quality == 100) {
        return 0;
    }

    // curve fudged to be roughly similar to quality of libjpeg
    // except lowest 10 for really low number of colors
    const double extra_low_quality_fudge = MAX(0,0.016/(0.001+quality) - 0.001);
    return extra_low_quality_fudge + 2.5/pow(210.0 + quality, 1.2) * (100.1-quality)/100.0;
}

static unsigned int mse_to_quality(double mse)
{
    for(int i=100; i > 0; i--) {
        if (mse <= quality_to_mse(i) + 0.000001) { // + epsilon for floating point errors
            return i;
        }
    }
    return 0;
}

LIQ_EXPORT liq_error liq_set_quality(liq_attr* attr, int minimum, int target)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) return LIQ_INVALID_POINTER;
    if (target < 0 || target > 100 || target < minimum || minimum < 0) return LIQ_VALUE_OUT_OF_RANGE;

    attr->target_mse = quality_to_mse(target);
    attr->max_mse = quality_to_mse(minimum);
    return LIQ_OK;
}

LIQ_EXPORT int liq_get_min_quality(const liq_attr *attr)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) return -1;
    return mse_to_quality(attr->max_mse);
}

LIQ_EXPORT int liq_get_max_quality(const liq_attr *attr)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) return -1;
    return mse_to_quality(attr->target_mse);
}


LIQ_EXPORT liq_error liq_set_max_colors(liq_attr* attr, int colors)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) return LIQ_INVALID_POINTER;
    if (colors < 2 || colors > 256) return LIQ_VALUE_OUT_OF_RANGE;

    attr->max_colors = colors;
    return LIQ_OK;
}

LIQ_EXPORT int liq_get_max_colors(const liq_attr *attr)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) return -1;

    return attr->max_colors;
}

LIQ_EXPORT liq_error liq_set_min_posterization(liq_attr *attr, int bits)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) return LIQ_INVALID_POINTER;
    if (bits < 0 || bits > 4) return LIQ_VALUE_OUT_OF_RANGE;

    attr->min_posterization_output = bits;
    return LIQ_OK;
}

LIQ_EXPORT int liq_get_min_posterization(const liq_attr *attr)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) return -1;

    return attr->min_posterization_output;
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
    attr->min_posterization_input = (speed >= 8) ? 1 : 0;
    attr->fast_palette = (speed >= 7);
    attr->use_dither_map = (speed <= (omp_get_max_threads() > 1 ? 7 : 5)); // parallelized dither map might speed up floyd remapping
    attr->use_contrast_maps = (speed <= 7) || attr->use_dither_map;
    attr->speed = speed;
    return LIQ_OK;
}

LIQ_EXPORT int liq_get_speed(const liq_attr *attr)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) return -1;

    return attr->speed;
}

LIQ_EXPORT liq_error liq_set_output_gamma(liq_result* res, double gamma)
{
    if (!CHECK_STRUCT_TYPE(res, liq_result)) return LIQ_INVALID_POINTER;
    if (gamma <= 0 || gamma >= 1.0) return LIQ_VALUE_OUT_OF_RANGE;

    if (res->remapping) {
        liq_remapping_result_destroy(res->remapping);
        res->remapping = NULL;
    }

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

LIQ_EXPORT int liq_get_min_opacity(const liq_attr *attr)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) return -1;

    return MIN(255, 256.0 * attr->min_opaque_val);
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
    return liq_attr_create_with_allocator(NULL, NULL);
}

LIQ_EXPORT void liq_attr_destroy(liq_attr *attr)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) {
        return;
    }

    liq_verbose_printf_flush(attr);

    attr->magic_header = liq_freed_magic;
    attr->free(attr);
}

LIQ_EXPORT liq_attr* liq_attr_copy(liq_attr *orig)
{
    if (!CHECK_STRUCT_TYPE(orig, liq_attr)) {
        return NULL;
    }

    liq_attr *attr = orig->malloc(sizeof(liq_attr));
    if (!attr) return NULL;
    *attr = *orig;
    return attr;
}

static void *liq_aligned_malloc(size_t size)
{
    unsigned char *ptr = malloc(size + 16);
    if (!ptr) {
        return NULL;
    }

    uintptr_t offset = 16 - ((uintptr_t)ptr & 15); // also reserves 1 byte for ptr[-1]
    ptr += offset;
    assert(0 == (((uintptr_t)ptr) & 15));
    ptr[-1] = offset ^ 0x59; // store how much pointer was shifted to get the original for free()
    return ptr;
}

static void liq_aligned_free(void *inptr)
{
    unsigned char *ptr = inptr;
    size_t offset = ptr[-1] ^ 0x59;
    assert(offset > 0 && offset <= 16);
    free(ptr - offset);
}

LIQ_EXPORT liq_attr* liq_attr_create_with_allocator(void* (*custom_malloc)(size_t), void (*custom_free)(void*))
{
#if USE_SSE
    if (!is_sse_available()) {
        return NULL;
    }
#endif
    if (!custom_malloc && !custom_free) {
        custom_malloc = liq_aligned_malloc;
        custom_free = liq_aligned_free;
    } else if (!custom_malloc != !custom_free) {
        return NULL; // either specify both or none
    }

    liq_attr *attr = custom_malloc(sizeof(liq_attr));
    if (!attr) return NULL;
    *attr = (liq_attr) {
        .magic_header = liq_attr_magic,
        .malloc = custom_malloc,
        .free = custom_free,
        .max_colors = 256,
        .min_opaque_val = 1, // whether preserve opaque colors for IE (1.0=no, does not affect alpha)
        .last_index_transparent = false, // puts transparent color at last index. This is workaround for blu-ray subtitles.
        .target_mse = 0,
        .max_mse = MAX_DIFF,
    };
    liq_set_speed(attr, 3);
    return attr;
}

static bool liq_image_use_low_memory(liq_image *img)
{
    img->temp_f_row = img->malloc(sizeof(img->f_pixels[0]) * img->width * omp_get_max_threads());
    return img->temp_f_row != NULL;
}

static bool liq_image_should_use_low_memory(liq_image *img, const bool low_memory_hint)
{
    return img->width * img->height * sizeof(f_pixel) > (low_memory_hint ? LIQ_HIGH_MEMORY_LIMIT/8 : LIQ_HIGH_MEMORY_LIMIT);
}

static liq_image *liq_image_create_internal(liq_attr *attr, rgba_pixel* rows[], liq_image_get_rgba_row_callback *row_callback, void *row_callback_user_info, int width, int height, double gamma)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) {
        return NULL;
    }
    if (width <= 0 || height <= 0) {
        liq_log_error(attr, "width and height must be > 0");
        return NULL;
    }
    if (gamma < 0 || gamma > 1.0) {
        liq_log_error(attr, "gamma must be >= 0 and <= 1 (try 1/gamma instead)");
        return NULL;
    }

    if (!rows && !row_callback) {
        liq_log_error(attr, "missing row data");
        return NULL;
    }

    liq_image *img = attr->malloc(sizeof(liq_image));
    if (!img) return NULL;
    *img = (liq_image){
        .magic_header = liq_image_magic,
        .malloc = attr->malloc,
        .free = attr->free,
        .width = width, .height = height,
        .gamma = gamma ? gamma : 0.45455,
        .rows = rows,
        .row_callback = row_callback,
        .row_callback_user_info = row_callback_user_info,
        .min_opaque_val = attr->min_opaque_val,
    };

    if (!rows || attr->min_opaque_val < 1.f) {
        img->temp_row = attr->malloc(sizeof(img->temp_row[0]) * width * omp_get_max_threads());
        if (!img->temp_row) return NULL;
    }

    // if image is huge or converted pixels are not likely to be reused then don't cache converted pixels
    if (liq_image_should_use_low_memory(img, !img->temp_row && !attr->use_contrast_maps && !attr->use_dither_map)) {
        verbose_print(attr, "  conserving memory");
        if (!liq_image_use_low_memory(img)) return NULL;
    }

    if (img->min_opaque_val < 1.f) {
        verbose_print(attr, "  Working around IE6 bug by making image less transparent...");
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
        if (img->free_rows_internal) return LIQ_VALUE_OUT_OF_RANGE;
        img->free_rows = true;
    }

    if (ownership_flags & LIQ_OWN_PIXELS) {
        img->free_pixels = true;
        if (!img->pixels) {
            // for simplicity of this API there's no explicit bitmap argument,
            // so the row with the lowest address is assumed to be at the start of the bitmap
            img->pixels = img->rows[0];
            for(unsigned int i=1; i < img->height; i++) {
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
    if (width <= 0 || height <= 0) {
        liq_log_error(attr, "width and height must be > 0");
        return NULL;
    }
    if (width > INT_MAX/16/height || height > INT_MAX/16/width) {
        liq_log_error(attr, "image too large");
        return NULL;
    }

    for(int i=0; i < height; i++) {
        if (!CHECK_USER_POINTER(rows+i) || !CHECK_USER_POINTER(rows[i])) {
            liq_log_error(attr, "invalid row pointers");
            return NULL;
        }
    }
    return liq_image_create_internal(attr, (rgba_pixel**)rows, NULL, NULL, width, height, gamma);
}

LIQ_EXPORT liq_image *liq_image_create_rgba(liq_attr *attr, void* bitmap, int width, int height, double gamma)
{
    if (!CHECK_STRUCT_TYPE(attr, liq_attr)) return NULL;
    if (width <= 0 || height <= 0) {
        liq_log_error(attr, "width and height must be > 0");
        return NULL;
    }
    if (width > INT_MAX/16/height || height > INT_MAX/16/width) {
        liq_log_error(attr, "image too large");
        return NULL;
    }
    if (!CHECK_USER_POINTER(bitmap)) {
        liq_log_error(attr, "invalid bitmap pointer");
        return NULL;
    }

    rgba_pixel *pixels = bitmap;
    rgba_pixel **rows = attr->malloc(sizeof(rows[0])*height);
    if (!rows) return NULL;

    for(int i=0; i < height; i++) {
        rows[i] = pixels + width * i;
    }

    liq_image *image = liq_image_create_internal(attr, rows, NULL, NULL, width, height, gamma);
    image->free_rows = true;
    image->free_rows_internal = true;
    return image;
}

NEVER_INLINE LIQ_EXPORT void liq_executing_user_callback(liq_image_get_rgba_row_callback *callback, liq_color *temp_row, int row, int width, void *user_info);
LIQ_EXPORT void liq_executing_user_callback(liq_image_get_rgba_row_callback *callback, liq_color *temp_row, int row, int width, void *user_info)
{
    assert(callback);
    assert(temp_row);
    callback(temp_row, row, width, user_info);
}

inline static bool liq_image_can_use_rows(liq_image *img)
{
    const bool iebug = img->min_opaque_val < 1.f;
    return (img->rows && !iebug);
}

static const rgba_pixel *liq_image_get_row_rgba(liq_image *img, unsigned int row)
{
    if (liq_image_can_use_rows(img)) {
        return img->rows[row];
    }

    assert(img->temp_row);
    rgba_pixel *temp_row = img->temp_row + img->width * omp_get_thread_num();
    if (img->rows) {
        memcpy(temp_row, img->rows[row], img->width * sizeof(temp_row[0]));
    } else {
        liq_executing_user_callback(img->row_callback, (liq_color*)temp_row, row, img->width, img->row_callback_user_info);
    }

    if (img->min_opaque_val < 1.f) modify_alpha(img, temp_row);
    return temp_row;
}

static void convert_row_to_f(liq_image *img, f_pixel *row_f_pixels, const unsigned int row, const float gamma_lut[])
{
    assert(row_f_pixels);
    assert(!USE_SSE || 0 == ((uintptr_t)row_f_pixels & 15));

    const rgba_pixel *const row_pixels = liq_image_get_row_rgba(img, row);

    for(unsigned int col=0; col < img->width; col++) {
        row_f_pixels[col] = to_f(gamma_lut, row_pixels[col]);
    }
}

static const f_pixel *liq_image_get_row_f(liq_image *img, unsigned int row)
{
    if (!img->f_pixels) {
        if (img->temp_f_row) {
            float gamma_lut[256];
            to_f_set_gamma(gamma_lut, img->gamma);
            f_pixel *row_for_thread = img->temp_f_row + img->width * omp_get_thread_num();
            convert_row_to_f(img, row_for_thread, row, gamma_lut);
            return row_for_thread;
        }

        assert(omp_get_thread_num() == 0);
        if (!liq_image_should_use_low_memory(img, false)) {
            img->f_pixels = img->malloc(sizeof(img->f_pixels[0]) * img->width * img->height);
        }
        if (!img->f_pixels) {
            if (!liq_image_use_low_memory(img)) return NULL;
            return liq_image_get_row_f(img, row);
        }

        float gamma_lut[256];
        to_f_set_gamma(gamma_lut, img->gamma);
        for(unsigned int i=0; i < img->height; i++) {
            convert_row_to_f(img, &img->f_pixels[i*img->width], i, gamma_lut);
        }
    }
    return img->f_pixels + img->width * row;
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

typedef void free_func(void*);

free_func *get_default_free_func(liq_image *img)
{
    // When default allocator is used then user-supplied pointers must be freed with free()
    if (img->free_rows_internal || img->free != liq_aligned_free) {
        return img->free;
    }
    return free;
}

static void liq_image_free_rgba_source(liq_image *input_image)
{
    if (input_image->free_pixels && input_image->pixels) {
        get_default_free_func(input_image)(input_image->pixels);
        input_image->pixels = NULL;
    }

    if (input_image->free_rows && input_image->rows) {
        get_default_free_func(input_image)(input_image->rows);
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
    if (!CHECK_STRUCT_TYPE(img, liq_image)) {
        liq_log_error(attr, "invalid image pointer");
        return NULL;
    }

    histogram *hist = get_histogram(img, attr);
    if (!hist) {
        return NULL;
    }

    liq_result *result = pngquant_quantize(hist, attr, img->gamma);

    pam_freeacolorhist(hist);
    return result;
}

LIQ_EXPORT liq_error liq_set_dithering_level(liq_result *res, float dither_level)
{
    if (!CHECK_STRUCT_TYPE(res, liq_result)) return LIQ_INVALID_POINTER;

    if (res->remapping) {
        liq_remapping_result_destroy(res->remapping);
        res->remapping = NULL;
    }

    if (res->dither_level < 0 || res->dither_level > 1.0f) return LIQ_VALUE_OUT_OF_RANGE;
    res->dither_level = dither_level;
    return LIQ_OK;
}

static liq_remapping_result *liq_remapping_result_create(liq_result *result)
{
    if (!CHECK_STRUCT_TYPE(result, liq_result)) {
        return NULL;
    }

    liq_remapping_result *res = result->malloc(sizeof(liq_remapping_result));
    if (!res) return NULL;
    *res = (liq_remapping_result) {
        .magic_header = liq_remapping_result_magic,
        .malloc = result->malloc,
        .free = result->free,
        .dither_level = result->dither_level,
        .use_dither_map = result->use_dither_map,
        .palette_error = result->palette_error,
        .gamma = result->gamma,
        .palette = pam_duplicate_colormap(result->palette),
    };
    return res;
}

LIQ_EXPORT double liq_get_output_gamma(const liq_result *result)
{
    if (!CHECK_STRUCT_TYPE(result, liq_result)) return -1;

    return result->gamma;
}

static void liq_remapping_result_destroy(liq_remapping_result *result)
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

    memset(&res->int_palette, 0, sizeof(liq_palette));

    if (res->remapping) {
        memset(&res->remapping->int_palette, 0, sizeof(liq_palette));
        liq_remapping_result_destroy(res->remapping);
    }

    pam_freecolormap(res->palette);

    res->magic_header = liq_freed_magic;
    res->free(res);
}

LIQ_EXPORT double liq_get_quantization_error(liq_result *result)
{
    if (!CHECK_STRUCT_TYPE(result, liq_result)) return -1;

    if (result->palette_error >= 0) {
        return result->palette_error*65536.0/6.0;
    }

    if (result->remapping && result->remapping->palette_error >= 0) {
        return result->remapping->palette_error*65536.0/6.0;
    }

    return result->palette_error;
}

LIQ_EXPORT int liq_get_quantization_quality(liq_result *result)
{
    if (!CHECK_STRUCT_TYPE(result, liq_result)) return -1;

    if (result->palette_error >= 0) {
        return mse_to_quality(result->palette_error);
    }

    if (result->remapping && result->remapping->palette_error >= 0) {
        return mse_to_quality(result->remapping->palette_error);
    }

    return result->palette_error;
}

static int compare_popularity(const void *ch1, const void *ch2)
{
    const float v1 = ((const colormap_item*)ch1)->popularity;
    const float v2 = ((const colormap_item*)ch2)->popularity;
    return v1 > v2 ? -1 : 1;
}

#define SWAP_PALETTE(map, a,b) { \
    const colormap_item tmp = (map)->palette[(a)]; \
    (map)->palette[(a)] = (map)->palette[(b)]; \
    (map)->palette[(b)] = tmp; }

static void sort_palette(colormap *map, const liq_attr *options)
{
    /*
    ** Step 3.5 [GRR]: remap the palette colors so that all entries with
    ** the maximal alpha value (i.e., fully opaque) are at the end and can
    ** therefore be omitted from the tRNS chunk.
    */
    if (options->last_index_transparent) {
	for(unsigned int i=0; i < map->colors; i++) {
	    if (map->palette[i].acolor.a < 1.0/256.0) {
		const unsigned int old = i, transparent_dest = map->colors-1;

		SWAP_PALETTE(map, transparent_dest, old);

		/* colors sorted by popularity make pngs slightly more compressible */
		qsort(map->palette, map->colors-1, sizeof(map->palette[0]), compare_popularity);
		return;
            }
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

    if (map->colors > 16) {
        SWAP_PALETTE(map, 7, 1); // slightly improves compression
        SWAP_PALETTE(map, 8, 2);
        SWAP_PALETTE(map, 9, 3);
    }
}

inline static unsigned int posterize_channel(unsigned int color, unsigned int bits)
{
    return (color & ~((1<<bits)-1)) | (color >> (8-bits));
}

static void set_rounded_palette(liq_palette *const dest, colormap *const map, const double gamma, unsigned int posterize)
{
    float gamma_lut[256];
    to_f_set_gamma(gamma_lut, gamma);

    dest->count = map->colors;
    for(unsigned int x = 0; x < map->colors; ++x) {
        rgba_pixel px = to_rgb(gamma, map->palette[x].acolor);

        px.r = posterize_channel(px.r, posterize);
        px.g = posterize_channel(px.g, posterize);
        px.b = posterize_channel(px.b, posterize);
        px.a = posterize_channel(px.a, posterize);

        map->palette[x].acolor = to_f(gamma_lut, px); /* saves rounding error introduced by to_rgb, which makes remapping & dithering more accurate */

        dest->entries[x] = (liq_color){.r=px.r,.g=px.g,.b=px.b,.a=px.a};
    }
}

LIQ_EXPORT const liq_palette *liq_get_palette(liq_result *result)
{
    if (!CHECK_STRUCT_TYPE(result, liq_result)) return NULL;

    if (result->remapping && result->remapping->int_palette.count) {
        return &result->remapping->int_palette;
    }

    if (!result->int_palette.count) {
        set_rounded_palette(&result->int_palette, result->palette, result->gamma, result->min_posterization_output);
    }
    return &result->int_palette;
}

static float remap_to_palette(liq_image *const input_image, unsigned char *const *const output_pixels, colormap *const map, const bool fast)
{
    const int rows = input_image->height;
    const unsigned int cols = input_image->width;
    const float min_opaque_val = input_image->min_opaque_val;
    double remapping_error=0;

    if (!liq_image_get_row_f(input_image, 0)) { // trigger lazy conversion
        return -1;
    }

    struct nearest_map *const n = nearest_init(map, fast);

    const unsigned int max_threads = omp_get_max_threads();
    viter_state average_color[(VITER_CACHE_LINE_GAP+map->colors) * max_threads];
    viter_init(map, max_threads, average_color);

    #pragma omp parallel for if (rows*cols > 3000) \
        schedule(static) default(none) shared(average_color) reduction(+:remapping_error)
    for(int row = 0; row < rows; ++row) {
        const f_pixel *const row_pixels = liq_image_get_row_f(input_image, row);
        unsigned int last_match=0;
        for(unsigned int col = 0; col < cols; ++col) {
            f_pixel px = row_pixels[col];
            float diff;

            output_pixels[row][col] = last_match = nearest_search(n, px, last_match, min_opaque_val, &diff);

            remapping_error += diff;
            viter_update_color(px, 1.0, map, last_match, omp_get_thread_num(), average_color);
        }
    }

    viter_finalize(map, max_threads, average_color);

    nearest_free(n);

    return remapping_error / (input_image->width * input_image->height);
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

    float a = px.a + sa;
         if (a > 1.0) { a = 1.0; }
    else if (a < 0)   { a = 0; }

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
         .a=a,
     };
}

/**
  Uses edge/noise map to apply dithering only to flat areas. Dithering on edges creates jagged lines, and noisy areas are "naturally" dithered.

  If output_image_is_remapped is true, only pixels noticeably changed by error diffusion will be written to output image.
 */
static void remap_to_palette_floyd(liq_image *input_image, unsigned char *const output_pixels[], const colormap *map, const float max_dither_error, const bool use_dither_map, const bool output_image_is_remapped, float base_dithering_level)
{
    const unsigned int rows = input_image->height, cols = input_image->width;
    const unsigned char *dither_map = use_dither_map ? (input_image->dither_map ? input_image->dither_map : input_image->edges) : NULL;
    const float min_opaque_val = input_image->min_opaque_val;

    const colormap_item *acolormap = map->palette;

    struct nearest_map *const n = nearest_init(map, false);

    /* Initialize Floyd-Steinberg error vectors. */
    f_pixel *restrict thiserr, *restrict nexterr;
    thiserr = input_image->malloc((cols + 2) * sizeof(*thiserr) * 2); // +2 saves from checking out of bounds access
    nexterr = thiserr + (cols + 2);
    srand(12345); /* deterministic dithering is better for comparing results */
    if (!thiserr) return;

    for (unsigned int col = 0; col < cols + 2; ++col) {
        const double rand_max = RAND_MAX;
        thiserr[col].r = ((double)rand() - rand_max/2.0)/rand_max/255.0;
        thiserr[col].g = ((double)rand() - rand_max/2.0)/rand_max/255.0;
        thiserr[col].b = ((double)rand() - rand_max/2.0)/rand_max/255.0;
        thiserr[col].a = ((double)rand() - rand_max/2.0)/rand_max/255.0;
    }

    // response to this value is non-linear and without it any value < 0.8 would give almost no dithering
    base_dithering_level = 1.0 - (1.0-base_dithering_level)*(1.0-base_dithering_level)*(1.0-base_dithering_level);

    if (dither_map) {
        base_dithering_level *= 1.0/255.0; // convert byte to float
    }
    base_dithering_level *= 15.0/16.0; // prevent small errors from accumulating

    bool fs_direction = true;
    unsigned int last_match=0;
    for (unsigned int row = 0; row < rows; ++row) {
        memset(nexterr, 0, (cols + 2) * sizeof(*nexterr));

        unsigned int col = (fs_direction) ? 0 : (cols - 1);
        const f_pixel *const row_pixels = liq_image_get_row_f(input_image, row);

        do {
            float dither_level = base_dithering_level;
            if (dither_map) {
                dither_level *= dither_map[row*cols + col];
            }

            const f_pixel spx = get_dithered_pixel(dither_level, max_dither_error, thiserr[col + 1], row_pixels[col]);

            const unsigned int guessed_match = output_image_is_remapped ? output_pixels[row][col] : last_match;
            output_pixels[row][col] = last_match = nearest_search(n, spx, guessed_match, min_opaque_val, NULL);

            const f_pixel xp = acolormap[last_match].acolor;
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

            const float colorimp = (3.0f + acolormap[last_match].acolor.a)/4.0f * dither_level;
            err.r *= colorimp;
            err.g *= colorimp;
            err.b *= colorimp;
            err.a *= dither_level;

            /* Propagate Floyd-Steinberg error terms. */
            if (fs_direction) {
                thiserr[col + 2].a += err.a * (7.f/16.f);
                thiserr[col + 2].r += err.r * (7.f/16.f);
                thiserr[col + 2].g += err.g * (7.f/16.f);
                thiserr[col + 2].b += err.b * (7.f/16.f);

                nexterr[col + 2].a  = err.a * (1.f/16.f);
                nexterr[col + 2].r  = err.r * (1.f/16.f);
                nexterr[col + 2].g  = err.g * (1.f/16.f);
                nexterr[col + 2].b  = err.b * (1.f/16.f);

                nexterr[col + 1].a += err.a * (5.f/16.f);
                nexterr[col + 1].r += err.r * (5.f/16.f);
                nexterr[col + 1].g += err.g * (5.f/16.f);
                nexterr[col + 1].b += err.b * (5.f/16.f);

                nexterr[col    ].a += err.a * (3.f/16.f);
                nexterr[col    ].r += err.r * (3.f/16.f);
                nexterr[col    ].g += err.g * (3.f/16.f);
                nexterr[col    ].b += err.b * (3.f/16.f);

            } else {
                thiserr[col    ].a += err.a * (7.f/16.f);
                thiserr[col    ].r += err.r * (7.f/16.f);
                thiserr[col    ].g += err.g * (7.f/16.f);
                thiserr[col    ].b += err.b * (7.f/16.f);

                nexterr[col    ].a  = err.a * (1.f/16.f);
                nexterr[col    ].r  = err.r * (1.f/16.f);
                nexterr[col    ].g  = err.g * (1.f/16.f);
                nexterr[col    ].b  = err.b * (1.f/16.f);

                nexterr[col + 1].a += err.a * (5.f/16.f);
                nexterr[col + 1].r += err.r * (5.f/16.f);
                nexterr[col + 1].g += err.g * (5.f/16.f);
                nexterr[col + 1].b += err.b * (5.f/16.f);

                nexterr[col + 2].a += err.a * (3.f/16.f);
                nexterr[col + 2].r += err.r * (3.f/16.f);
                nexterr[col + 2].g += err.g * (3.f/16.f);
                nexterr[col + 2].b += err.b * (3.f/16.f);
            }

            // remapping is done in zig-zag
            if (fs_direction) {
                ++col;
                if (col >= cols) break;
            } else {
                if (col <= 0) break;
                --col;
            }
        } while(1);

        f_pixel *const temperr = thiserr;
        thiserr = nexterr;
        nexterr = temperr;
        fs_direction = !fs_direction;
    }

    input_image->free(MIN(thiserr, nexterr)); // MIN because pointers were swapped
    nearest_free(n);
}


/* histogram contains information how many times each color is present in the image, weighted by importance_map */
static histogram *get_histogram(liq_image *input_image, const liq_attr *options)
{
    unsigned int ignorebits=MAX(options->min_posterization_output, options->min_posterization_input);
    const unsigned int cols = input_image->width, rows = input_image->height;

    if (!input_image->noise && options->use_contrast_maps) {
        contrast_maps(input_image);
    }

   /*
    ** Step 2: attempt to make a histogram of the colors, unclustered.
    ** If at first we don't succeed, increase ignorebits to increase color
    ** coherence and try again.
    */

    unsigned int maxcolors = options->max_histogram_entries;

    struct acolorhash_table *acht;
    const bool all_rows_at_once = liq_image_can_use_rows(input_image);
    do {
        acht = pam_allocacolorhash(maxcolors, rows*cols, ignorebits, options->malloc, options->free);
        if (!acht) return NULL;

        // histogram uses noise contrast map for importance. Color accuracy in noisy areas is not very important.
        // noise map does not include edges to avoid ruining anti-aliasing
        for(unsigned int row=0; row < rows; row++) {
            bool added_ok;
            if (all_rows_at_once) {
                added_ok = pam_computeacolorhash(acht, (const rgba_pixel *const *)input_image->rows, cols, rows, input_image->noise);
                if (added_ok) break;
            } else {
                const rgba_pixel* rows_p[1] = { liq_image_get_row_rgba(input_image, row) };
                added_ok = pam_computeacolorhash(acht, rows_p, cols, 1, input_image->noise ? &input_image->noise[row * cols] : NULL);
            }
            if (!added_ok) {
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

    if (input_image->free_pixels && input_image->f_pixels) {
        liq_image_free_rgba_source(input_image); // bow can free the RGBA source if copy has been made in f_pixels
    }

    histogram *hist = pam_acolorhashtoacolorhist(acht, input_image->gamma, options->malloc, options->free);
    pam_freeacolorhash(acht);

    if (hist) {
        liq_verbose_printf(options, "  made histogram...%d colors found", hist->size);
    }
    return hist;
}

static void modify_alpha(liq_image *input_image, rgba_pixel *const row_pixels)
{
    /* IE6 makes colors with even slightest transparency completely transparent,
       thus to improve situation in IE, make colors that are less than ~10% transparent
       completely opaque */

    const float min_opaque_val = input_image->min_opaque_val;
    const float almost_opaque_val = min_opaque_val * 169.f/256.f;
    const unsigned int almost_opaque_val_int = (min_opaque_val * 169.f/256.f)*255.f;

    for(unsigned int col = 0; col < input_image->width; col++) {
        const rgba_pixel px = row_pixels[col];

        /* ie bug: to avoid visible step caused by forced opaqueness, linearily raise opaqueness of almost-opaque colors */
        if (px.a >= almost_opaque_val_int) {
            float al = px.a / 255.f;
            al = almost_opaque_val + (al-almost_opaque_val) * (1.f-almost_opaque_val) / (min_opaque_val-almost_opaque_val);
            al *= 256.f;
            row_pixels[col].a = al >= 255.f ? 255 : al;
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
    if (cols < 4 || rows < 4 || (3*cols*rows) > LIQ_HIGH_MEMORY_LIMIT) {
        return;
    }

    unsigned char *restrict noise = image->malloc(cols*rows);
    unsigned char *restrict edges = image->malloc(cols*rows);
    unsigned char *restrict tmp = image->malloc(cols*rows);

    if (!noise || !edges || !tmp) {
        return;
    }

    const f_pixel *curr_row, *prev_row, *next_row;
    curr_row = prev_row = next_row = liq_image_get_row_f(image, 0);

    for (int j=0; j < rows; j++) {
        prev_row = curr_row;
        curr_row = next_row;
        next_row = liq_image_get_row_f(image, MIN(rows-1,j+1));

        f_pixel prev, curr = curr_row[0], next=curr;
        for (int i=0; i < cols; i++) {
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

            z *= 256.f;
            noise[j*cols+i] = z < 256 ? z : 255;
            z = (1.f-edge)*256.f;
            edges[j*cols+i] = z < 256 ? z : 255;
        }
    }

    // noise areas are shrunk and then expanded to remove thin edges from the map
    liq_max3(noise, tmp, cols, rows);
    liq_max3(tmp, noise, cols, rows);

    liq_blur(noise, tmp, noise, cols, rows, 3);

    liq_max3(noise, tmp, cols, rows);

    liq_min3(tmp, noise, cols, rows);
    liq_min3(noise, tmp, cols, rows);
    liq_min3(tmp, noise, cols, rows);

    liq_min3(edges, tmp, cols, rows);
    liq_max3(tmp, edges, cols, rows);
    for(int i=0; i < cols*rows; i++) edges[i] = MIN(noise[i], edges[i]);

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
    unsigned char *const edges = input_image->edges;

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
                    float e = edges[row*width + lastcol] / 255.f;
                    e *= 1.f - 2.5f/neighbor_count;
                    edges[row*width + lastcol++] = e * 255.f;
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
    // if output is posterized it doesn't make sense to aim for perfrect colors, so increase target_mse
    // at this point actual gamma is not set, so very conservative posterization estimate is used
    const double target_mse = MAX(options->target_mse, pow((1<<options->min_posterization_output)/1024.0, 2));
    int feedback_loop_trials = options->feedback_loop_trials;
    colormap *acolormap = NULL;
    double least_error = MAX_DIFF;
    double target_mse_overshoot = feedback_loop_trials>0 ? 1.05 : 1.0;
    const double percent = (double)(feedback_loop_trials>0?feedback_loop_trials:1)/100.0;

    do {
        colormap *newmap = mediancut(hist, options->min_opaque_val, max_colors,
            target_mse * target_mse_overshoot, MAX(MAX(90.0/65536.0, target_mse), least_error)*1.2,
            options->malloc, options->free);
        if (!newmap) {
            return NULL;
        }

        if (feedback_loop_trials <= 0) {
            return newmap;
        }

        // after palette has been created, total error (MSE) is calculated to keep the best palette
        // at the same time Voronoi iteration is done to improve the palette
        // and histogram weights are adjusted based on remapping error to give more weight to poorly matched colors

        const bool first_run_of_target_mse = !acolormap && target_mse > 0;
        double total_error = viter_do_iteration(hist, newmap, options->min_opaque_val, first_run_of_target_mse ? NULL : adjust_histogram_callback, !acolormap || options->fast_palette);

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

    // likely_colormap_index (used and set in viter_do_iteration) can't point to index outside colormap
    if (acolormap->colors < 256) {
	for(unsigned int j=0; j < hist->size; j++) {
	    if (hist->achv[j].likely_colormap_index >= acolormap->colors) {
		hist->achv[j].likely_colormap_index = 0; // actual value doesn't matter, as the guess is out of date anyway
	    }
	}
    }
    *palette_error_p = least_error;
    return acolormap;
}

static liq_result *pngquant_quantize(histogram *hist, const liq_attr *options, const double gamma)
{
    colormap *acolormap;
    double palette_error = -1;

    // no point having perfect match with imperfect colors (ignorebits > 0)
    const bool fast_palette = options->fast_palette || hist->ignorebits > 0;

    // If image has few colors to begin with (and no quality degradation is required)
    // then it's possible to skip quantization entirely
    if (hist->size <= options->max_colors && options->target_mse == 0) {
        acolormap = pam_colormap(hist->size, options->malloc, options->free);
        for(unsigned int i=0; i < hist->size; i++) {
            acolormap->palette[i].acolor = hist->achv[i].acolor;
            acolormap->palette[i].popularity = hist->achv[i].perceptual_weight;
        }
        palette_error = 0;
    } else {
        acolormap = find_best_palette(hist, options, &palette_error);
        if (!acolormap) {
            return NULL;
        }

        // Voronoi iteration approaches local minimum for the palette
        const double max_mse = options->max_mse;
        const double iteration_limit = options->voronoi_iteration_limit;
        unsigned int iterations = options->voronoi_iterations;

        if (!iterations && palette_error < 0 && max_mse < MAX_DIFF) iterations = 1; // otherwise total error is never calculated and MSE limit won't work

        if (iterations) {
            verbose_print(options, "  moving colormap towards local minimum");

            double previous_palette_error = MAX_DIFF;

            for(unsigned int i=0; i < iterations; i++) {
                palette_error = viter_do_iteration(hist, acolormap, options->min_opaque_val, NULL, i==0 || options->fast_palette);

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
            liq_verbose_printf(options, "  image degradation MSE=%.3f (Q=%d) exceeded limit of %.3f (%d)",
                               palette_error*65536.0/6.0, mse_to_quality(palette_error),
                               max_mse*65536.0/6.0, mse_to_quality(max_mse));
            pam_freecolormap(acolormap);
            return NULL;
        }
    }

    sort_palette(acolormap, options);

    liq_result *result = options->malloc(sizeof(liq_result));
    if (!result) return NULL;
    *result = (liq_result){
        .magic_header = liq_result_magic,
        .malloc = options->malloc,
        .free = options->free,
        .palette = acolormap,
        .palette_error = palette_error,
        .fast_palette = fast_palette,
        .use_dither_map = options->use_dither_map,
        .gamma = gamma,
        .min_posterization_output = options->min_posterization_output,
    };
    return result;
}

LIQ_EXPORT liq_error liq_write_remapped_image(liq_result *result, liq_image *input_image, void *buffer, size_t buffer_size)
{
    if (!CHECK_STRUCT_TYPE(result, liq_result)) {
        return LIQ_INVALID_POINTER;
    }
    if (!CHECK_STRUCT_TYPE(input_image, liq_image)) {
        return LIQ_INVALID_POINTER;
    }
    if (!CHECK_USER_POINTER(buffer)) {
        return LIQ_INVALID_POINTER;
    }

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

LIQ_EXPORT liq_error liq_write_remapped_image_rows(liq_result *quant, liq_image *input_image, unsigned char **row_pointers)
{
    if (!CHECK_STRUCT_TYPE(quant, liq_result)) return LIQ_INVALID_POINTER;
    if (!CHECK_STRUCT_TYPE(input_image, liq_image)) return LIQ_INVALID_POINTER;
    for(unsigned int i=0; i < input_image->height; i++) {
        if (!CHECK_USER_POINTER(row_pointers+i) || !CHECK_USER_POINTER(row_pointers[i])) return LIQ_INVALID_POINTER;
    }

    if (quant->remapping) {
        liq_remapping_result_destroy(quant->remapping);
    }
    liq_remapping_result *const result = quant->remapping = liq_remapping_result_create(quant);
    if (!result) return LIQ_OUT_OF_MEMORY;

    if (!input_image->edges && !input_image->dither_map && quant->use_dither_map) {
        contrast_maps(input_image);
    }

    /*
     ** Step 4: map the colors in the image to their closest match in the
     ** new colormap, and write 'em out.
     */

    float remapping_error = result->palette_error;
    if (result->dither_level == 0) {
        set_rounded_palette(&result->int_palette, result->palette, result->gamma, quant->min_posterization_output);
        remapping_error = remap_to_palette(input_image, row_pointers, result->palette, quant->fast_palette);
    } else {
        const bool generate_dither_map = result->use_dither_map && (input_image->edges && !input_image->dither_map);
        if (generate_dither_map) {
            // If dithering (with dither map) is required, this image is used to find areas that require dithering
            remapping_error = remap_to_palette(input_image, row_pointers, result->palette, quant->fast_palette);
            update_dither_map(row_pointers, input_image);
        }

        // remapping above was the last chance to do voronoi iteration, hence the final palette is set after remapping
        set_rounded_palette(&result->int_palette, result->palette, result->gamma, quant->min_posterization_output);

        remap_to_palette_floyd(input_image, row_pointers, result->palette,
            MAX(remapping_error*2.4, 16.f/256.f), result->use_dither_map, generate_dither_map, result->dither_level);
    }

    // remapping error from dithered image is absurd, so always non-dithered value is used
    // palette_error includes some perceptual weighting from histogram which is closer correlated with dssim
    // so that should be used when possible.
    if (result->palette_error < 0) {
        result->palette_error = remapping_error;
    }

    return LIQ_OK;
}
