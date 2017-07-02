#undef NDEBUG
#include <assert.h>
#include "libimagequant.h"
#include <stdio.h>
#include <string.h>

static char magic[] = "magic";

static void test_log_callback_function(const liq_attr *at, const char *message, void* user_info) {
    assert(user_info == magic);
    assert(message);
    assert(strlen(message));
}

static int test_abort_callback(float progress_percent, void* user_info) {
    assert(user_info == magic);
    return 0;
}

static int progress_called = 0;
static int test_continue_callback(float progress_percent, void* user_info) {
    assert(user_info == magic);
    progress_called++;
    return 1;
}

static void test_abort() {
    liq_attr *attr = liq_attr_create();

    unsigned char dummy[4] = {0};
    liq_image *img = liq_image_create_rgba(attr, dummy, 1, 1, 0);

    liq_attr_set_progress_callback(attr, test_abort_callback, magic);

    liq_result *res = liq_quantize_image(attr, img);
    assert(!res);

    liq_attr_destroy(attr);
}

static void test_zero_histogram() {
    liq_attr *attr = liq_attr_create();
    liq_histogram *hist = liq_histogram_create(attr);
    assert(hist);

    liq_result *res;
    liq_error err = liq_histogram_quantize(hist, attr, &res);
    assert(!res);
    assert(err);

    liq_attr_destroy(attr);
    liq_histogram_destroy(hist);
}

static void test_histogram() {
    liq_attr *attr = liq_attr_create();
    liq_histogram *hist = liq_histogram_create(attr);
    assert(hist);

    const unsigned char dummy1[4] = {255,0,255,255};
    liq_image *const img1 = liq_image_create_rgba(attr, dummy1, 1, 1, 0);
    assert(img1);

    const liq_error err1 = liq_histogram_add_image(hist, attr, img1);
    assert(LIQ_OK == err1);

    const unsigned char dummy2[4] = {0,0,0,0};
    liq_image *const img2 = liq_image_create_rgba(attr, dummy2, 1, 1, 0);
    assert(img2);
    liq_image_add_fixed_color(img2, (liq_color){255,255,255,255});


    const liq_error err2 = liq_histogram_add_image(hist, attr, img2);
    assert(LIQ_OK == err2);

    liq_image_destroy(img1);
    liq_image_destroy(img2);

    liq_result *res;
    liq_error err = liq_histogram_quantize(hist, attr, &res);
    assert(LIQ_OK == err);
    assert(res);

    liq_attr_destroy(attr);

    liq_histogram_destroy(hist);

    const liq_palette *pal = liq_get_palette(res);
    assert(pal);
    assert(pal->count == 3);

    liq_result_destroy(res);
}

static void test_fixed_colors() {
    liq_attr *attr = liq_attr_create();

    liq_attr_set_progress_callback(attr, test_continue_callback, magic);
    liq_set_log_callback(attr, test_log_callback_function, magic);

    unsigned char dummy[4] = {0};
    liq_image *img = liq_image_create_rgba(attr, dummy, 1, 1, 0);
    assert(img);

    liq_image_add_fixed_color(img, (liq_color){0,0,0,0});

    liq_result *res = liq_quantize_image(attr, img);
    assert(res);
    assert(progress_called);

    const liq_palette *pal = liq_get_palette(res);
    assert(pal);
    assert(pal->count == 1);

    liq_result_destroy(res);
    liq_image_destroy(img);
    liq_attr_destroy(attr);
}

static void test_fixed_colors_order() {
    liq_attr *attr = liq_attr_create();

    unsigned char dummy[4] = {0};
    liq_image *img = liq_image_create_rgba(attr, dummy, 1, 1, 0);
    assert(img);

    liq_color colors[17] = {
        {0,0,0,0}, {1,1,1,1}, {2,2,2,2}, {3,3,3,3}, {4,4,4,4}, {5,4,4,4},
        {6,4,4,4}, {6,7,4,4}, {6,7,8,4}, {6,7,8,9}, {10,7,8,9}, {10,11,8,9},
        {10,11,12,9}, {10,11,12,13}, {14,11,12,13}, {14,15,12,13}, {14,15,16,13},
    };

    for(int i=0; i < 17; i++) {
        liq_image_add_fixed_color(img, colors[i]);
    }

    liq_result *res = liq_quantize_image(attr, img);
    assert(res);

    const liq_palette *pal = liq_get_palette(res);
    assert(pal);
    assert(pal->count == 17);

    for(int i=0; i < 17; i++) {
        assert(pal->entries[i].r == colors[i].r);
        assert(pal->entries[i].g == colors[i].g);
        assert(pal->entries[i].b == colors[i].b);
        assert(pal->entries[i].a == colors[i].a);
    }

    liq_set_dithering_level(res, 1.0);

    char buf[1];
    assert(LIQ_OK == liq_write_remapped_image(res, img, buf, 1));

    liq_result_set_progress_callback(res, test_abort_callback, magic);
    assert(LIQ_ABORTED == liq_write_remapped_image(res, img, buf, 1));

    liq_result_destroy(res);
    liq_image_destroy(img);
    liq_attr_destroy(attr);
}

int main(void) {
    test_fixed_colors();
    test_fixed_colors_order();
    test_abort();
    test_histogram();
    test_zero_histogram();
    assert(printf("OK\n"));
    return 0;
}
