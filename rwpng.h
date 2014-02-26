/*---------------------------------------------------------------------------

   pngquant:  RGBA -> RGBA-palette quantization program             rwpng.h

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

#ifndef RWPNG_H
#define RWPNG_H

#include "png.h"    /* libpng header; includes zlib.h */
#include <setjmp.h>

#ifndef USE_COCOA
#define USE_COCOA 0
#endif

typedef enum {
    SUCCESS = 0,
    MISSING_ARGUMENT = 1,
    READ_ERROR = 2,
    INVALID_ARGUMENT = 4,
    NOT_OVERWRITING_ERROR = 15,
    CANT_WRITE_ERROR = 16,
    OUT_OF_MEMORY_ERROR = 17,
    WRONG_ARCHITECTURE = 18, // Missing SSE3
    PNG_OUT_OF_MEMORY_ERROR = 24,
    LIBPNG_FATAL_ERROR = 25,
    LIBPNG_INIT_ERROR = 35,
    TOO_LARGE_FILE = 98,
    TOO_LOW_QUALITY = 99,
} pngquant_error;

typedef struct {
    jmp_buf jmpbuf;
    png_uint_32 width;
    png_uint_32 height;
    double gamma;
    unsigned char **row_pointers;
    unsigned char *rgba_data;
    png_size_t file_size;
} png24_image;

typedef struct {
    jmp_buf jmpbuf;
    png_uint_32 width;
    png_uint_32 height;
    double gamma;
    unsigned char **row_pointers;
    unsigned char *indexed_data;
    unsigned int num_palette;
    unsigned int num_trans;
    png_color palette[256];
    unsigned char trans[256];
    png_size_t maximum_file_size;
    char fast_compression;
} png8_image;

typedef union {
    jmp_buf jmpbuf;
    png24_image png24;
    png8_image png8;
} rwpng_png_image;

/* prototypes for public functions in rwpng.c */

void rwpng_version_info(FILE *fp);

pngquant_error rwpng_read_image24(FILE *infile, png24_image *mainprog_ptr);

pngquant_error rwpng_write_image8(FILE *outfile, png8_image *mainprog_ptr);
pngquant_error rwpng_write_image24(FILE *outfile, png24_image *mainprog_ptr);

#endif
