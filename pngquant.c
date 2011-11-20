/* pngquant.c - quantize the colors in an alphamap down to a specified number
**
** Copyright (C) 1989, 1991 by Jef Poskanzer.
** Copyright (C) 1997, 2000, 2002 by Greg Roelofs; based on an idea by
**                                Stefan Schneider.
** (C) 2011 by Kornel Lesinski.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

/* GRR TO DO:  "original file size" and "quantized file size" if verbose? */
/* GRR TO DO:  add option to preserve background color (if any) exactly */
/* GRR TO DO:  add mapfile support, but cleanly (build palette in main()) */
/* GRR TO DO:  support 16 bps without down-conversion */
/* GRR TO DO:  if all samples are gray and image is opaque and sample depth
                would be no bigger than palette and user didn't explicitly
                specify a mapfile, switch to grayscale */
/* GRR TO DO:  if all samples are 0 or maxval, eliminate gAMA chunk (rwpng.c) */

#define PNGQUANT_VERSION "1.6 (October 2011)"

#define PNGQUANT_USAGE "\
   usage:  pngquant [options] [ncolors] [pngfile [pngfile ...]]\n\n\
   options:\n\
      -force        overwrite existing output files (synonym: -f)\n\
      -ext new.png  set custom extension for output filename\n\
      -nofs         disable dithering (synonyms: -nofloyd, -ordered)\n\
      -verbose      print status messages (synonyms: -noquiet)\n\
      -speed N      speed/quality trade-off. 1=slow, 3=default, 10=fast & rough\n\
      -iebug        increase opacity to work around Internet Explorer 6 bug\n\
\n\
   Quantizes one or more 32-bit RGBA PNGs to 8-bit (or smaller) RGBA-palette\n\
   PNGs using Floyd-Steinberg diffusion dithering (unless disabled).\n\
   The output filename is the same as the input name except that\n\
   it ends in \"-fs8.png\", \"-or8.png\" or your custom extension (unless the\n\
   input is stdin, in which case the quantized image will go to stdout).\n\
   The default behavior if the output file exists is to skip the conversion;\n\
   use -force to overwrite.\n"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#ifdef WIN32        /* defined in Makefile.w32 (or use _MSC_VER for MSVC) */
#  include <fcntl.h>    /* O_BINARY */
#  include <io.h>   /* setmode() */
#endif

#include "rwpng.h"  /* typedefs, common macros, public prototypes */
#include "pam.h"
#include "mediancut.h"
#include "blur.h"
#include "viter.h"

pngquant_error pngquant(read_info *input_image, write_info *output_image, int floyd, int reqcolors, int ie_bug, int speed_tradeoff);
pngquant_error read_image(const char *filename, int using_stdin, read_info *input_image_p);
pngquant_error write_image(write_info *output_image,const char *filename,const char *newext,int force,int using_stdin);

static int verbose=0;
void verbose_printf(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    if (verbose) vfprintf(stderr, fmt, va);
    va_end(va);
}

static void print_full_version(FILE *fd)
{
    fprintf(fd, "pngquant-improved, version %s, by Greg Roelofs, Kornel Lesinski.\n", PNGQUANT_VERSION);
    rwpng_version_info(fd);
    fputs("\n", fd);
}

static void print_usage(FILE *fd)
{
    fputs(PNGQUANT_USAGE, fd);
}

int main(int argc, char *argv[])
{
    int argn;
    int reqcolors;
    int floyd = TRUE;
    int force = FALSE;
    int ie_bug = FALSE;
    int speed_tradeoff = 3; // 1 max quality, 10 rough & fast. 3 is optimum.
    int using_stdin = FALSE;
    int latest_error=0, error_count=0, file_count=0;
    const char *filename, *newext = NULL;

    argn = 1;

    while (argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0') {
        if (0 == strcmp(argv[argn], "--")) { ++argn;break; }

        if ( 0 == strncmp(argv[argn], "-fs", 3) ||
             0 == strncmp(argv[argn], "-floyd", 3) )
            floyd = TRUE;
        else if ( 0 == strncmp(argv[argn], "-nofs", 5) ||
                  0 == strncmp(argv[argn], "-nofloyd", 5) ||
                  0 == strncmp(argv[argn], "-ordered", 3) )
            floyd = FALSE;
        else if (0 == strcmp(argv[argn], "-iebug"))
            ie_bug = TRUE;
        else if (0 == strncmp(argv[argn], "-force", 2))
            force = TRUE;
        else if (0 == strncmp(argv[argn], "-noforce", 4))
            force = FALSE;
        else if ( 0 == strcmp(argv[argn], "-verbose") ||
                  0 == strcmp(argv[argn], "-v") ||
                  0 == strncmp(argv[argn], "-noquiet", 4) )
            verbose = TRUE;
        else if ( 0 == strncmp(argv[argn], "-noverbose", 4) ||
                  0 == strncmp(argv[argn], "-quiet", 2) )
            verbose = FALSE;

        else if ( 0 == strcmp(argv[argn], "-version")) {
            puts(PNGQUANT_VERSION);
            return SUCCESS;
        } else if ( 0 == strcmp(argv[argn], "-h") || 0 == strcmp(argv[argn], "--help")) {
            print_full_version(stdout);
            print_usage(stdout);
            return SUCCESS;
        } else if (0 == strcmp(argv[argn], "-ext")) {
            ++argn;
            if (argn == argc) {
                print_usage(stderr);
                return MISSING_ARGUMENT;
            }
            newext = argv[argn];
        }
        else if (0 == strcmp(argv[argn], "-s") ||
                 0 == strcmp(argv[argn], "-speed")) {
            ++argn;
            if (argn == argc) {
                print_usage(stderr);
                return MISSING_ARGUMENT;
            }
            speed_tradeoff = atoi(argv[argn]);
        }
        else {
            print_usage(stderr);
            return MISSING_ARGUMENT;
        }
        ++argn;
    }

    if (argn == argc) {
        print_full_version(stderr);
        print_usage(stderr);
        return MISSING_ARGUMENT;
    }
    if (sscanf(argv[argn], "%d", &reqcolors) != 1) {
        reqcolors = 256; argn--;
    }
    if (reqcolors <= 1) {
        fputs("number of colors must be greater than 1\n", stderr);
        return INVALID_ARGUMENT;
    }
    if (reqcolors > 256) {
        fputs("number of colors cannot be more than 256\n", stderr);
        return INVALID_ARGUMENT;
    }
    if (speed_tradeoff < 1 || speed_tradeoff > 10) {
        fputs("speed should be between 1 (slow) and 10 (fast)\n", stderr);
        return INVALID_ARGUMENT;
    }
    ++argn;

    if (newext == NULL) {
        newext = floyd? "-ie-fs8.png" : "-ie-or8.png";
        if (!ie_bug) newext += 3; /* skip "-ie" */
    }

    if ( argn == argc || 0==strcmp(argv[argn],"-")) {
        using_stdin = TRUE;
        filename = "stdin";
    } else {
        filename = argv[argn];
        ++argn;
    }


    /*=============================  MAIN LOOP  =============================*/

    while (argn <= argc) {
        int retval;

        verbose_printf("%s:\n", filename);

        read_info input_image = {{0}};
        write_info output_image = {{0}};
        retval = read_image(filename,using_stdin,&input_image);

        if (!retval) {
            verbose_printf("  read file corrected for gamma %2.1f\n", 1.0/input_image.gamma);

            retval = pngquant(&input_image, &output_image, floyd, reqcolors, ie_bug, speed_tradeoff);
        }

        /* now we're done with the INPUT data and row_pointers, so free 'em */
        if (input_image.rgba_data) {
            free(input_image.rgba_data);
        }
        if (input_image.row_pointers) {
            free(input_image.row_pointers);
        }

        if (!retval) {
            verbose_printf("  writing %d-color image\n", output_image.num_palette);

            retval = write_image(&output_image,filename,newext,force,using_stdin);
        }

        if (output_image.indexed_data) {
            free(output_image.indexed_data);
        }
        if (output_image.row_pointers) {
            free(output_image.row_pointers);
        }

        if (retval) {
            latest_error = retval;
            ++error_count;
        }
        ++file_count;

        verbose_printf("\n");

        filename = argv[argn];
        ++argn;
    }

    /*=======================================================================*/


    if (error_count)
        verbose_printf("There were errors quantizing %d file%s out of a"
          " total of %d file%s.\n",
          error_count, (error_count == 1)? "" : "s",
          file_count, (file_count == 1)? "" : "s");
    else
        verbose_printf("No errors detected while quantizing %d image%s.\n",
          file_count, (file_count == 1)? "" : "s");

    return latest_error;
}

static int compare_popularity(const void *ch1, const void *ch2)
{
    const float v1 = ((const colormap_item*)ch1)->popularity;
    const float v2 = ((const colormap_item*)ch2)->popularity;
    return v1-v2;
}

void sort_palette(write_info *output_image, colormap *map)
{
    assert(map); assert(output_image);

    /*
    ** Step 3.5 [GRR]: remap the palette colors so that all entries with
    ** the maximal alpha value (i.e., fully opaque) are at the end and can
    ** therefore be omitted from the tRNS chunk.
    */

    verbose_printf("  eliminating opaque tRNS-chunk entries...");

    /* move transparent colors to the beginning to shrink trns chunk */
    int num_transparent=0;
    for(int i=0; i < map->colors; i++)
    {
        rgb_pixel px = to_rgb(output_image->gamma, map->palette[i].acolor);
        if (px.a != 255) {
            if (i != num_transparent) {
                const colormap_item tmp = map->palette[num_transparent];
                map->palette[num_transparent] = map->palette[i];
                map->palette[i] = tmp;
                i--;
            }
            num_transparent++;
        }
    }

    verbose_printf("%d entr%s transparent\n", num_transparent, (num_transparent == 1)? "y" : "ies");

    /* colors sorted by popularity make pngs slightly more compressible
     * opaque and transparent are sorted separately
     */
    qsort(map->palette, num_transparent, sizeof(map->palette[0]), compare_popularity);
    qsort(map->palette+num_transparent, map->colors-num_transparent, sizeof(map->palette[0]), compare_popularity);

    output_image->num_palette = map->colors;
    output_image->num_trans = num_transparent;
}

void set_palette(write_info *output_image, const colormap *map)
{
    for (int x = 0; x < map->colors; ++x) {
        rgb_pixel px = to_rgb(output_image->gamma, map->palette[x].acolor);
        map->palette[x].acolor = to_f(output_image->gamma, px); /* saves rounding error introduced by to_rgb, which makes remapping & dithering more accurate */

        output_image->palette[x].red   = px.r;
        output_image->palette[x].green = px.g;
        output_image->palette[x].blue  = px.b;
        output_image->trans[x]         = px.a;
    }
}

float remap_to_palette(read_info *input_image, write_info *output_image, colormap *map, float min_opaque_val, int ie_bug)
{
    rgb_pixel **input_pixels = (rgb_pixel **)input_image->row_pointers;
    unsigned char **row_pointers = output_image->row_pointers;
    int rows = input_image->height, cols = input_image->width;
    double gamma = input_image->gamma;

    int remapped_pixels=0;
    float remapping_error=0;

    int transparent_ind = best_color_index((f_pixel){0,0,0,0}, map, min_opaque_val, NULL);

    f_pixel average_color[map->colors];
    float average_color_count[map->colors];
    viter_init(map, average_color, average_color_count, NULL, NULL);

    for (int row = 0; row < rows; ++row) {
        for(int col = 0; col < cols; ++col) {

            f_pixel px = to_f(gamma, input_pixels[row][col]);
            int match;

            if (px.a < 1.0/256.0) {
                match = transparent_ind;
            } else {
                float diff;
                match = best_color_index(px, map,min_opaque_val, &diff);

                remapped_pixels++;
                remapping_error += diff;
            }

            row_pointers[row][col] = match;

            viter_update_color(px, 1.0, map, match, average_color, average_color_count, NULL, NULL);
        }
    }

    viter_finalize(map, average_color, average_color_count);

    return remapping_error / MAX(1,remapped_pixels);
}

/**
  Uses edge/noise map to apply dithering only to flat areas. Dithering on edges creates jagged lines, and noisy areas are "naturally" dithered.
 */
void remap_to_palette_floyd(read_info *input_image, write_info *output_image, const colormap *map, float min_opaque_val, int ie_bug, const float *edge_map)
{
    rgb_pixel **input_pixels = (rgb_pixel **)input_image->row_pointers;
    unsigned char **row_pointers = output_image->row_pointers;
    int rows = input_image->height, cols = input_image->width;
    double gamma = input_image->gamma;

    const colormap_item *acolormap = map->palette;

    int ind=0;
    int transparent_ind = best_color_index((f_pixel){0,0,0,0}, map, min_opaque_val, NULL);

    f_pixel *restrict thiserr = NULL;
    f_pixel *restrict nexterr = NULL;
    float sr=0, sg=0, sb=0, sa=0;
    int fs_direction = 1;

    /* Initialize Floyd-Steinberg error vectors. */
    thiserr = malloc((cols + 2) * sizeof(*thiserr));
    nexterr = malloc((cols + 2) * sizeof(*thiserr));
    srand(12345); /* deterministic dithering is better for comparing results */

    for (int col = 0; col < cols + 2; ++col) {
        const double rand_max = RAND_MAX;
        thiserr[col].r = ((double)rand() - rand_max/2.0)/rand_max/255.0;
        thiserr[col].g = ((double)rand() - rand_max/2.0)/rand_max/255.0;
        thiserr[col].b = ((double)rand() - rand_max/2.0)/rand_max/255.0;
        thiserr[col].a = ((double)rand() - rand_max/2.0)/rand_max/255.0;
    }

    for (int row = 0; row < rows; ++row) {
        memset(nexterr, 0, (cols + 2) * sizeof(*nexterr));

        int col = (fs_direction) ? 0 : (cols - 1);

        do {
            f_pixel px = to_f(gamma, input_pixels[row][col]);

            float dither_level = edge_map ? edge_map[row*cols + col] : 0.9;

            /* Use Floyd-Steinberg errors to adjust actual color. */
            sr = px.r + thiserr[col + 1].r * dither_level;
            sg = px.g + thiserr[col + 1].g * dither_level;
            sb = px.b + thiserr[col + 1].b * dither_level;
            sa = px.a + thiserr[col + 1].a * dither_level;

            if (sr < 0) sr = 0;
            else if (sr > 1) sr = 1;
            if (sg < 0) sg = 0;
            else if (sg > 1) sg = 1;
            if (sb < 0) sb = 0;
            else if (sb > 1) sb = 1;
            if (sa < 0) sa = 0;
            /* when fighting IE bug, dithering must not make opaque areas transparent */
            else if (sa > 1 || (ie_bug && px.a > 255.0/256.0)) sa = 1;

            if (sa < 1.0/256.0) {
                ind = transparent_ind;
            } else {
                ind = best_color_index((f_pixel){.r=sr, .g=sg, .b=sb, .a=sa}, map, min_opaque_val, NULL);
            }

            row_pointers[row][col] = ind;

            f_pixel xp = acolormap[ind].acolor;
            f_pixel err = {
                .r = (sr - xp.r),
                .g = (sg - xp.g),
                .b = (sb - xp.b),
                .a = (sa - xp.a),
            };

            // If dithering error is crazy high, don't propagate it that much
            // This prevents crazy geen pixels popping out of the blue (or red or black! ;)
            if (err.r*err.r + err.g*err.g + err.b*err.b + err.a*err.a > 8.f/256.f) {
                dither_level *= 0.5;
            }

            float colorimp = (3.0f + acolormap[ind].acolor.a)/4.0f * dither_level;
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

            if (fs_direction) {
                ++col;
                if (col >= cols) break;
            } else {
                --col;
                if (col < 0) break;
            }
        }
        while(1);

        f_pixel *temperr;
        temperr = thiserr;
        thiserr = nexterr;
        nexterr = temperr;
        fs_direction = !fs_direction;
    }
}

/* build the output filename from the input name by inserting "-fs8" or
 * "-or8" before the ".png" extension (or by appending that plus ".png" if
 * there isn't any extension), then make sure it doesn't exist already */
char *add_filename_extension(const char *filename, const char *newext)
{
    int x = strlen(filename);

    char* outname = malloc(x+4+strlen(newext)+1);

    strncpy(outname, filename, x);
    if (strncmp(outname+x-4, ".png", 4) == 0)
        strcpy(outname+x-4, newext);
    else
        strcpy(outname+x, newext);

    return outname;
}

static void set_binary_mode(FILE *fp)
{
#if defined(MSDOS) || defined(FLEXOS) || defined(OS2) || defined(WIN32)
#if (defined(__HIGHC__) && !defined(FLEXOS))
    setmode(fp, _BINARY);
#else
    setmode(fp == stdout ? 1 : 0, O_BINARY);
#endif
#endif
}

pngquant_error write_image(write_info *output_image,const char *filename,const char *newext,int force,int using_stdin)
{
    FILE *outfile;
    if (using_stdin) {
        set_binary_mode(stdout);
        outfile = stdout;
    } else {
        char *outname = add_filename_extension(filename,newext);

        if (!force) {
            if ((outfile = fopen(outname, "rb")) != NULL) {
                fprintf(stderr, "  error:  %s exists; not overwriting\n", outname);
                fclose(outfile);
                free(outname);
                return NOT_OVERWRITING_ERROR;
            }
        }
        if ((outfile = fopen(outname, "wb")) == NULL) {
            fprintf(stderr, "  error:  cannot open %s for writing\n", outname);
            free(outname);
            return CANT_WRITE_ERROR;
        }
        free(outname);
    }

    pngquant_error retval = rwpng_write_image_init(outfile, output_image);
    if (retval) {
        fprintf(stderr, "  rwpng_write_image_init() error\n");
        if (!using_stdin)
            fclose(outfile);
        return retval;
    }

    /* write entire interlaced palette PNG */
    retval = rwpng_write_image_whole(output_image);

    if (!using_stdin)
        fclose(outfile);

    /* now we're done with the OUTPUT data and row_pointers, too */
    return retval;
}

hist *histogram(read_info *input_image, int reqcolors, int speed_tradeoff, const float *importance_map)
{
    hist *hist;
    int ignorebits=0;
    const rgb_pixel *const *input_pixels = (const rgb_pixel *const *)input_image->row_pointers;
    int cols = input_image->width, rows = input_image->height;
    double gamma = input_image->gamma;
    assert(gamma > 0);

   /*
    ** Step 2: attempt to make a histogram of the colors, unclustered.
    ** If at first we don't succeed, increase ignorebits to increase color
    ** coherence and try again.
    */

    if (speed_tradeoff > 7) ignorebits++;
    int maxcolors = (1<<17) + (1<<18)*(10-speed_tradeoff);

    verbose_printf("  making histogram...");
    for (; ;) {

        hist = pam_computeacolorhist(input_pixels, cols, rows, gamma, maxcolors, ignorebits, importance_map);
        if (hist) break;

        ignorebits++;
        verbose_printf("too many colors!\n  scaling colors to improve clustering...");
    }

    verbose_printf("%d colors found\n", hist->size);
    return hist;
}

float modify_alpha(read_info *input_image, int ie_bug)
{
    /* IE6 makes colors with even slightest transparency completely transparent,
       thus to improve situation in IE, make colors that are less than ~10% transparent
       completely opaque */

    rgb_pixel **input_pixels = (rgb_pixel **)input_image->row_pointers;
    rgb_pixel *pP;
    int rows= input_image->height, cols = input_image->width;
    double gamma = input_image->gamma;
    float min_opaque_val, almost_opaque_val;

    if (ie_bug) {
        min_opaque_val = 238.0/256.0; /* rest of the code uses min_opaque_val rather than checking for ie_bug */
        almost_opaque_val = min_opaque_val * 169.0/256.0;

        verbose_printf("  Working around IE6 bug by making image less transparent...\n");
    } else {
        min_opaque_val = almost_opaque_val = 1;
    }

    for(int row = 0; row < rows; ++row) {
        pP = input_pixels[row];
        for(int col = 0; col < cols; ++col, ++pP) {
            f_pixel px = to_f(gamma, *pP);

#ifndef NDEBUG
            rgb_pixel rgbcheck = to_rgb(gamma, px);


            if (pP->a && (pP->r != rgbcheck.r || pP->g != rgbcheck.g || pP->b != rgbcheck.b || pP->a != rgbcheck.a)) {
                fprintf(stderr, "Conversion error: expected %d,%d,%d,%d got %d,%d,%d,%d\n",
                        pP->r,pP->g,pP->b,pP->a, rgbcheck.r,rgbcheck.g,rgbcheck.b,rgbcheck.a);
                return -1;
            }
#endif
            /* set all completely transparent colors to black */
            if (!pP->a) {
                *pP = (rgb_pixel){0,0,0,pP->a};
            }
            /* ie bug: to avoid visible step caused by forced opaqueness, linearily raise opaqueness of almost-opaque colors */
            else if (pP->a < 255 && px.a > almost_opaque_val) {
                assert((min_opaque_val-almost_opaque_val)>0);

                float al = almost_opaque_val + (px.a-almost_opaque_val) * (1-almost_opaque_val) / (min_opaque_val-almost_opaque_val);
                if (al > 1) al = 1;
                px.a = al;
                pP->a = to_rgb(gamma, px).a;
            }
        }
    }

    return min_opaque_val;
}

pngquant_error read_image(const char *filename, int using_stdin, read_info *input_image_p)
{
    FILE *infile;

    if (using_stdin) {
        set_binary_mode(stdin);
        infile = stdin;
    } else if ((infile = fopen(filename, "rb")) == NULL) {
        fprintf(stderr, "  error:  cannot open %s for reading\n", filename);
        return READ_ERROR;
    }

    /*
     ** Step 1: read in the alpha-channel image.
     */
    /* GRR:  returns RGBA (4 channels), 8 bps */
    pngquant_error retval = rwpng_read_image(infile, input_image_p);

    if (!using_stdin)
        fclose(infile);

    if (retval) {
        fprintf(stderr, "  rwpng_read_image() error\n");
        return retval;
    }

    return SUCCESS;
}

/**
 Builds two maps:
    noise - approximation of areas with high-frequency noise, except straight edges. 1=flat, 0=noisy.
    edges - noise map including all edges
 */
void contrast_maps(const rgb_pixel*const apixels[], int cols, int rows, double gamma, float **noiseP, float **edgesP)
{
    float *noise = malloc(sizeof(float)*cols*rows);
    float *tmp = malloc(sizeof(float)*cols*rows);
    float *edges = malloc(sizeof(float)*cols*rows);

    for (int j=0; j < rows; j++) {
        f_pixel prev, curr = to_f(gamma, apixels[j][0]), next=curr;
        for (int i=0; i < cols; i++) {
            prev=curr;
            curr=next;
            next = to_f(gamma, apixels[j][MIN(cols-1,i+1)]);

            float a = fabsf(prev.a+next.a - curr.a*2.f),
            r = fabsf(prev.r+next.r - curr.r*2.f),
            g = fabsf(prev.g+next.g - curr.g*2.f),
            b = fabsf(prev.b+next.b - curr.b*2.f);

            f_pixel nextl = to_f(gamma, apixels[MAX(0,j-1)][i]);
            f_pixel prevl = to_f(gamma, apixels[MIN(rows-1,j+1)][i]);

            float a1 = fabsf(prevl.a+nextl.a - curr.a*2.f),
            r1 = fabsf(prevl.r+nextl.r - curr.r*2.f),
            g1 = fabsf(prevl.g+nextl.g - curr.g*2.f),
            b1 = fabsf(prevl.b+nextl.b - curr.b*2.f);

            float horiz = MAX(MAX(a,r),MAX(g,b));
            float vert = MAX(MAX(a1,r1),MAX(g1,b1));
            float edge = MAX(horiz,vert);
            float z = edge - fabs(horiz-vert)*.5;
            z = 1.f - MAX(z,MIN(horiz,vert));
            z *= z;
            z *= z;

            noise[j*cols+i] = z;
            edges[j*cols+i] = 1.f-edge;
        }
    }

    max3(noise, tmp, cols, rows);
    max3(tmp, noise, cols, rows);

    blur(noise, tmp, noise, cols, rows, 3);

    max3(noise, tmp, cols, rows);

    min3(tmp, noise, cols, rows);
    min3(noise, tmp, cols, rows);
    min3(tmp, noise, cols, rows);

    min3(edges, tmp, cols, rows);
    max3(tmp, edges, cols, rows);
    for(int i=0; i < cols*rows; i++) edges[i] = MIN(noise[i], edges[i]);

    free(tmp);

    *noiseP = noise;
    *edgesP = edges;
}

/**
 * Builds map of neighbor pixels mapped to the same palette entry
 *
 * For efficiency/simplicity it mainly looks for same consecutive pixels horizontally
 * and peeks 1 pixel above/below. Full 2d algorithm doesn't improve it significantly.
 * Correct flood fill doesn't have visually good properties.
 */
void update_dither_map(write_info *output_image, float *edges)
{
    const int width = output_image->width;
    const int height = output_image->height;
    const unsigned char *pixels = output_image->indexed_data;

    for(int row=0; row < height; row++)
    {
        unsigned char lastpixel = pixels[row*width];
        int lastcol=0;
        for(int col=1; col < width; col++)
        {
            unsigned char px = pixels[row*width + col];

            if (px != lastpixel || col == width-1) {
                float neighbor_count = 3.f + col-lastcol;

                int i=lastcol;
                while(i < col) {
                    if (row > 0) {
                        unsigned char pixelabove = pixels[(row-1)*width + i];
                        if (pixelabove == lastpixel) neighbor_count += 1.f;
                    }
                    if (row < height-1) {
                        unsigned char pixelbelow = pixels[(row+1)*width + i];
                        if (pixelbelow == lastpixel) neighbor_count += 1.f;
                    }
                    i++;
                }

                while(lastcol < col) {
                    edges[row*width + lastcol++] *= 1.f - 3.f/neighbor_count;
                }
                lastpixel = px;
            }
        }
    }
}

/**
 Repeats mediancut with different histogram weights to find palette with minimum error.

 feedback_loop_trials controls how long the search will take. < 0 skips the iteration.
 */
static colormap *find_best_palette(hist *hist, int reqcolors, float min_opaque_val, int feedback_loop_trials, float *palette_error_p)
{
    hist_item *achv = hist->achv;
    colormap *acolormap = NULL;
    float least_error;
    const double percent = (double)(feedback_loop_trials>0?feedback_loop_trials:1)/100.0;

    do
    {
        verbose_printf("  selecting colors");

        colormap *newmap = mediancut(hist, min_opaque_val, reqcolors);

        if (feedback_loop_trials <= 0) {
            verbose_printf("\n");
            return newmap;
        }

        verbose_printf("...");

        f_pixel average_color[newmap->colors], base_color[newmap->colors];
        float average_color_count[newmap->colors], base_color_count[newmap->colors];
        viter_init(newmap, average_color,average_color_count,base_color,base_color_count);

        float total_error = 0;
        for(int i=0; i < hist->size; i++) {
            float diff;
            int match = best_color_index(achv[i].acolor, newmap, min_opaque_val, &diff);
                assert(diff >= 0);
            assert(achv[i].perceptual_weight > 0);
            total_error += diff * achv[i].perceptual_weight;

            viter_update_color(achv[i].acolor, achv[i].perceptual_weight, newmap, match,
                               average_color,average_color_count,base_color,base_color_count);

            achv[i].adjusted_weight = (achv[i].perceptual_weight+achv[i].adjusted_weight) * (1.0+sqrtf(diff));
        }

        if (!acolormap || total_error < least_error) {
            if (acolormap) pam_freecolormap(acolormap);
            acolormap = newmap;

            viter_finalize(acolormap, average_color,average_color_count);

            least_error = total_error;
            feedback_loop_trials -= 1; // asymptotic improvement could make it go on forever
        } else {
            feedback_loop_trials -= 6;
            if (total_error > least_error*4) feedback_loop_trials -= 3;
            pam_freecolormap(newmap);
        }

        verbose_printf("%d%%\n",100-MAX(0,(int)(feedback_loop_trials/percent)));
    }
    while(feedback_loop_trials > 0);

    float total_weight = 0;
    for(int i=0; i < hist->size; i++) total_weight += achv[i].perceptual_weight;

    *palette_error_p = least_error / total_weight;
    return acolormap;
}

pngquant_error pngquant(read_info *input_image, write_info *output_image, int floyd, int reqcolors, int ie_bug, int speed_tradeoff)
{
    float min_opaque_val;

    min_opaque_val = modify_alpha(input_image,ie_bug);
    assert(min_opaque_val>0);

    float *noise = NULL, *edges = NULL;
    if (speed_tradeoff < 8) {
        contrast_maps((const rgb_pixel**)input_image->row_pointers, input_image->width, input_image->height, input_image->gamma,
                   &noise, &edges);
    }

    // histogram uses noise contrast map for importance. Color accuracy in noisy areas is not very important.
    // noise map does not include edges to avoid ruining anti-aliasing
    hist *hist = histogram(input_image, reqcolors, speed_tradeoff, noise); if (noise) free(noise);

    float palette_error = -1;
    colormap *acolormap = find_best_palette(hist, reqcolors, min_opaque_val, 56-9*speed_tradeoff, &palette_error);

    verbose_printf("  moving colormap towards local minimum\n");

    int iterations = MAX(5-speed_tradeoff,0); iterations *= iterations;
    for(int i=0; i < iterations; i++) {
        palette_error = viter_do_interation(hist, acolormap, min_opaque_val);
    }

    pam_freeacolorhist(hist);

    output_image->width = input_image->width;
    output_image->height = input_image->height;
    output_image->gamma = 0.45455;

    /*
    ** Step 3.7 [GRR]: allocate memory for the entire indexed image
    */

    output_image->indexed_data = malloc(output_image->height * output_image->width);
    output_image->row_pointers = malloc(output_image->height * sizeof(output_image->row_pointers[0]));

    if (!output_image->indexed_data || !output_image->row_pointers) {
        fprintf(stderr, "  insufficient memory for indexed data and/or row pointers\n");
        return OUT_OF_MEMORY_ERROR;
    }

    for (int row = 0;  row < output_image->height;  ++row) {
        output_image->row_pointers[row] = output_image->indexed_data + row*output_image->width;
    }

    // tRNS, etc.
    sort_palette(output_image, acolormap);

    /*
     ** Step 4: map the colors in the image to their closest match in the
     ** new colormap, and write 'em out.
     */
    verbose_printf("  mapping image to new colors...");

    int use_dither_map = floyd && edges && speed_tradeoff < 6;

    if (!floyd || use_dither_map) {
        // If no dithering is required, that's the final remapping.
        // If dithering (with dither map) is required, this image is used to find areas that require dithering
        float remapping_error = remap_to_palette(input_image, output_image, acolormap, min_opaque_val, ie_bug);

        if (use_dither_map) {
            update_dither_map(output_image, edges);
        }

        // remapping error from dithered image is absurd, so always non-dithered value is used
        verbose_printf("MSE=%.3f", remapping_error*256.0f);
    }

    // remapping above was the last chance to do voronoi iteration, hence the final palette is set after remapping
    set_palette(output_image, acolormap);

    if (floyd) {
        remap_to_palette_floyd(input_image, output_image, acolormap, min_opaque_val, ie_bug, edges);
    }

    verbose_printf("\n");

    if (edges) free(edges);
    pam_freecolormap(acolormap);

    return SUCCESS;
}


