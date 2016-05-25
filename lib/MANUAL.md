# libimagequant—Image Quantization Library

Small, portable C library for high-quality conversion of RGBA images to 8-bit indexed-color (palette) images.
It's powering [pngquant2](https://pngquant.org).

## License

Libimagequant is dual-licensed:

* For Free/Libre Open Source Software it's available under [GPL v3 or later](https://raw.github.com/pornel/pngquant/master/lib/COPYRIGHT) with additional copyright notices for older parts of the code.

* For use in non-GPL software (e.g. closed-source or App Store distribution) please ask kornel@pngquant.org for a commercial license.

## Download

The [library](https://pngquant.org/lib) is currently a part of the [pngquant2 project](https://github.com/pornel/pngquant/tree/master/lib).

Files needed for the library are only in the `lib/` directory inside the repository (and you can ignore the rest).

## Compiling and Linking

The library can be linked with ANSI C, C++ and [Rust](https://crates.io/crates/imagequant/) programs. It has no external dependencies.

To build on Unix-like systems run:

    make -C lib

it will create `lib/libimagequant.a` which you can link with your program.

    gcc yourprogram.c /path/to/lib/libimagequant.a

On BSD, use `gmake` (GNU make) rather than the native `make`.

Alternatively you can compile the library with your program simply by including all `.c` files (and define `NDEBUG` to get a fast version):

    gcc -std=c99 -O3 -DNDEBUG lib/*.c yourprogram.c

In [Rust](https://github.com/pornel/libimagequant-rust),
if using Cargo, add `imagequant` to dependencies.

### Compiling on Windows/Visual Studio

The library can be compiled with any C compiler that has at least basic support for C99 (GCC, clang, ICC, C++ Builder, even Tiny C Compiler), but Visual Studio 2012 and older are not up to date with the 1999 C standard. There are 2 options for using `libimagequant` on Windows:

 * Use Visual Studio **2013** (MSVC 18) and an [MSVC-compatible branch of the library](https://github.com/pornel/pngquant/tree/msvc/lib)
 * Or use GCC from [MinGW](http://www.mingw.org). Use GCC to build `libimagequant.a` (using the instructions above for Unix) and add it along with `libgcc.a` (shipped with the MinGW compiler) to your VC project.

## Overview

The basic flow is:

1. Create attributes object and configure the library.
2. Create image object from RGBA pixels or data source.
3. Perform quantization (generate palette).
4. Store remapped image and final palette.
5. Free memory.

Please note that libimagequant only handles raw uncompressed arrays of pixels in memory and is completely independent of any file format.

<p>

    #include "lib/libimagequant.h"

    liq_attr *attr = liq_attr_create();
    liq_image *image = liq_image_create_rgba(attr, example_bitmap_rgba, width, height, 0);
    liq_result *res = liq_quantize_image(attr, image);

    liq_write_remapped_image(res, image, example_bitmap_8bpp, example_bitmap_size);
    const liq_palette *pal = liq_get_palette(res);

    // Save the image and the palette now.
    for(int i=0; i < pal->count; i++) {
        example_copy_palette_entry(pal->entries[i]);
    }
    // You'll need a PNG library to write to a file.
    example_write_image(example_bitmap_8bpp);

    liq_result_destroy(res);
    liq_image_destroy(image);
    liq_attr_destroy(attr);

Functions returning `liq_error` return `LIQ_OK` (`0`) on success and non-zero on error.

It's safe to pass `NULL` to any function accepting `liq_attr`, `liq_image`, `liq_result` (in that case the error code `LIQ_INVALID_POINTER` will be returned). These objects can be reused multiple times.

There are 3 ways to create image object for quantization:

  * `liq_image_create_rgba()` for simple, contiguous RGBA pixel arrays (width×height×4 bytes large bitmap).
  * `liq_image_create_rgba_rows()` for non-contiguous RGBA pixel arrays (that have padding between rows or reverse order, e.g. BMP).
  * `liq_image_create_custom()` for RGB, ABGR, YUV and all other formats that can be converted on-the-fly to RGBA (you have to supply the conversion function).

Note that "image" here means raw uncompressed pixels. If you have a compressed image file, such as PNG, you must use another library (e.g. libpng or lodepng) to decode it first.

## Functions

----

    liq_attr* liq_attr_create(void);

Returns object that will hold initial settings (attributes) for the library. The object should be freed using `liq_attr_destroy()` after it's no longer needed.

Returns `NULL` in the unlikely case that the library cannot run on the current machine (e.g. the library has been compiled for SSE-capable x86 CPU and run on VIA C3 CPU).

----

    liq_error liq_set_max_colors(liq_attr* attr, int colors);

Specifies maximum number of colors to use. The default is 256. Instead of setting a fixed limit it's better to use `liq_set_quality()`.

Returns `LIQ_VALUE_OUT_OF_RANGE` if number of colors is outside the range 2-256.

----

    int liq_get_max_colors(liq_attr* attr);

Returns the value set by `liq_set_max_colors()`.

----

    liq_error liq_set_quality(liq_attr* attr, int minimum, int maximum);

Quality is in range `0` (worst) to `100` (best) and values are analoguous to JPEG quality (i.e. `80` is usually good enough).

Quantization will attempt to use the lowest number of colors needed to achieve `maximum` quality. `maximum` value of `100` is the default and means conversion as good as possible.

If it's not possible to convert the image with at least `minimum` quality (i.e. 256 colors is not enough to meet the minimum quality), then `liq_quantize_image()` will fail. The default minumum is `0` (proceeds regardless of quality).

Quality measures how well the generated palette fits image given to `liq_quantize_image()`. If a different image is remapped with `liq_write_remapped_image()` then actual quality may be different.

Regardless of the quality settings the number of colors won't exceed the maximum (see `liq_set_max_colors()`).

Returns `LIQ_VALUE_OUT_OF_RANGE` if target is lower than minimum or any of them is outside the 0-100 range.
Returns `LIQ_INVALID_POINTER` if `attr` appears to be invalid.

    liq_attr *attr = liq_attr_create();
    liq_set_quality(attr, 50, 80); // use quality 80 if possible. Give up if quality drops below 50.

----

    int liq_get_min_quality(liq_attr* attr);

Returns the lower bound set by `liq_set_quality()`.

----

    int liq_get_max_quality(liq_attr* attr);

Returns the upper bound set by `liq_set_quality()`.

----

    liq_image *liq_image_create_rgba(liq_attr *attr, void* pixels, int width, int height, double gamma);

Creates an object that represents the image pixels later used for quantization and remapping. The pixel array must be contiguous run of RGBA pixels (alpha is the last component, 0 = transparent, 255 = opaque).

The pixel array must not be modified or freed until this object is freed with `liq_image_destroy()`. See also `liq_image_set_memory_ownership()`.

`width` and `height` are dimensions in pixels. An image 10x10 pixel large will need 400-byte array.

`gamma` can be `0` for images with the typical 1/2.2 [gamma](https://en.wikipedia.org/wiki/Gamma_correction).
Otherwise `gamma` must be > 0 and < 1, e.g. `0.45455` (1/2.2) or `0.55555` (1/1.8). Generated palette will use the same gamma unless `liq_set_output_gamma()` is used. If `liq_set_output_gamma` is not used, then it only affects whether brighter or darker areas of the image will get more palette colors allocated.

Returns `NULL` on failure, e.g. if `pixels` is `NULL` or `width`/`height` is <= 0.

----

    liq_image *liq_image_create_rgba_rows(liq_attr *attr, void* rows[], int width, int height, double gamma);

Same as `liq_image_create_rgba()`, but takes an array of pointers to rows of pixels. This allows defining images with reversed rows (like in BMP), "stride" different than width or using only fragment of a larger bitmap, etc.

The `rows` array must have at least `height` elements, and each row must be at least `width` RGBA pixels wide.

    unsigned char *bitmap = …;
    void *rows = malloc(height * sizeof(void*));
    int bytes_per_row = width * 4 + padding; // stride
    for(int i=0; i < height; i++) {
        rows[i] = bitmap + i * bytes_per_row;
    }
    liq_image *img = liq_image_create_rgba_rows(attr, rows, width, height, 0);
    // …
    liq_image_destroy(img);
    free(rows);

The row pointers and pixels must not be modified or freed until this object is freed with `liq_image_destroy()` (you can change that with `liq_image_set_memory_ownership()`).

See also `liq_image_create_rgba()` and `liq_image_create_custom()`.

----

    liq_result *liq_quantize_image(liq_attr *attr, liq_image *input_image);

Performs quantization (palette generation) based on settings in `attr` and pixels of the image.

Returns `NULL` if quantization fails, e.g. due to limit set in `liq_set_quality()`.

See `liq_write_remapped_image()`.

----

    liq_error liq_set_dithering_level(liq_result *res, float dither_level);

Enables/disables dithering in `liq_write_remapped_image()`. Dithering level must be between `0` and `1` (inclusive). Dithering level `0` enables fast non-dithered remapping. Otherwise a variation of Floyd-Steinberg error diffusion is used.

Precision of the dithering algorithm depends on the speed setting, see `liq_set_speed()`.

Returns `LIQ_VALUE_OUT_OF_RANGE` if the dithering level is outside the 0-1 range.

----

    liq_error liq_write_remapped_image(liq_result *result, liq_image *input_image, void *buffer, size_t buffer_size);

Remaps the image to palette and writes its pixels to the given buffer, 1 pixel per byte.

The buffer must be large enough to fit the entire image, i.e. width×height bytes large. For safety, pass the size of the buffer as `buffer_size`.

For best performance call `liq_get_palette()` *after* this function, as palette is improved during remapping.

Returns `LIQ_BUFFER_TOO_SMALL` if given size of the buffer is not enough to fit the entire image.

    int buffer_size = width*height;
    char *buffer = malloc(buffer_size);
    if (LIQ_OK == liq_write_remapped_image(result, input_image, buffer, buffer_size)) {
        liq_palette *pal = liq_get_palette(result);
        // save image
    }

See `liq_get_palette()`.

The buffer is assumed to be contiguous, with rows ordered from top to bottom, and no gaps between rows. If you need to write rows with padding or upside-down order, then use `liq_write_remapped_image_rows()`.

Please note that it only writes raw uncompressed pixels to memory. It does not perform any PNG compression. If you'd like to create a PNG file then you need to pass the raw pixel data to another library, e.g. libpng or lodepng. See `rwpng.c` in `pngquant` project for an example how to do that.

----

    const liq_palette *liq_get_palette(liq_result *result);

Returns pointer to palette optimized for image that has been quantized or remapped (final refinements are applied to the palette during remapping).

It's valid to call this method before remapping, if you don't plan to remap any images or want to use same palette for multiple images.

`liq_palette->count` contains number of colors (up to 256), `liq_palette->entries[n]` contains RGBA value for nth palette color.

The palette is **temporary and read-only**. You must copy the palette elsewhere *before* calling `liq_result_destroy()`.

Returns `NULL` on error.

----

    void liq_attr_destroy(liq_attr *);
    void liq_image_destroy(liq_image *);
    void liq_result_destroy(liq_result *);

Releases memory owned by the given object. Object must not be used any more after it has been freed.

Freeing `liq_result` also frees any `liq_palette` obtained from it.

## Advanced Functions

----

    liq_error liq_set_speed(liq_attr* attr, int speed);

Higher speed levels disable expensive algorithms and reduce quantization precision. The default speed is `3`. Speed `1` gives marginally better quality at significant CPU cost. Speed `10` has usually 5% lower quality, but is 8 times faster than the default.

High speeds combined with `liq_set_quality()` will use more colors than necessary and will be less likely to meet minimum required quality.

<table><caption>Features dependent on speed</caption>
<tr><th>Noise-sensitive dithering</th><td>speed 1 to 5</td></tr>
<tr><th>Forced posterization</th><td>8-10 or if image has more than million colors</td></tr>
<tr><th>Quantization error known</th><td>1-7 or if minimum quality is set</td></tr>
<tr><th>Additional quantization techniques</th><td>1-6</td></tr>
</table>

Returns `LIQ_VALUE_OUT_OF_RANGE` if the speed is outside the 1-10 range.

----

    int liq_get_speed(liq_attr* attr);

Returns the value set by `liq_set_speed()`.

----

    liq_error liq_set_min_opacity(liq_attr* attr, int min);

Alpha values higher than this will be rounded to opaque. This is a workaround for Internet Explorer 6, but because this browser is not used any more, this option is deprecated and will be removed. The default is `255` (no change).

Returns `LIQ_VALUE_OUT_OF_RANGE` if the value is outside the 0-255 range.

----

    int liq_get_min_opacity(liq_attr* attr);

Returns the value set by `liq_set_min_opacity()`.

----

    liq_set_min_posterization(liq_attr* attr, int bits);

Ignores given number of least significant bits in all channels, posterizing image to `2^bits` levels. `0` gives full quality. Use `2` for VGA or 16-bit RGB565 displays, `4` if image is going to be output on a RGB444/RGBA4444 display (e.g. low-quality textures on Android).

Returns `LIQ_VALUE_OUT_OF_RANGE` if the value is outside the 0-4 range.

----

    int liq_get_min_posterization(liq_attr* attr);

Returns the value set by `liq_set_min_posterization()`.

----

    liq_set_last_index_transparent(liq_attr* attr, int is_last);

`0` (default) makes alpha colors sorted before opaque colors. Non-`0` mixes colors together except completely transparent color, which is moved to the end of the palette. This is a workaround for programs that blindly assume the last palette entry is transparent.

----

    liq_image *liq_image_create_custom(liq_attr *attr, liq_image_get_rgba_row_callback *row_callback, void *user_info, int width, int height, double gamma);

<p>

    void image_get_rgba_row_callback(liq_color row_out[], int row_index, int width, void *user_info) {
        for(int column_index=0; column_index < width; column_index++) {
            row_out[column_index] = /* generate pixel at (row_index, column_index) */;
        }
    }

Creates image object that will use callback to read image data. This allows on-the-fly conversion of images that are not in the RGBA color space.

`user_info` value will be passed to the callback. It may be useful for storing pointer to program's internal representation of the image.

The callback must read/generate `row_index`-th row and write its RGBA pixels to the `row_out` array. Row `width` is given for convenience and will always equal to image width.

The callback will be called multiple times for each row. Quantization and remapping require at least two full passes over image data, so caching of callback's work makes no sense — in such case it's better to convert entire image and use `liq_image_create_rgba()` instead.

To use RGB image:

    void rgb_to_rgba_callback(liq_color row_out[], int row_index, int width, void *user_info) {
        unsigned char *rgb_row = ((unsigned char *)user_info) + 3*width*row_index;

        for(int i=0; i < width; i++) {
            row_out[i].r = rgb_row[i*3];
            row_out[i].g = rgb_row[i*3+1];
            row_out[i].b = rgb_row[i*3+2];
            row_out[i].a = 255;
        }
    }
    liq_image *img = liq_image_create_custom(attr, rgb_to_rgba_callback, rgb_bitmap, width, height, 0);

The library doesn't support RGB bitmaps "natively", because supporting only single format allows compiler to inline more code, 4-byte pixel alignment is faster, and SSE instructions operate on 4 values at once, so alpha support is almost free.

----

    liq_error liq_image_set_memory_ownership(liq_image *image, int ownership_flags);

Passes ownership of image pixel data and/or its rows array to the `liq_image` object, so you don't have to free it yourself. Memory owned by the object will be freed at its discretion with `free` function specified in `liq_attr_create_with_allocator()` (by default it's stdlib's `free()`).

* `LIQ_OWN_PIXELS` makes pixel array owned by the object. The pixels will be freed automatically at any point when it's no longer needed. If you set this flag you must **not** free the pixel array yourself. If the image has been created with `liq_image_create_rgba_rows()` then the starting address of the array of pixels is assumed to be the lowest address of any row.

* `LIQ_OWN_ROWS` makes array of row pointers (but not the pixels pointed by these rows) owned by the object. Rows will be freed when object is deallocated. If you set this flag you must **not** free the rows array yourself. This flag is valid only if the object has been created with `liq_image_create_rgba_rows()`.

These flags can be combined with binary *or*, i.e. `LIQ_OWN_PIXELS | LIQ_OWN_ROWS`.

This function must not be used if the image has been created with `liq_image_create_custom()`.

Returns `LIQ_VALUE_OUT_OF_RANGE` if invalid flags are specified or the image object only takes pixels from a callback.

----

    liq_error liq_write_remapped_image_rows(liq_result *result, liq_image *input_image, unsigned char **row_pointers);

Similar to `liq_write_remapped_image()`. Writes remapped image, at 1 byte per pixel, to each row pointed by `row_pointers` array. The array must have at least as many elements as height of the image, and each row must have at least as many bytes as width of the image. Rows must not overlap.

For best performance call `liq_get_palette()` *after* this function, as remapping may change the palette.

Returns `LIQ_INVALID_POINTER` if `result` or `input_image` is `NULL`.

----

    double liq_get_quantization_error(liq_result *result);

Returns mean square error of quantization (square of difference between pixel values in the source image and its remapped version). Alpha channel, gamma correction and approximate importance of pixels is taken into account, so the result isn't exactly the mean square error of all channels.

For most images MSE 1-5 is excellent. 7-10 is OK. 20-30 will have noticeable errors. 100 is awful.

This function may return `-1` if the value is not available (this happens when a high speed has been requested, the image hasn't been remapped yet, and quality limit hasn't been set, see `liq_set_speed()` and `liq_set_quality()`). The value is not updated when multiple images are remapped, it applies only to the image used in `liq_quantize_image()` or the first image that has been remapped. See `liq_get_remapping_error()`.

----

    double liq_get_remapping_error(liq_result *result);

Returns mean square error of last remapping done (square of difference between pixel values in the remapped image and its remapped version). Alpha channel and gamma correction are taken into account, so the result isn't exactly the mean square error of all channels.

This function may return `-1` if the value is not available (this happens when a high speed has been requested or the image hasn't been remapped yet).

----

    double liq_get_quantization_quality(liq_result *result);

Analoguous to `liq_get_quantization_error()`, but returns quantization error as quality value in the same 0-100 range that is used by `liq_set_quality()`.

It may return `-1` if the value is not available (see note in `liq_get_quantization_error()`).

This function can be used to add upper limit to quality options presented to the user, e.g.

    liq_attr *attr = liq_attr_create();
    liq_image *img = liq_image_create_rgba(…);
    liq_result *res = liq_quantize_image(attr, img);
    int max_attainable_quality = liq_get_quantization_quality(res);
    printf("Please select quality between 0 and %d: ", max_attainable_quality);
    int user_selected_quality = prompt();
    if (user_selected_quality < max_attainable_quality) {
        liq_set_quality(user_selected_quality, 0);
        liq_result_destroy(res);
        res = liq_quantize_image(attr, img);
    }
    liq_write_remapped_image(…);

----

    double liq_get_remapping_quality(liq_result *result);

Analoguous to `liq_get_remapping_error()`, but returns quantization error as quality value in the same 0-100 range that is used by `liq_set_quality()`.

----

    void liq_set_log_callback(liq_attr*, liq_log_callback_function*, void *user_info);

<p>

    void log_callback_function(const liq_attr*, const char *message, void *user_info) {}

----

    void liq_set_log_flush_callback(liq_attr*, liq_log_flush_callback_function*, void *user_info);
<p>

    void log_flush_callback_function(const liq_attr*, void *user_info) {}

Sets up callback function to be called when the library reports status or errors. The callback must not call any library functions.

`user_info` value will be passed through to the callback. It can be `NULL`.

`NULL` callback clears the current callback.

In the log callback the `message` is a zero-terminated string containing informative message to output. It is valid only until the callback returns, so you must copy it.

`liq_set_log_flush_callback()` sets up callback function that will be called after the last log callback, which can be used to flush buffers and free resources used by the log callback.

----

    void liq_set_progress_callback(liq_attr*, liq_progress_callback_function*, void *user_info);
    void liq_result_set_progress_callback(liq_result*, liq_progress_callback_function*, void *user_info);

<p>

    int progress_callback_function(const liq_attr*, float progress_percent, void *user_info) {}

Sets up callback function to be called while the library is processing images. The callback may abort processing by returning `0`.

Setting callback to `NULL` clears the current callback. `liq_set_progress_callback` is for quantization progress, and `liq_result_set_progress_callback` is for remapping progress (currently only dithered remapping reports progress).

`user_info` value will be passed through to the callback. It can be `NULL`.

The callback must not call any library functions.

`progress_percent` is a value between 0 and 100 that estimates how much of the current task has been done.

The callback should return `1` to continue the operation, and `0` to abort current operation.

----

    liq_attr* liq_attr_create_with_allocator(void* (*malloc)(size_t), void (*free)(void*));

Same as `liq_attr_create`, but uses given `malloc` and `free` replacements to allocate all memory used by the library.

The `malloc` function must return 16-byte aligned memory on x86 (and on other architectures memory aligned for `double` and pointers). Conversely, if your stdlib's `malloc` doesn't return appropriately aligned memory, you should use this function to provide aligned replacements.

----

    liq_attr* liq_attr_copy(liq_attr *orig);

Creates an independent copy of `liq_attr`. The copy should also be freed using `liq_attr_destroy()`.

---

    liq_error liq_set_output_gamma(liq_result* res, double gamma);

Sets gamma correction for generated palette and remapped image. Must be > 0 and < 1, e.g. `0.45455` for gamma 1/2.2 in PNG images. By default output gamma is same as gamma of the input image.

----

    int liq_image_get_width(const liq_image *img);
    int liq_image_get_height(const liq_image *img);
    double liq_get_output_gamma(const liq_result *result);

Getters for `width`, `height` and `gamma` of the input image.

If the input is invalid, these all return -1.

---

    liq_error liq_image_add_fixed_color(liq_image* img, liq_color color);

Reserves a color in the output palette created from this image. It behaves as if the given color was used in the image and was very important.

RGB values of `liq_color` are assumed to have the same gamma as the image.

It must be called before the image is quantized.

Returns error if more than 256 colors are added. If image is quantized to fewer colors than the number of fixed colors added, then excess fixed colors will be ignored.

---

    int liq_version();

Returns version of the library as an integer. Same as `LIQ_VERSION`. Human-readable version is defined as `LIQ_VERSION_STRING`.

## Multithreading

The library is stateless and doesn't use any global or thread-local storage. It doesn't use any locks.

* Different threads can perform unrelated quantizations/remappings at the same time (e.g. each thread working on a different image).
* The same `liq_attr`, `liq_result`, etc. can be accessed from different threads, but not at the same time (e.g. you can create `liq_attr` in one thread and free it in another).

The library needs to sort unique colors present in the image. Although the sorting algorithm does few things to make stack usage minimal in typical cases, there is no guarantee against extremely degenerate cases, so threads should have automatically growing stack.

### OpenMP

The library will parallelize some operations if compiled with OpenMP.

You must not increase number of maximum threads after `liq_image` has been created, as it allocates some per-thread buffers.

Callback of `liq_image_create_custom()` may be called from different threads at the same time.

## Acknowledgements

Thanks to Irfan Skiljan for helping test the first version of the library.

The library is developed by [Kornel Lesiński](mailto:%20kornel@pngquant.org).
