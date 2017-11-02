/*
** PNG read/write functions
**
** © 1998-2000 by Greg Roelofs.
** © 2009-2017 by Kornel Lesiński.
**
** See COPYRIGHT file for license.
*/

#ifndef RWPNG_H
#define RWPNG_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
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
    WRONG_ARCHITECTURE = 18, // Missing SSE
    PNG_OUT_OF_MEMORY_ERROR = 24,
    LIBPNG_FATAL_ERROR = 25,
    WRONG_INPUT_COLOR_TYPE = 26,
    LIBPNG_INIT_ERROR = 35,
    TOO_LARGE_FILE = 98,
    TOO_LOW_QUALITY = 99,
} pngquant_error;

typedef struct rwpng_rgba {
  unsigned char r,g,b,a;
} rwpng_rgba;

struct rwpng_chunk {
    struct rwpng_chunk *next;
    unsigned char *data;
    size_t size;
    unsigned char name[5];
    unsigned char location;
};

typedef enum {
  RWPNG_NONE,
  RWPNG_SRGB, // sRGB chunk
  RWPNG_ICCP, // used ICC profile
  RWPNG_ICCP_WARN_GRAY, // ignore and warn about GRAY ICC profile
  RWPNG_GAMA_CHRM, // used gAMA and cHRM
  RWPNG_GAMA_ONLY, // used gAMA only (i.e. not sRGB)
  RWPNG_COCOA, // Colors handled by Cocoa reader
} rwpng_color_transform;

typedef struct {
    jmp_buf jmpbuf;
    uint32_t width;
    uint32_t height;
    size_t file_size;
    double gamma;
    unsigned char **row_pointers;
    unsigned char *rgba_data;
    struct rwpng_chunk *chunks;
    rwpng_color_transform input_color;
    rwpng_color_transform output_color;
} png24_image;

typedef struct {
    jmp_buf jmpbuf;
    uint32_t width;
    uint32_t height;
    size_t maximum_file_size;
    size_t metadata_size;
    double gamma;
    unsigned char **row_pointers;
    unsigned char *indexed_data;
    struct rwpng_chunk *chunks;
    unsigned int num_palette;
    rwpng_rgba palette[256];
    rwpng_color_transform output_color;
    char fast_compression;
} png8_image;

typedef union {
    jmp_buf jmpbuf;
    png24_image png24;
    png8_image png8;
} rwpng_png_image;

/* prototypes for public functions in rwpng.c */

void rwpng_version_info(FILE *fp);

pngquant_error rwpng_read_image24(FILE *infile, png24_image *mainprog_ptr, int strip, int verbose);
pngquant_error rwpng_write_image8(FILE *outfile, png8_image *mainprog_ptr);
pngquant_error rwpng_write_image24(FILE *outfile, const png24_image *mainprog_ptr);
void rwpng_free_image24(png24_image *);
void rwpng_free_image8(png8_image *);

#endif
