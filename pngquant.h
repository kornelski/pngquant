//
// http://pngquant.org
//

#ifndef LIBIMAGEQUANT_H
#define LIBIMAGEQUANT_H

#ifndef LIQ_EXPORT
#define LIQ_EXPORT extern
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct liq_attr liq_attr;
typedef struct liq_image liq_image;

typedef enum liq_error {
    LIQ_OK = 0,
    LIQ_VALUE_OUT_OF_RANGE = 100,
    LIQ_OUT_OF_MEMORY,
    LIQ_NOT_READY,
    LIQ_BITMAP_NOT_AVAILABLE,
    LIQ_BUFFER_TOO_SMALL,
} liq_error;

LIQ_EXPORT liq_error liq_set_max_colors(liq_attr* attr, int colors);
LIQ_EXPORT liq_error liq_set_speed(liq_attr* attr, int speed);
LIQ_EXPORT liq_error liq_set_min_opacity(liq_attr* attr, int min);
LIQ_EXPORT liq_error liq_set_quality(liq_attr* attr, int target, int minimum);

typedef void liq_log_callback_function(const liq_attr*, const char *message, void* user_info);
typedef void liq_log_flush_callback_function(const liq_attr*, void* user_info);
LIQ_EXPORT void liq_set_log_callback(liq_attr*, liq_log_callback_function*, void* user_info);
LIQ_EXPORT void liq_set_log_flush_callback(liq_attr*, liq_log_flush_callback_function*, void* user_info);

void liq_image_destroy(liq_image *img);

#ifdef __cplusplus
}
#endif

#endif
