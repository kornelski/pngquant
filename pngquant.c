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

#define PNGQUANT_VERSION "1.4.3 (August 2011)"

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
#include <assert.h>
#include <stdarg.h>
#ifdef WIN32        /* defined in Makefile.w32 (or use _MSC_VER for MSVC) */
#  include <fcntl.h>    /* O_BINARY */
#  include <io.h>   /* setmode() */
#endif

#include <stddef.h>

#include "png.h"    /* libpng header; includes zlib.h */
#include "rwpng.h"  /* typedefs, common macros, public prototypes */
#include "pam.h"

#define index_of_channel(ch) (offsetof(f_pixel,ch)/sizeof(float))

typedef struct box *box_vector;
struct box {
    int ind;
    int colors;
    int sum;
    float weight;
};

pngquant_error pngquant(read_info *input_image, write_info *output_image, int floyd, int reqcolors, int ie_bug, int speed_tradeoff);
pngquant_error read_image(const char *filename, int using_stdin, read_info *input_image_p);
pngquant_error write_image(write_info *output_image,const char *filename,const char *newext,int force,int using_stdin);

static hist_item *mediancut(hist_item achv[], float min_opaque_val, int colors, int reqcolors);
typedef int (*comparefunc)(const void *, const void *);
static int weightedcompare_r(const void *ch1, const void *ch2);
static int weightedcompare_g(const void *ch1, const void *ch2);
static int weightedcompare_b(const void *ch1, const void *ch2);
static int weightedcompare_a(const void *ch1, const void *ch2);

static f_pixel averagepixels(int indx, int clrs, hist_item achv[], float min_opaque_val);


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

        read_info input_image = {0};
        write_info output_image = {0};
        retval = read_image(filename,using_stdin,&input_image);

        if (!retval) {
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

static int popularity(const void *ch1, const void *ch2)
{
    const float v1 = ((const hist_item*)ch1)->value;
    const float v2 = ((const hist_item*)ch2)->value;
    return v1-v2;
}

void set_palette(write_info *output_image, int newcolors, hist_item acolormap[])
{
    assert(acolormap); assert(output_image);

    /*
    ** Step 3.4 [GRR]: set the bit-depth appropriately, given the actual
    ** number of colors that will be used in the output image.
    */

    verbose_printf("  writing %d-color image\n", newcolors);

    /*
    ** Step 3.5 [GRR]: remap the palette colors so that all entries with
    ** the maximal alpha value (i.e., fully opaque) are at the end and can
    ** therefore be omitted from the tRNS chunk.
    */

    verbose_printf("  remapping colormap to eliminate opaque tRNS-chunk entries...");

    /* move transparent colors to the beginning to shrink trns chunk */
    int num_transparent=0;
    for(int i=0; i < newcolors; i++)
    {
        rgb_pixel px = to_rgb(output_image->gamma, acolormap[i].acolor);
        if (px.a != 255) {
            if (i != num_transparent) {
                hist_item tmp = acolormap[num_transparent];
                acolormap[num_transparent] = acolormap[i];
                acolormap[i] = tmp;
                i--;
            }
            num_transparent++;
        }
    }

    verbose_printf("%d entr%s transparent\n", num_transparent, (num_transparent == 1)? "y" : "ies");

        /* colors sorted by popularity make pngs slightly more compressible
         * opaque and transparent are sorted separately
         */
    qsort(acolormap, num_transparent, sizeof(acolormap[0]), popularity);
    qsort(acolormap+num_transparent, newcolors-num_transparent, sizeof(acolormap[0]), popularity);

    output_image->num_palette = newcolors;
    output_image->num_trans = num_transparent;

    for (int x = 0; x < newcolors; ++x) {
        rgb_pixel px = to_rgb(output_image->gamma, acolormap[x].acolor);
        acolormap[x].acolor = to_f(output_image->gamma, px); /* saves rounding error introduced by to_rgb, which makes remapping & dithering more accurate */

        output_image->palette[x].red   = px.r;
        output_image->palette[x].green = px.g;
        output_image->palette[x].blue  = px.b;
        output_image->trans[x]         = px.a;
    }
}

static int best_color_index(f_pixel px, hist_item* acolormap, int numcolors, float min_opaque_val)
{
    int ind=0;

    float dist = colordifference(px,acolormap[0].acolor);

    for(int i = 1; i < numcolors; i++) {
        float newdist = colordifference(px,acolormap[i].acolor);

        if (newdist < dist) {

            /* penalty for making holes in IE */
            if (px.a > min_opaque_val && acolormap[i].acolor.a < 1) {
                if (newdist+1.0 > dist) continue;
            }

            ind = i;
            dist = newdist;
        }
    }
    return ind;
}

void remap_to_palette(read_info *input_image, write_info *output_image, int floyd, float min_opaque_val, int ie_bug, int newcolors, hist_item acolormap[])
{
    int ind=0;
    int transparent_ind = best_color_index((f_pixel){0,0,0,0}, acolormap, newcolors, min_opaque_val);

    rgb_pixel *pP;
    rgb_pixel **input_pixels = (rgb_pixel **)input_image->row_pointers;
    unsigned char **row_pointers = output_image->row_pointers;
    unsigned char *outrow, *pQ;
    int rows = input_image->height, cols = input_image->width;
    double gamma = input_image->gamma;

    f_pixel *thiserr = NULL;
    f_pixel *nexterr = NULL;
    float sr=0, sg=0, sb=0, sa=0, err;
    int fs_direction = 0;

    if (floyd) {
        /* Initialize Floyd-Steinberg error vectors. */
        thiserr = malloc((cols + 2) * sizeof(*thiserr));
        nexterr = malloc((cols + 2) * sizeof(*thiserr));
        srandom(12345); /** deterministic dithering is better for comparing results */

        for (int col = 0; col < cols + 2; ++col) {
            const double rand_max = RAND_MAX;
            thiserr[col].r = ((double)random() - rand_max/2.0)/rand_max/255.0;
            thiserr[col].g = ((double)random() - rand_max/2.0)/rand_max/255.0;
            thiserr[col].b = ((double)random() - rand_max/2.0)/rand_max/255.0;
            thiserr[col].a = ((double)random() - rand_max/2.0)/rand_max/255.0;
        }
        fs_direction = 1;
    }
    for (int row = 0; row < rows; ++row) {
        int col;
        outrow = row_pointers[row];

        if (floyd) {
            for (col = 0; col < cols + 2; ++col) {
                nexterr[col] = (f_pixel){0,0,0,0};
            }
        }

        if ((!floyd) || fs_direction) {
            col = 0;
            pP = input_pixels[row];
            pQ = outrow;
        } else {
            col = cols - 1;
            pP = &(input_pixels[row][col]);
            pQ = &(outrow[col]);
        }

        do {
            f_pixel px = to_f(gamma, *pP);

            if (floyd) {
                /* Use Floyd-Steinberg errors to adjust actual color. */
                sr = px.r + thiserr[col + 1].r;
                sg = px.g + thiserr[col + 1].g;
                sb = px.b + thiserr[col + 1].b;
                sa = px.a + thiserr[col + 1].a;

                if (sr < 0) sr = 0;
                else if (sr > 1) sr = 1;
                if (sg < 0) sg = 0;
                else if (sg > 1) sg = 1;
                if (sb < 0) sb = 0;
                else if (sb > 1) sb = 1;
                if (sa < 0) sa = 0;
                /* when fighting IE bug, dithering must not make opaque areas transparent */
                else if (sa > 1 || (ie_bug && px.a > 255.0/256.0)) sa = 1;

                px = (f_pixel){.r=sr, .g=sg, .b=sb, .a=sa};
            }

            if (px.a < 1.0/256.0) {
                ind = transparent_ind;
            } else {
                ind = best_color_index(px,acolormap,newcolors,min_opaque_val);
            }

            if (floyd) {
                float colorimp = (1.0/256.0) + acolormap[ind].acolor.a;
                f_pixel px = acolormap[ind].acolor;

                /* Propagate Floyd-Steinberg error terms. */
                if (fs_direction) {
                    err = (sr - px.r) * colorimp;
                    thiserr[col + 2].r += (err * 7.0f) / 16.0f;
                    nexterr[col    ].r += (err * 3.0f) / 16.0f;
                    nexterr[col + 1].r += (err * 5.0f) / 16.0f;
                    nexterr[col + 2].r += (err       ) / 16.0f;
                    err = (sg - px.g) * colorimp;
                    thiserr[col + 2].g += (err * 7.0f) / 16.0f;
                    nexterr[col    ].g += (err * 3.0f) / 16.0f;
                    nexterr[col + 1].g += (err * 5.0f) / 16.0f;
                    nexterr[col + 2].g += (err       ) / 16.0f;
                    err = (sb - px.b) * colorimp;
                    thiserr[col + 2].b += (err * 7.0f) / 16.0f;
                    nexterr[col    ].b += (err * 3.0f) / 16.0f;
                    nexterr[col + 1].b += (err * 5.0f) / 16.0f;
                    nexterr[col + 2].b += (err       ) / 16.0f;
                    err = (sa - px.a);
                    thiserr[col + 2].a += (err * 7.0f) / 16.0f;
                    nexterr[col    ].a += (err * 3.0f) / 16.0f;
                    nexterr[col + 1].a += (err * 5.0f) / 16.0f;
                    nexterr[col + 2].a += (err       ) / 16.0f;
                } else {
                    err = (sr - px.r) * colorimp;
                    thiserr[col    ].r += (err * 7.0f) / 16.0f;
                    nexterr[col + 2].r += (err * 3.0f) / 16.0f;
                    nexterr[col + 1].r += (err * 5.0f) / 16.0f;
                    nexterr[col    ].r += (err       ) / 16.0f;
                    err = (sg - px.g) * colorimp;
                    thiserr[col    ].g += (err * 7.0f) / 16.0f;
                    nexterr[col + 2].g += (err * 3.0f) / 16.0f;
                    nexterr[col + 1].g += (err * 5.0f) / 16.0f;
                    nexterr[col    ].g += (err       ) / 16.0f;
                    err = (sb - px.b) * colorimp;
                    thiserr[col    ].b += (err * 7.0f) / 16.0f;
                    nexterr[col + 2].b += (err * 3.0f) / 16.0f;
                    nexterr[col + 1].b += (err * 5.0f) / 16.0f;
                    nexterr[col    ].b += (err       ) / 16.0f;
                    err = (sa - px.a);
                    thiserr[col    ].a += (err * 7.0f) / 16.0f;
                    nexterr[col + 2].a += (err * 3.0f) / 16.0f;
                    nexterr[col + 1].a += (err * 5.0f) / 16.0f;
                    nexterr[col    ].a += (err       ) / 16.0f;
                }
            }

            *pQ = ind;

            if ((!floyd) || fs_direction) {
                ++col;
                ++pP;
                ++pQ;
                if (col >= cols) break;
            } else {
                --col;
                --pP;
                --pQ;
                if (col < 0) break;
            }
        }
        while(1);

        if (floyd) {
            f_pixel *temperr;
            temperr = thiserr;
            thiserr = nexterr;
            nexterr = temperr;
            fs_direction = !fs_direction;
        }
    }
}

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
        outfile = stdout;   /* GRR:  see comment above about fdopen() */
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

hist_item *histogram(read_info *input_image, int reqcolors, int *colors, int speed_tradeoff)
{
    hist_item *achv;
    int ignorebits=0;
    rgb_pixel **input_pixels = (rgb_pixel **)input_image->row_pointers;
    int cols = input_image->width, rows = input_image->height;
    double gamma = input_image->gamma;
    assert(gamma > 0); assert(colors);

   /*
    ** Step 2: attempt to make a histogram of the colors, unclustered.
    ** If at first we don't succeed, increase ignorebits to increase color
    ** coherence and try again.
    */

    if (speed_tradeoff > 7) ignorebits++;
    int maxcolors = (1<<16) + (1<<17)*(10-speed_tradeoff);

    verbose_printf("  making histogram...");
    for (; ;) {

        achv = pam_computeacolorhist(input_pixels, cols, rows, gamma, maxcolors, ignorebits, colors);
        if (achv) break;

        ignorebits++;
        verbose_printf("too many colors!\n  scaling colors to improve clustering...\n");
    }

    verbose_printf("%d colors found\n", *colors);
    return achv;
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

    /* build the output filename from the input name by inserting "-fs8" or
     * "-or8" before the ".png" extension (or by appending that plus ".png" if
     * there isn't any extension), then make sure it doesn't exist already */

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

pngquant_error pngquant(read_info *input_image, write_info *output_image, int floyd, int reqcolors, int ie_bug, int speed_tradeoff)
{
    float min_opaque_val;

    verbose_printf("  Reading file corrected for gamma %2.1f\n", 1.0/input_image->gamma);

    min_opaque_val = modify_alpha(input_image,ie_bug);
    assert(min_opaque_val>0);

    int colors=0;
    hist_item *achv = histogram(input_image, reqcolors, &colors, speed_tradeoff);
    int newcolors = MIN(colors, reqcolors);

    // backup numbers in achv
    for(int i=0; i < colors; i++) {
        achv[i].num_pixels = achv[i].value;
    }

    hist_item *acolormap = NULL;
    float least_error = -1;
    int feedback_loop_trials = 9*(6-speed_tradeoff);
    const double percent = (double)(feedback_loop_trials>0?feedback_loop_trials:1)/100.0;
    do
    {
        verbose_printf("  selecting colors");

        hist_item *newmap = mediancut(achv, min_opaque_val, colors, newcolors);

        verbose_printf("...");

        float total_error=0;

        if (feedback_loop_trials || acolormap) {
            for(int i=0; i < colors; i++) {

                int match = best_color_index(achv[i].acolor, newmap, newcolors, min_opaque_val);
                float diff = colordifference(achv[i].acolor, newmap[match].acolor);
                assert(diff >= 0);
                assert(achv[i].num_pixels > 0);
                total_error += diff * achv[i].num_pixels;

                achv[i].value = (achv[i].num_pixels+achv[i].value) * (1.0+sqrtf(diff));
            }
        }

        if (total_error < least_error || !acolormap) {
            if (acolormap) free(acolormap);
            acolormap = newmap;
            least_error = total_error;
            feedback_loop_trials -= 1; // asymptotic improvement could make it go on forever
        } else {
            feedback_loop_trials -= 6;
            if (total_error > least_error*4) feedback_loop_trials -= 3;
            free(newmap);
        }

        verbose_printf(" %d%%\n",100-MAX(0,(int)(feedback_loop_trials/percent)));
    }
    while(feedback_loop_trials > 0);

    pam_freeacolorhist(achv);

    output_image->width = input_image->width;
    output_image->height = input_image->height;
    output_image->gamma = 0.45455;

    set_palette(output_image, newcolors, acolormap);

    /*
    ** Step 3.7 [GRR]: allocate memory for the entire indexed image
    ** note that rwpng_info.row_pointers
    ** is still in use via apixels (INPUT data).
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


    /*
    ** Step 4: map the colors in the image to their closest match in the
    ** new colormap, and write 'em out.
    */
    verbose_printf("  mapping image to new colors...\n" );

    remap_to_palette(input_image,output_image,floyd,min_opaque_val,ie_bug,newcolors,acolormap);

    free(acolormap);

    return SUCCESS;
}



typedef struct {
    int chan; float variance;
} channelvariance;

static int comparevariance(const void *ch1, const void *ch2)
{
    return ((channelvariance*)ch1)->variance > ((channelvariance*)ch2)->variance ? -1 :
          (((channelvariance*)ch1)->variance < ((channelvariance*)ch2)->variance ? 1 : 0);
};

static channelvariance channel_sort_order[4];

inline static int weightedcompare_other(const float *restrict c1p, const float *restrict c2p)
{
    // other channels are sorted backwards
    if (c1p[channel_sort_order[1].chan] > c2p[channel_sort_order[1].chan]) return -1;
    if (c1p[channel_sort_order[1].chan] < c2p[channel_sort_order[1].chan]) return 1;

    if (c1p[channel_sort_order[2].chan] > c2p[channel_sort_order[2].chan]) return -1;
    if (c1p[channel_sort_order[2].chan] < c2p[channel_sort_order[2].chan]) return 1;

    if (c1p[channel_sort_order[3].chan] > c2p[channel_sort_order[3].chan]) return -1;
    if (c1p[channel_sort_order[3].chan] < c2p[channel_sort_order[3].chan]) return 1;

    return 0;
}

/** these are specialised functions to make first comparison faster without lookup in channel_sort_order[] */
static int weightedcompare_r(const void *ch1, const void *ch2)
{
    const float *c1p = (const float *)&((hist_item*)ch1)->acolor;
    const float *c2p = (const float *)&((hist_item*)ch2)->acolor;

    if (c1p[index_of_channel(r)] > c2p[index_of_channel(r)]) return 1;
    if (c1p[index_of_channel(r)] < c2p[index_of_channel(r)]) return -1;

    return weightedcompare_other(c1p, c2p);
}

static int weightedcompare_g(const void *ch1, const void *ch2)
{
    const float *c1p = (const float *)&((hist_item*)ch1)->acolor;
    const float *c2p = (const float *)&((hist_item*)ch2)->acolor;

    if (c1p[index_of_channel(g)] > c2p[index_of_channel(g)]) return 1;
    if (c1p[index_of_channel(g)] < c2p[index_of_channel(g)]) return -1;

    return weightedcompare_other(c1p, c2p);
}

static int weightedcompare_b(const void *ch1, const void *ch2)
{
    const float *c1p = (const float *)&((hist_item*)ch1)->acolor;
    const float *c2p = (const float *)&((hist_item*)ch2)->acolor;

    if (c1p[index_of_channel(b)] > c2p[index_of_channel(b)]) return 1;
    if (c1p[index_of_channel(b)] < c2p[index_of_channel(b)]) return -1;

    return weightedcompare_other(c1p, c2p);
}

static int weightedcompare_a(const void *ch1, const void *ch2)
{
    const float *c1p = (const float *)&((hist_item*)ch1)->acolor;
    const float *c2p = (const float *)&((hist_item*)ch2)->acolor;

    if (c1p[index_of_channel(a)] > c2p[index_of_channel(a)]) return 1;
    if (c1p[index_of_channel(a)] < c2p[index_of_channel(a)]) return -1;

    return weightedcompare_other(c1p, c2p);
}

/*
** Here is the fun part, the median-cut colormap generator.  This is based
** on Paul Heckbert's paper, "Color Image Quantization for Frame Buffer
** Display," SIGGRAPH 1982 Proceedings, page 297.
*/

static hist_item *mediancut(hist_item achv[], float min_opaque_val, int colors, int newcolors)
{
    box_vector bv = calloc(newcolors, sizeof(struct box));
    hist_item *acolormap = calloc(newcolors, sizeof(hist_item));
    if (!bv || !acolormap) {
        return NULL;
    }

    /*
    ** Set up the initial box.
    */
    bv[0].ind = 0;
    bv[0].colors = colors;
    bv[0].weight = 1.0;
    for(int i=0; i < colors; i++) bv[0].sum += achv[i].value;

    int boxes = 1;

    /*
    ** Main loop: split boxes until we have enough.
    */
    while (boxes < newcolors) {

        /*
        ** Find the best splittable box.
        */
        int bi=-1; float maxsum=0;
        for (int i=0; i < boxes; i++) {
            if (bv[i].colors < 2) continue;

            if (bv[i].sum*bv[i].weight > maxsum) {
                maxsum = bv[i].sum*bv[i].weight;
                bi = i;
            }
        }
        if (bi < 0)
            break;        /* ran out of colors! */

        int indx = bv[bi].ind;
        int clrs = bv[bi].colors;

        /* compute variance of channels */
        f_pixel mean = averagepixels(bv[bi].ind, bv[bi].colors, achv, min_opaque_val);

        f_pixel variance = (f_pixel){0,0,0,0};
        for (int i = 0; i < clrs; ++i) {
            f_pixel px = achv[indx + i].acolor;
            variance.a += (mean.a - px.a)*(mean.a - px.a);
            variance.r += (mean.r - px.r)*(mean.r - px.r);
            variance.g += (mean.g - px.g)*(mean.g - px.g);
            variance.b += (mean.b - px.b)*(mean.b - px.b);
        }

        /*
        ** Sort dimensions by their variance, and then sort colors first by dimension with highest variance
        */

        channel_sort_order[0] = (channelvariance){index_of_channel(r), variance.r};
        channel_sort_order[1] = (channelvariance){index_of_channel(g), variance.g};
        channel_sort_order[2] = (channelvariance){index_of_channel(b), variance.b};
        channel_sort_order[3] = (channelvariance){index_of_channel(a), variance.a};

        qsort(channel_sort_order, 4, sizeof(channel_sort_order[0]), comparevariance);


        comparefunc comp;
             if (channel_sort_order[0].chan == index_of_channel(r)) comp = weightedcompare_r;
        else if (channel_sort_order[0].chan == index_of_channel(g)) comp = weightedcompare_g;
        else if (channel_sort_order[0].chan == index_of_channel(b)) comp = weightedcompare_b;
        else comp = weightedcompare_a;

        qsort(&(achv[indx]), clrs, sizeof(achv[0]), comp);

        /*
            Classic implementation tries to get even number of colors or pixels in each subdivision.

            Here, instead of popularity I use (sqrt(popularity)*variance) metric.
            Each subdivision balances number of pixels (popular colors) and low variance -
            boxes can be large if they have similar colors. Later boxes with high variance
            will be more likely to be split.

            Median used as expected value gives much better results than mean.
        */

        f_pixel median = averagepixels(indx+(clrs-1)/2, clrs&1 ? 1 : 2, achv, min_opaque_val);

        int lowersum = 0;
        float halfvar = 0, lowervar = 0;
        for(int i=0; i < clrs -1; i++) {
            halfvar += sqrtf(colordifference(median, achv[indx+i].acolor)) * sqrtf(achv[indx+i].value);
        }
        halfvar /= 2.0f;

        int break_at;
        for (break_at = 0; break_at < clrs - 1; ++break_at) {
            if (lowervar >= halfvar)
                break;

            lowervar += sqrtf(colordifference(median, achv[indx+break_at].acolor)) * sqrtf(achv[indx+break_at].value);
            lowersum += achv[indx + break_at].value;
        }

        /*
        ** Split the box. Sum*weight is then used to find "largest" box to split.
        */
        int sm = bv[bi].sum;
        bv[bi].colors = break_at;
        bv[bi].sum = lowersum;
        bv[bi].weight = powf(colordifference(mean, averagepixels(bv[bi].ind, bv[bi].colors, achv, min_opaque_val)),0.25f);
        bv[boxes].ind = indx + break_at;
        bv[boxes].colors = clrs - break_at;
        bv[boxes].sum = sm - lowersum;
        bv[boxes].weight = powf(colordifference(mean, averagepixels(bv[boxes].ind, bv[boxes].colors, achv, min_opaque_val)),0.25f);
        ++boxes;
    }

    /*
    ** Ok, we've got enough boxes.  Now choose a representative color for
    ** each box.  There are a number of possible ways to make this choice.
    ** One would be to choose the center of the box; this ignores any structure
    ** within the boxes.  Another method would be to average all the colors in
    ** the box - this is the method specified in Heckbert's paper.  A third
    ** method is to average all the pixels in the box.  You can switch which
    ** method is used by switching the commenting on the REP_ defines at
    ** the beginning of this source file.
    */
    for (int bi = 0; bi < boxes; ++bi) {
        acolormap[bi].acolor = averagepixels(bv[bi].ind, bv[bi].colors, achv, min_opaque_val);

        for(int i=0; i < bv[bi].colors; i++) {
            /* increase histogram popularity by difference from the final color (this is used as part of feedback loop) */
            achv[bv[bi].ind + i].value *= 1.0 + sqrt(colordifference(acolormap[bi].acolor, achv[bv[bi].ind + i].acolor))/2.0;

            /* store total color popularity */
            acolormap[bi].value += achv[bv[bi].ind + i].value;
        }
    }

    /*
    ** All done.
    */
    return acolormap;
}

static f_pixel averagepixels(int indx, int clrs, hist_item achv[], float min_opaque_val)
{
    float r = 0, g = 0, b = 0, a = 0, sum = 0;
    float maxa = 0;
    int i;

    for (i = 0; i < clrs; ++i) {
        float weight = 1.0f;
        f_pixel px = achv[indx + i].acolor;
        float tmp;

        /* give more weight to colors that are further away from average
            this is intended to prevent desaturation of images and fading of whites
         */
        tmp = (0.5f - px.r);
        weight += tmp*tmp;
        tmp = (0.5f - px.g);
        weight += tmp*tmp;
        tmp = (0.5f - px.b);
        weight += tmp*tmp;

        weight *= achv[indx + i].value;
        sum += weight;

        r += px.r * weight;
        g += px.g * weight;
        b += px.b * weight;
        a += px.a * weight;

        /* find if there are opaque colors, in case we're supposed to preserve opacity exactly (ie_bug) */
        if (px.a > maxa) maxa = px.a;
    }

    /* Colors are in premultiplied alpha colorspace, so they'll blend OK
       even if different opacities were mixed together */
    if (!sum) sum=1;
    a /= sum;
    r /= sum;
    g /= sum;
    b /= sum;


    /** if there was at least one completely opaque color, "round" final color to opaque */
    if (a >= min_opaque_val && maxa >= (255.0/256.0)) a = 1;

    return (f_pixel){.r=r, .g=g, .b=b, .a=a};
}

