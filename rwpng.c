/*---------------------------------------------------------------------------

   pngquant:  RGBA -> RGBA-palette quantization program             rwpng.c

  ---------------------------------------------------------------------------

      Copyright (c) 1998-2000 Greg Roelofs.  All rights reserved.

      This software is provided "as is," without warranty of any kind,
      express or implied.  In no event shall the author or contributors
      be held liable for any damages arising in any way from the use of
      this software.

      Permission is granted to anyone to use this software for any purpose,
      including commercial applications, and to alter it and redistribute
      it freely, subject to the following restrictions:

      1. Redistributions of source code must retain the above copyright
         notice, disclaimer, and this list of conditions.
      2. Redistributions in binary form must reproduce the above copyright
         notice, disclaimer, and this list of conditions in the documenta-
         tion and/or other materials provided with the distribution.
      3. All advertising materials mentioning features or use of this
         software must display the following acknowledgment:

            This product includes software developed by Greg Roelofs
            and contributors for the book, "PNG: The Definitive Guide,"
            published by O'Reilly and Associates.

  ---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "png.h"
#include "rwpng.h"

#ifndef Z_BEST_COMPRESSION
#define Z_BEST_COMPRESSION 9
#endif
#ifndef Z_BEST_SPEED
#define Z_BEST_SPEED 1
#endif

static void rwpng_error_handler(png_structp png_ptr, png_const_charp msg);
int rwpng_read_image24_cocoa(FILE *infile, png24_image *mainprog_ptr);


void rwpng_version_info(FILE *fp)
{
    const char *pngver = png_get_header_ver(NULL);

    fprintf(fp, "   Compiled with libpng %s; using libpng %s.\n",
      PNG_LIBPNG_VER_STRING, pngver);

#if PNG_LIBPNG_VER < 10600
    if (strcmp(pngver, "1.3.") < 0) {
        fputs("\nWARNING: Your version of libpng is outdated and may produce corrupted files.\n"
              "Please recompile pngquant with newer version of libpng (1.5 or later.)\n", fp);
    }
#endif

#if USE_COCOA
    fputs("   Compiled with Apple Cocoa image reader.\n", fp);
#endif
}


struct rwpng_read_data {
    FILE *const fp;
    png_size_t bytes_read;
};

static void user_read_data(png_structp png_ptr, png_bytep data, png_size_t length)
{
    struct rwpng_read_data *read_data = (struct rwpng_read_data *)png_get_io_ptr(png_ptr);

    png_size_t read = fread(data, 1, length, read_data->fp);
    if (!read) png_error(png_ptr, "Read error");
    read_data->bytes_read += read;
}

struct rwpng_write_data {
    unsigned char *buffer;
    png_size_t bytes_written;
    png_size_t bytes_left;
};

static void user_write_data(png_structp png_ptr, png_bytep data, png_size_t length)
{
    struct rwpng_write_data *write_data = (struct rwpng_write_data *)png_get_io_ptr(png_ptr);

    if (length <= write_data->bytes_left) {
        memcpy(write_data->buffer + write_data->bytes_written, data, length);
        write_data->bytes_left -= length;
        write_data->bytes_written += length;
    } else {
        write_data->bytes_written = 0;
        write_data->bytes_left = 0;
    }
}

static void user_flush_data(png_structp png_ptr)
{
    // libpng never calls this :(
}


static png_bytepp rwpng_create_row_pointers(png_infop info_ptr, png_structp png_ptr, unsigned char *base, unsigned int height, unsigned int rowbytes)
{
    if (!rowbytes) rowbytes = png_get_rowbytes(png_ptr, info_ptr);

    png_bytepp row_pointers = malloc(height * sizeof(row_pointers[0]));
    if (!row_pointers) return NULL;
    for(unsigned int row = 0;  row < height;  ++row) {
        row_pointers[row] = base + row * rowbytes;
    }
    return row_pointers;
}


/*
   retval:
     0 = success
    21 = bad sig
    22 = bad IHDR
    24 = insufficient memory
    25 = libpng error (via longjmp())
    26 = wrong PNG color type (no alpha channel)
 */

pngquant_error rwpng_read_image24_libpng(FILE *infile, png24_image *mainprog_ptr)
{
    png_structp  png_ptr = NULL;
    png_infop    info_ptr = NULL;
    png_size_t   rowbytes;
    int          color_type, bit_depth;

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, mainprog_ptr,
      rwpng_error_handler, NULL);
    if (!png_ptr) {
        return PNG_OUT_OF_MEMORY_ERROR;   /* out of memory */
    }

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return PNG_OUT_OF_MEMORY_ERROR;   /* out of memory */
    }

    /* setjmp() must be called in every function that calls a non-trivial
     * libpng function */

    if (setjmp(mainprog_ptr->jmpbuf)) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return LIBPNG_FATAL_ERROR;   /* fatal libpng error (via longjmp()) */
    }

    struct rwpng_read_data read_data = {infile, 0};
    png_set_read_fn(png_ptr, &read_data, user_read_data);

    png_read_info(png_ptr, info_ptr);  /* read all PNG info up to image data */


    /* alternatively, could make separate calls to png_get_image_width(),
     * etc., but want bit_depth and color_type for later [don't care about
     * compression_type and filter_type => NULLs] */

    png_get_IHDR(png_ptr, info_ptr, &mainprog_ptr->width, &mainprog_ptr->height,
      &bit_depth, &color_type, NULL, NULL, NULL);


    /* expand palette images to RGB, low-bit-depth grayscale images to 8 bits,
     * transparency chunks to full alpha channel; strip 16-bit-per-sample
     * images to 8 bits per sample; and convert grayscale to RGB[A] */

    /* GRR TO DO:  preserve all safe-to-copy ancillary PNG chunks */

    if (!(color_type & PNG_COLOR_MASK_ALPHA)) {
#ifdef PNG_READ_FILLER_SUPPORTED
        png_set_expand(png_ptr);
        png_set_filler(png_ptr, 65535L, PNG_FILLER_AFTER);
#else
        fprintf(stderr, "pngquant readpng:  image is neither RGBA nor GA\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        mainprog_ptr->retval = 26;
        return mainprog_ptr->retval;
#endif
    }
/*
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_expand(png_ptr);
 */
    if (bit_depth == 16)
        png_set_strip_16(png_ptr);

    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);


    /* get and save the gamma info (if any) for writing */

    double gamma;
    mainprog_ptr->gamma = png_get_gAMA(png_ptr, info_ptr, &gamma) ? gamma : 0.45455;

    png_set_interlace_handling(png_ptr);

    /* all transformations have been registered; now update info_ptr data,
     * get rowbytes and channels, and allocate image memory */

    png_read_update_info(png_ptr, info_ptr);

    rowbytes = png_get_rowbytes(png_ptr, info_ptr);

    if ((mainprog_ptr->rgba_data = malloc(rowbytes*mainprog_ptr->height)) == NULL) {
        fprintf(stderr, "pngquant readpng:  unable to allocate image data\n");
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return PNG_OUT_OF_MEMORY_ERROR;
    }

    png_bytepp row_pointers = rwpng_create_row_pointers(info_ptr, png_ptr, mainprog_ptr->rgba_data, mainprog_ptr->height, 0);

    /* now we can go ahead and just read the whole image */

    png_read_image(png_ptr, row_pointers);

    /* and we're done!  (png_read_end() can be omitted if no processing of
     * post-IDAT text/time/etc. is desired) */

    png_read_end(png_ptr, NULL);

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    mainprog_ptr->file_size = read_data.bytes_read;
    mainprog_ptr->row_pointers = (unsigned char **)row_pointers;

    return SUCCESS;
}


pngquant_error rwpng_read_image24(FILE *infile, png24_image *input_image_p)
{
#if USE_COCOA
    return rwpng_read_image24_cocoa(infile, input_image_p);
#else
    return rwpng_read_image24_libpng(infile, input_image_p);
#endif
}


static pngquant_error rwpng_write_image_init(rwpng_png_image *mainprog_ptr, png_structpp png_ptr_p, png_infopp info_ptr_p, int fast_compression)
{
    /* could also replace libpng warning-handler (final NULL), but no need: */

    *png_ptr_p = png_create_write_struct(PNG_LIBPNG_VER_STRING, mainprog_ptr, rwpng_error_handler, NULL);

    if (!(*png_ptr_p)) {
        return LIBPNG_INIT_ERROR;   /* out of memory */
    }

    *info_ptr_p = png_create_info_struct(*png_ptr_p);
    if (!(*info_ptr_p)) {
        png_destroy_write_struct(png_ptr_p, NULL);
        return LIBPNG_INIT_ERROR;   /* out of memory */
    }

    /* setjmp() must be called in every function that calls a PNG-writing
     * libpng function, unless an alternate error handler was installed--
     * but compatible error handlers must either use longjmp() themselves
     * (as in this program) or exit immediately, so here we go: */

    if (setjmp(mainprog_ptr->jmpbuf)) {
        png_destroy_write_struct(png_ptr_p, info_ptr_p);
        return LIBPNG_INIT_ERROR;   /* libpng error (via longjmp()) */
    }

    png_set_compression_level(*png_ptr_p, fast_compression ? Z_BEST_SPEED : Z_BEST_COMPRESSION);

    return SUCCESS;
}


void rwpng_write_end(png_infopp info_ptr_p, png_structpp png_ptr_p, png_bytepp row_pointers)
{
    png_write_info(*png_ptr_p, *info_ptr_p);

    png_set_packing(*png_ptr_p);

    png_write_image(*png_ptr_p, row_pointers);

    png_write_end(*png_ptr_p, NULL);

    png_destroy_write_struct(png_ptr_p, info_ptr_p);
}

void rwpng_set_gamma(png_infop info_ptr, png_structp png_ptr, double gamma)
{
    if (gamma > 0.0) {
        png_set_gAMA(png_ptr, info_ptr, gamma);

        if (gamma > 0.45454 && gamma < 0.45456) {
            png_set_sRGB(png_ptr, info_ptr, 0); // 0 = Perceptual
        }
    }
}

pngquant_error rwpng_write_image8(FILE *outfile, png8_image *mainprog_ptr)
{
    png_structp png_ptr;
    png_infop info_ptr;

    pngquant_error retval = rwpng_write_image_init((rwpng_png_image*)mainprog_ptr, &png_ptr, &info_ptr, mainprog_ptr->fast_compression);
    if (retval) return retval;

    struct rwpng_write_data write_data;
    if (mainprog_ptr->maximum_file_size) {
        write_data = (struct rwpng_write_data){
            .buffer = malloc(mainprog_ptr->maximum_file_size),
            .bytes_left = mainprog_ptr->maximum_file_size,
        };
        if (!write_data.buffer) return PNG_OUT_OF_MEMORY_ERROR;
        png_set_write_fn(png_ptr, &write_data, user_write_data, user_flush_data);
    } else {
        png_init_io(png_ptr, outfile);
    }

    // Palette images generally don't gain anything from filtering
    png_set_filter(png_ptr, PNG_FILTER_TYPE_BASE, PNG_FILTER_VALUE_NONE);

    rwpng_set_gamma(info_ptr, png_ptr, mainprog_ptr->gamma);

    /* set the image parameters appropriately */
    int sample_depth;
#if PNG_LIBPNG_VER > 10400 /* old libpng corrupts files with low depth */
    if (mainprog_ptr->num_palette <= 2)
        sample_depth = 1;
    else if (mainprog_ptr->num_palette <= 4)
        sample_depth = 2;
    else if (mainprog_ptr->num_palette <= 16)
        sample_depth = 4;
    else
#endif
        sample_depth = 8;

    png_set_IHDR(png_ptr, info_ptr, mainprog_ptr->width, mainprog_ptr->height,
      sample_depth, PNG_COLOR_TYPE_PALETTE,
      0, PNG_COMPRESSION_TYPE_DEFAULT,
      PNG_FILTER_TYPE_BASE);

    png_set_PLTE(png_ptr, info_ptr, &mainprog_ptr->palette[0], mainprog_ptr->num_palette);

    if (mainprog_ptr->num_trans > 0)
        png_set_tRNS(png_ptr, info_ptr, mainprog_ptr->trans, mainprog_ptr->num_trans, NULL);

    rwpng_write_end(&info_ptr, &png_ptr, mainprog_ptr->row_pointers);

    if (mainprog_ptr->maximum_file_size) {
        if (!write_data.bytes_written) {
            retval = TOO_LARGE_FILE;
        } else {
            if (!fwrite(write_data.buffer, 1, write_data.bytes_written, outfile)) {
                retval = CANT_WRITE_ERROR;
            }
        }

        free(write_data.buffer);
    }
    return retval;
}

pngquant_error rwpng_write_image24(FILE *outfile, png24_image *mainprog_ptr)
{
    png_structp png_ptr;
    png_infop info_ptr;

    pngquant_error retval = rwpng_write_image_init((rwpng_png_image*)mainprog_ptr, &png_ptr, &info_ptr, 0);
    if (retval) return retval;

    png_init_io(png_ptr, outfile);

    rwpng_set_gamma(info_ptr, png_ptr, mainprog_ptr->gamma);

    png_set_IHDR(png_ptr, info_ptr, mainprog_ptr->width, mainprog_ptr->height,
                 8, PNG_COLOR_TYPE_RGB_ALPHA,
                 0, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_BASE);


    png_bytepp row_pointers = rwpng_create_row_pointers(info_ptr, png_ptr, mainprog_ptr->rgba_data, mainprog_ptr->height, 0);

    rwpng_write_end(&info_ptr, &png_ptr, row_pointers);

    free(row_pointers);

    return SUCCESS;
}


static void rwpng_error_handler(png_structp png_ptr, png_const_charp msg)
{
    rwpng_png_image  *mainprog_ptr;

    /* This function, aside from the extra step of retrieving the "error
     * pointer" (below) and the fact that it exists within the application
     * rather than within libpng, is essentially identical to libpng's
     * default error handler.  The second point is critical:  since both
     * setjmp() and longjmp() are called from the same code, they are
     * guaranteed to have compatible notions of how big a jmp_buf is,
     * regardless of whether _BSD_SOURCE or anything else has (or has not)
     * been defined. */

    fprintf(stderr, "  error: %s\n", msg);
    fflush(stderr);

    mainprog_ptr = png_get_error_ptr(png_ptr);
    if (mainprog_ptr == NULL) abort();

    longjmp(mainprog_ptr->jmpbuf, 1);
}
