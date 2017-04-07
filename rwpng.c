/*---------------------------------------------------------------------------

   pngquant:  RGBA -> RGBA-palette quantization program             rwpng.c

  ---------------------------------------------------------------------------

   © 1998-2000 by Greg Roelofs.
   © 2009-2015 by Kornel Lesiński.

   All rights reserved.

   Redistribution and use in source and binary forms, with or without modification,
   are permitted provided that the following conditions are met:

   1. Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.

   2. Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
   AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
   IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
   FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
   SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
   CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
   OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

  ---------------------------------------------------------------------------*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "png.h"  /* if this include fails, you need to install libpng (e.g. libpng-devel package) and run ./configure */
#include "rwpng.h"
#if USE_LCMS
#include "lcms2.h"
#endif

#ifndef Z_BEST_COMPRESSION
#define Z_BEST_COMPRESSION 9
#endif
#ifndef Z_BEST_SPEED
#define Z_BEST_SPEED 1
#endif

#ifdef _OPENMP
#include <omp.h>
#else
#define omp_get_max_threads() 1
#endif

#if PNG_LIBPNG_VER < 10500
typedef png_const_charp png_const_bytep;
#endif

static void rwpng_error_handler(png_structp png_ptr, png_const_charp msg);
int rwpng_read_image24_cocoa(FILE *infile, png24_image *mainprog_ptr);


void rwpng_version_info(FILE *fp)
{
    const char *pngver = png_get_header_ver(NULL);

#if USE_COCOA
    fprintf(fp, "   Color profiles are supported via Cocoa. Using libpng %s.\n", pngver);
#elif USE_LCMS
    fprintf(fp, "   Color profiles are supported via Little CMS. Using libpng %s.\n", pngver);
#else
    fprintf(fp, "   Compiled with no support for color profiles. Using libpng %s.\n", pngver);
#endif

#if PNG_LIBPNG_VER < 10600
    if (strcmp(pngver, "1.3.") < 0) {
        fputs("\nWARNING: Your version of libpng is outdated and may produce corrupted files.\n"
              "Please recompile pngquant with the current version of libpng (1.6 or later).\n", fp);
    } else if (strcmp(pngver, "1.6.") < 0) {
        #if defined(PNG_UNKNOWN_CHUNKS_SUPPORTED)
        fputs("\nWARNING: Your version of libpng is old and has buggy support for custom chunks.\n"
              "Please recompile pngquant with the current version of libpng (1.6 or later).\n", fp);
        #endif
    }
#endif
}


struct rwpng_read_data {
    FILE *const fp;
    png_size_t bytes_read;
};

#if !USE_COCOA
static void user_read_data(png_structp png_ptr, png_bytep data, png_size_t length)
{
    struct rwpng_read_data *read_data = (struct rwpng_read_data *)png_get_io_ptr(png_ptr);

    png_size_t read = fread(data, 1, length, read_data->fp);
    if (!read) {
        png_error(png_ptr, "Read error");
    }
    read_data->bytes_read += read;
}
#endif

struct rwpng_write_state {
    FILE *outfile;
    png_size_t maximum_file_size;
    png_size_t bytes_written;
    pngquant_error retval;
};

static void user_write_data(png_structp png_ptr, png_bytep data, png_size_t length)
{
    struct rwpng_write_state *write_state = (struct rwpng_write_state *)png_get_io_ptr(png_ptr);

    if (SUCCESS != write_state->retval) {
        return;
    }

    if (!fwrite(data, length, 1, write_state->outfile)) {
        write_state->retval = CANT_WRITE_ERROR;
    }

    write_state->bytes_written += length;
}

static void user_flush_data(png_structp png_ptr)
{
    (void)(png_ptr);
    // libpng never calls this :(
}


static png_bytepp rwpng_create_row_pointers(png_infop info_ptr, png_structp png_ptr, unsigned char *base, unsigned int height, png_size_t rowbytes)
{
    if (!rowbytes) {
        rowbytes = png_get_rowbytes(png_ptr, info_ptr);
    }

    png_bytepp row_pointers = malloc(height * sizeof(row_pointers[0]));
    if (!row_pointers) return NULL;
    for(size_t row = 0; row < height; row++) {
        row_pointers[row] = base + row * rowbytes;
    }
    return row_pointers;
}

#if !USE_COCOA
static int read_chunk_callback(png_structp png_ptr, png_unknown_chunkp in_chunk)
{
    if (0 == memcmp("iCCP", in_chunk->name, 5) ||
        0 == memcmp("cHRM", in_chunk->name, 5) ||
        0 == memcmp("gAMA", in_chunk->name, 5)) {
        return 0; // not handled
    }

    if (in_chunk->location == 0 ) {
        return 1; // ignore chunks with invalid location
    }

    struct rwpng_chunk **head = (struct rwpng_chunk **)png_get_user_chunk_ptr(png_ptr);

    struct rwpng_chunk *chunk = malloc(sizeof(struct rwpng_chunk));
    memcpy(chunk->name, in_chunk->name, 5);
    chunk->size = in_chunk->size;
    chunk->location = in_chunk->location;
    chunk->data = in_chunk->size ? malloc(in_chunk->size) : NULL;
    if (in_chunk->size) {
        memcpy(chunk->data, in_chunk->data, in_chunk->size);
    }

    chunk->next = *head;
    *head = chunk;

    return 1; // marks as "handled", libpng won't store it
}
#endif

/*
   retval:
     0 = success
    21 = bad sig
    22 = bad IHDR
    24 = insufficient memory
    25 = libpng error (via longjmp())
    26 = wrong PNG color type (no alpha channel)
 */

#if !USE_COCOA
static void rwpng_warning_stderr_handler(png_structp png_ptr, png_const_charp msg) {
    (void)(png_ptr);
    fprintf(stderr, "  libpng warning: %s\n", msg);
}

static void rwpng_warning_silent_handler(png_structp png_ptr, png_const_charp msg) {
    (void)(png_ptr);
    (void)(msg);
}

static pngquant_error rwpng_read_image24_libpng(FILE *infile, png24_image *mainprog_ptr, int strip, int verbose)
{
    png_structp  png_ptr = NULL;
    png_infop    info_ptr = NULL;
    png_size_t   rowbytes;
    int          color_type, bit_depth;

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, mainprog_ptr,
      rwpng_error_handler, verbose ? rwpng_warning_stderr_handler : rwpng_warning_silent_handler);
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

#if defined(PNG_SKIP_sRGB_CHECK_PROFILE) && defined(PNG_SET_OPTION_SUPPORTED)
    png_set_option(png_ptr, PNG_SKIP_sRGB_CHECK_PROFILE, PNG_OPTION_ON);
#endif

#if PNG_LIBPNG_VER >= 10500 && defined(PNG_UNKNOWN_CHUNKS_SUPPORTED)
    if (!strip) {
        /* copy standard chunks too */
        png_set_keep_unknown_chunks(png_ptr, PNG_HANDLE_CHUNK_IF_SAFE, (png_const_bytep)"pHYs\0iTXt\0tEXt\0zTXt", 4);
    }
#endif
    if (!strip) {
        png_set_read_user_chunk_fn(png_ptr, &mainprog_ptr->chunks, read_chunk_callback);
    }

    struct rwpng_read_data read_data = {infile, 0};
    png_set_read_fn(png_ptr, &read_data, user_read_data);

    png_read_info(png_ptr, info_ptr);  /* read all PNG info up to image data */

    /* alternatively, could make separate calls to png_get_image_width(),
     * etc., but want bit_depth and color_type for later [don't care about
     * compression_type and filter_type => NULLs] */

    png_get_IHDR(png_ptr, info_ptr, (png_uint_32 *)&mainprog_ptr->width, (png_uint_32 *)&mainprog_ptr->height,
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
        mainprog_ptr->retval = WRONG_INPUT_COLOR_TYPE;
        return mainprog_ptr->retval;
#endif
    }

    if (bit_depth == 16) {
        png_set_strip_16(png_ptr);
    }

    if (!(color_type & PNG_COLOR_MASK_COLOR)) {
        png_set_gray_to_rgb(png_ptr);
    }

    /* get source gamma for gamma correction, or use sRGB default */
    double gamma = 0.45455;
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_sRGB)) {
        mainprog_ptr->input_color = RWPNG_SRGB;
        mainprog_ptr->output_color = RWPNG_SRGB;
    } else {
        png_get_gAMA(png_ptr, info_ptr, &gamma);
        if (gamma > 0 && gamma <= 1.0) {
            mainprog_ptr->input_color = RWPNG_GAMA_ONLY;
            mainprog_ptr->output_color = RWPNG_GAMA_ONLY;
        } else {
            fprintf(stderr, "pngquant readpng:  ignored out-of-range gamma %f\n", gamma);
            mainprog_ptr->input_color = RWPNG_NONE;
            mainprog_ptr->output_color = RWPNG_NONE;
            gamma = 0.45455;
        }
    }
    mainprog_ptr->gamma = gamma;

    png_set_interlace_handling(png_ptr);

    /* all transformations have been registered; now update info_ptr data,
     * get rowbytes and channels, and allocate image memory */

    png_read_update_info(png_ptr, info_ptr);

    rowbytes = png_get_rowbytes(png_ptr, info_ptr);

    // For overflow safety reject images that won't fit in 32-bit
    if (rowbytes > INT_MAX/mainprog_ptr->height) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return PNG_OUT_OF_MEMORY_ERROR;
    }

    if ((mainprog_ptr->rgba_data = malloc(rowbytes * mainprog_ptr->height)) == NULL) {
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

#if USE_LCMS
#if PNG_LIBPNG_VER < 10500
    png_charp ProfileData;
#else
    png_bytep ProfileData;
#endif
    png_uint_32 ProfileLen;

    cmsHPROFILE hInProfile = NULL;

    /* color_type is read from the image before conversion to RGBA */
    int COLOR_PNG = color_type & PNG_COLOR_MASK_COLOR;

    /* embedded ICC profile */
    if (png_get_iCCP(png_ptr, info_ptr, &(png_charp){0}, &(int){0}, &ProfileData, &ProfileLen)) {

        hInProfile = cmsOpenProfileFromMem(ProfileData, ProfileLen);
        cmsColorSpaceSignature colorspace = cmsGetColorSpace(hInProfile);

        /* only RGB (and GRAY) valid for PNGs */
        if (colorspace == cmsSigRgbData && COLOR_PNG) {
            mainprog_ptr->input_color = RWPNG_ICCP;
            mainprog_ptr->output_color = RWPNG_SRGB;
        } else {
            if (colorspace == cmsSigGrayData && !COLOR_PNG) {
                mainprog_ptr->input_color = RWPNG_ICCP_WARN_GRAY;
                mainprog_ptr->output_color = RWPNG_SRGB;
            }
            cmsCloseProfile(hInProfile);
            hInProfile = NULL;
        }
    }

    /* build RGB profile from cHRM and gAMA */
    if (hInProfile == NULL && COLOR_PNG &&
        !png_get_valid(png_ptr, info_ptr, PNG_INFO_sRGB) &&
        png_get_valid(png_ptr, info_ptr, PNG_INFO_gAMA) &&
        png_get_valid(png_ptr, info_ptr, PNG_INFO_cHRM)) {

        cmsCIExyY WhitePoint;
        cmsCIExyYTRIPLE Primaries;

        png_get_cHRM(png_ptr, info_ptr, &WhitePoint.x, &WhitePoint.y,
                     &Primaries.Red.x, &Primaries.Red.y,
                     &Primaries.Green.x, &Primaries.Green.y,
                     &Primaries.Blue.x, &Primaries.Blue.y);

        WhitePoint.Y = Primaries.Red.Y = Primaries.Green.Y = Primaries.Blue.Y = 1.0;

        cmsToneCurve *GammaTable[3];
        GammaTable[0] = GammaTable[1] = GammaTable[2] = cmsBuildGamma(NULL, 1/gamma);

        hInProfile = cmsCreateRGBProfile(&WhitePoint, &Primaries, GammaTable);

        cmsFreeToneCurve(GammaTable[0]);

        mainprog_ptr->input_color = RWPNG_GAMA_CHRM;
        mainprog_ptr->output_color = RWPNG_SRGB;
    }

    /* transform image to sRGB colorspace */
    if (hInProfile != NULL) {

        cmsHPROFILE hOutProfile = cmsCreate_sRGBProfile();
        cmsHTRANSFORM hTransform = cmsCreateTransform(hInProfile, TYPE_RGBA_8,
                                                      hOutProfile, TYPE_RGBA_8,
                                                      INTENT_PERCEPTUAL,
                                                      omp_get_max_threads() > 1 ? cmsFLAGS_NOCACHE : 0);

        #pragma omp parallel for \
            if (mainprog_ptr->height*mainprog_ptr->width > 8000) \
            schedule(static)
        for (unsigned int i = 0; i < mainprog_ptr->height; i++) {
            /* It is safe to use the same block for input and output,
               when both are of the same TYPE. */
            cmsDoTransform(hTransform, row_pointers[i],
                                       row_pointers[i],
                                       mainprog_ptr->width);
        }

        cmsDeleteTransform(hTransform);
        cmsCloseProfile(hOutProfile);
        cmsCloseProfile(hInProfile);

        mainprog_ptr->gamma = 0.45455;
    }
#endif

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    mainprog_ptr->file_size = read_data.bytes_read;
    mainprog_ptr->row_pointers = (unsigned char **)row_pointers;

    return SUCCESS;
}
#endif

static void rwpng_free_chunks(struct rwpng_chunk *chunk) {
    if (!chunk) return;
    rwpng_free_chunks(chunk->next);
    free(chunk->data);
    free(chunk);
}

void rwpng_free_image24(png24_image *image)
{
    free(image->row_pointers);
    image->row_pointers = NULL;

    free(image->rgba_data);
    image->rgba_data = NULL;

    rwpng_free_chunks(image->chunks);
    image->chunks = NULL;
}

void rwpng_free_image8(png8_image *image)
{
    free(image->indexed_data);
    image->indexed_data = NULL;

    free(image->row_pointers);
    image->row_pointers = NULL;

    rwpng_free_chunks(image->chunks);
    image->chunks = NULL;
}

pngquant_error rwpng_read_image24(FILE *infile, png24_image *input_image_p, int strip, int verbose)
{
#if USE_COCOA
    return rwpng_read_image24_cocoa(infile, input_image_p);
#else
    return rwpng_read_image24_libpng(infile, input_image_p, strip, verbose);
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
    png_set_compression_mem_level(*png_ptr_p, fast_compression ? 9 : 5); // judging by optipng results, smaller mem makes libpng compress slightly better

    return SUCCESS;
}


static void rwpng_write_end(png_infopp info_ptr_p, png_structpp png_ptr_p, png_bytepp row_pointers)
{
    png_write_info(*png_ptr_p, *info_ptr_p);

    png_set_packing(*png_ptr_p);

    png_write_image(*png_ptr_p, row_pointers);

    png_write_end(*png_ptr_p, NULL);

    png_destroy_write_struct(png_ptr_p, info_ptr_p);
}

static void rwpng_set_gamma(png_infop info_ptr, png_structp png_ptr, double gamma, rwpng_color_transform color)
{
    if (color != RWPNG_GAMA_ONLY && color != RWPNG_NONE) {
        png_set_gAMA(png_ptr, info_ptr, gamma);
    }
    if (color == RWPNG_SRGB) {
        png_set_sRGB(png_ptr, info_ptr, 0); // 0 = Perceptual
    }
}

pngquant_error rwpng_write_image8(FILE *outfile, png8_image *mainprog_ptr)
{
    png_structp png_ptr;
    png_infop info_ptr;

    if (mainprog_ptr->num_palette > 256) return INVALID_ARGUMENT;

    pngquant_error retval = rwpng_write_image_init((rwpng_png_image*)mainprog_ptr, &png_ptr, &info_ptr, mainprog_ptr->fast_compression);
    if (retval) return retval;

    struct rwpng_write_state write_state;
    write_state = (struct rwpng_write_state){
        .outfile = outfile,
        .maximum_file_size = mainprog_ptr->maximum_file_size,
        .retval = SUCCESS,
    };
    png_set_write_fn(png_ptr, &write_state, user_write_data, user_flush_data);

    // Palette images generally don't gain anything from filtering
    png_set_filter(png_ptr, PNG_FILTER_TYPE_BASE, PNG_FILTER_VALUE_NONE);

    rwpng_set_gamma(info_ptr, png_ptr, mainprog_ptr->gamma, mainprog_ptr->output_color);

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

    struct rwpng_chunk *chunk = mainprog_ptr->chunks;
    mainprog_ptr->metadata_size = 0;
    int chunk_num=0;
    while(chunk) {
        png_unknown_chunk pngchunk = {
            .size = chunk->size,
            .data = chunk->data,
            .location = chunk->location,
        };
        memcpy(pngchunk.name, chunk->name, 5);
        png_set_unknown_chunks(png_ptr, info_ptr, &pngchunk, 1);

        #if defined(PNG_HAVE_IHDR) && PNG_LIBPNG_VER < 10600
        png_set_unknown_chunk_location(png_ptr, info_ptr, chunk_num, pngchunk.location ? pngchunk.location : PNG_HAVE_IHDR);
        #endif

        mainprog_ptr->metadata_size += chunk->size + 12;
        chunk = chunk->next;
        chunk_num++;
    }

    png_set_IHDR(png_ptr, info_ptr, mainprog_ptr->width, mainprog_ptr->height,
      sample_depth, PNG_COLOR_TYPE_PALETTE,
      0, PNG_COMPRESSION_TYPE_DEFAULT,
      PNG_FILTER_TYPE_BASE);

    png_color palette[256];
    png_byte trans[256];
    unsigned int num_trans = 0;
    for(unsigned int i = 0; i < mainprog_ptr->num_palette; i++) {
        palette[i] = (png_color){
            .red   = mainprog_ptr->palette[i].r,
            .green = mainprog_ptr->palette[i].g,
            .blue  = mainprog_ptr->palette[i].b,
        };
        trans[i] = mainprog_ptr->palette[i].a;
        if (mainprog_ptr->palette[i].a < 255) {
            num_trans = i+1;
        }
    }

    png_set_PLTE(png_ptr, info_ptr, palette, mainprog_ptr->num_palette);

    if (num_trans > 0) {
        png_set_tRNS(png_ptr, info_ptr, trans, num_trans, NULL);
    }

    rwpng_write_end(&info_ptr, &png_ptr, mainprog_ptr->row_pointers);

    if (SUCCESS == write_state.retval && write_state.maximum_file_size && write_state.bytes_written > write_state.maximum_file_size) {
        return TOO_LARGE_FILE;
    }

    return write_state.retval;
}

pngquant_error rwpng_write_image24(FILE *outfile, const png24_image *mainprog_ptr)
{
    png_structp png_ptr;
    png_infop info_ptr;

    pngquant_error retval = rwpng_write_image_init((rwpng_png_image*)mainprog_ptr, &png_ptr, &info_ptr, 0);
    if (retval) return retval;

    png_init_io(png_ptr, outfile);

    rwpng_set_gamma(info_ptr, png_ptr, mainprog_ptr->gamma, mainprog_ptr->output_color);

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

    fprintf(stderr, "  error: %s (libpng failed)\n", msg);
    fflush(stderr);

    mainprog_ptr = png_get_error_ptr(png_ptr);
    if (mainprog_ptr == NULL) abort();

    longjmp(mainprog_ptr->jmpbuf, 1);
}
