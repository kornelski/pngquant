/* pngquant.c - quantize the colors in an alphamap down to a specified number
**
** Copyright (C) 1989, 1991 by Jef Poskanzer.
** Copyright (C) 1997, 2000, 2002 by Greg Roelofs; based on an idea by
**                                Stefan Schneider.
** Copyright (C) 2009 by Kornel Lesinski.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

/* GRR TO DO:  set sBIT flag appropriately for maxval-scaled images */
/* GRR TO DO:  "original file size" and "quantized file size" if verbose? */
/* GRR TO DO:  add option to preserve background color (if any) exactly */
/* GRR TO DO:  add mapfile support, but cleanly (build palette in main()) */
/* GRR TO DO:  default to 256 colors if number not specified on command line */
/* GRR TO DO:  support 16 bps without down-conversion */
/* GRR TO DO:  replace PBMPLUS mem-management routines? */
/* GRR TO DO:  if all samples are gray and image is opaque and sample depth
                would be no bigger than palette and user didn't explicitly
                specify a mapfile, switch to grayscale */
/* GRR TO DO:  if all samples are 0 or maxval, eliminate gAMA chunk (rwpng.c) */


#define PNGQUANT_VERSION "1.1.4dev (2011)"

#define PNGQUANT_USAGE "\
   usage:  pngquant [options] [ncolors] [pngfile [pngfile ...]]\n\
                    [options] -map mapfile [pngfile [pngfile ...]]\n\
   options:\n\
      -force         overwrite existing output files\n\
      -ext new.png   set custom extension for output filename\n\
      -nofs          disable dithering (synonyms: -nofloyd, -ordered)\n\
      -verbose       print status messages (synonyms: -noquiet)\n\
      -iebug         increase opacity to work around Internet Explorer 6 bug\n\
\n\
   Quantizes one or more 32-bit RGBA PNGs to 8-bit (or smaller) RGBA-palette\n\
   PNGs using Floyd-Steinberg diffusion dithering (unless disabled).\n\
   The output filename is the same as the input name except that\n\
   it ends in \"-fs8.png\", \"-or8.png\" or your custom extension (unless the\n\
   input is stdin, in which case the quantized image will go to stdout).\n\
   The default behavior if the output file exists is to skip the conversion;\n\
   use -force to overwrite.\n\
   NOTE:  the -map option is NOT YET SUPPORTED.\n"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#ifdef WIN32        /* defined in Makefile.w32 (or use _MSC_VER for MSVC) */
#  include <fcntl.h>    /* O_BINARY */
#  include <io.h>   /* setmode() */
#endif

#include <math.h>

#include "png.h"    /* libpng header; includes zlib.h */
#include "rwpng.h"  /* typedefs, common macros, public prototypes */
#include "pam.h"

typedef unsigned char   uch;
typedef unsigned short  ush;
typedef png_uint_32     ulg;

static mainprog_info rwpng_info;

#define FNMAX      1024     /* max filename length */
#define MAXCOLORS  (32767*8)

/* #define REP_CENTER_BOX */
/* #define REP_AVERAGE_COLORS */
#define REP_AVERAGE_PIXELS

typedef struct box *box_vector;
struct box {
    int ind;
    int colors;
    int sum;
};

typedef enum {
    READ_ERROR = 2,
    TOO_MANY_COLORS = 5,
    NOT_OVERWRITING_ERROR = 15,
    CANT_WRITE_ERROR = 16,
    OUT_OF_MEMORY_ERROR = 17,
    INTERNAL_LOGIC_ERROR = 18,
} pngquant_error;

static pngquant_error pngquant(char *filename, char *newext, int floyd, int force, int verbose,
                               int using_stdin, int reqcolors, int ie_bug);

static acolorhist_vector mediancut(acolorhist_vector achv, int colors, int sum, pixval min_opaque_val, int newcolors);
static int redcompare (const void *ch1, const void *ch2);
static int greencompare (const void *ch1, const void *ch2);
static int bluecompare (const void *ch1, const void *ch2);
static int alphacompare (const void *ch1, const void *ch2);
static int sumcompare (const void *b1, const void *b2);

static double colorimportance(double alpha);

static char *pm_allocrow (int cols, int size);

static f_pixel centerbox(int indx, int clrs, acolorhist_vector achv);
static f_pixel averagecolors(int indx, int clrs, acolorhist_vector achv);
static f_pixel averagepixels(int indx, int clrs, acolorhist_vector achv, pixval min_opaque_val);


int main(int argc, char *argv[])
{
    int argn;
    int reqcolors;
    int floyd = TRUE;
    int force = FALSE;
    int ie_bug = FALSE;
    int verbose = FALSE;
    int using_stdin = FALSE;
    int latest_error=0, error_count=0, file_count=0;
    char *filename, *newext = NULL;
    char *pq_usage = PNGQUANT_USAGE;

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
        else if ( 0 == strncmp(argv[argn], "-verbose", 2) ||
                  0 == strncmp(argv[argn], "-noquiet", 4) )
            verbose = TRUE;
        else if ( 0 == strncmp(argv[argn], "-noverbose", 4) ||
                  0 == strncmp(argv[argn], "-quiet", 2) )
            verbose = FALSE;

        else if (0 == strcmp(argv[argn], "-ext")) {
            ++argn;
            if (argn == argc) {
                fprintf(stderr, "%s", pq_usage);
                fflush(stderr);
                return 1;
            }
            newext = argv[argn];
        }
        else {
            fprintf(stderr, "pngquant, version %s, by Greg Roelofs, Kornel Lesinski.\n",
              PNGQUANT_VERSION);
            rwpng_version_info();
            fputs("\n", stderr);
            fputs(pq_usage, stderr);
            fflush(stderr);
            return 1;
        }
        ++argn;
    }

    if (argn == argc) {
        fprintf(stderr, "pngquant, version %s, by Greg Roelofs, Kornel Lesinski.\n",
          PNGQUANT_VERSION);
        rwpng_version_info();
        fputs("\n", stderr);
        fputs(pq_usage, stderr);
        fflush(stderr);
        return 1;
    }
    if (sscanf(argv[argn], "%d", &reqcolors) != 1) {
        reqcolors = 256; argn--;
    }
    if (reqcolors <= 1) {
        fputs("number of colors must be greater than 1\n", stderr);
        fflush(stderr);
        return 4;
    }
    if (reqcolors > 256) {
        fputs("number of colors cannot be more than 256\n", stderr);
        fflush(stderr);
        return 4;
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

        if (verbose) {
            fprintf(stderr, "%s:\n", filename);
            fflush(stderr);
        }

        retval = pngquant(filename, newext, floyd, force, verbose, using_stdin,
          reqcolors, ie_bug);

        if (retval) {
            latest_error = retval;
            ++error_count;
        }
        ++file_count;

        if (verbose) {
            fprintf(stderr, "\n");
            fflush(stderr);
        }

        filename = argv[argn];
        ++argn;
    }

    /*=======================================================================*/


    if (verbose) {
        if (error_count)
            fprintf(stderr, "There were errors quantizing %d file%s out of a"
              " total of %d file%s.\n",
              error_count, (error_count == 1)? "" : "s",
              file_count, (file_count == 1)? "" : "s");
        else
            fprintf(stderr, "No errors detected while quantizing %d image%s.\n",
              file_count, (file_count == 1)? "" : "s");
        fflush(stderr);
    }

    return latest_error;
}

int set_palette(int newcolors,int verbose,int* remap,acolorhist_vector acolormap)
{
    /*
    ** Step 3.4 [GRR]: set the bit-depth appropriately, given the actual
    ** number of colors that will be used in the output image.
    */

    int top_idx,bot_idx;

    if (newcolors <= 2)
        rwpng_info.sample_depth = 1;
    else if (newcolors <= 4)
        rwpng_info.sample_depth = 2;
    else if (newcolors <= 16)
        rwpng_info.sample_depth = 4;
    else
        rwpng_info.sample_depth = 8;

    if (verbose) {
        fprintf(stderr, "  writing %d-bit colormapped image\n",
          rwpng_info.sample_depth);
        fflush(stderr);
    }

    /*
    ** Step 3.5 [GRR]: remap the palette colors so that all entries with
    ** the maximal alpha value (i.e., fully opaque) are at the end and can
    ** therefore be omitted from the tRNS chunk.  Note that the ordering of
    ** opaque entries is reversed from how Step 3 arranged them--not that
    ** this should matter to anyone.
    */

    if (verbose) {
        fprintf(stderr,
          "  remapping colormap to eliminate opaque tRNS-chunk entries...");
        fflush(stderr);
    }
    int x=0;
    for (top_idx = newcolors-1, bot_idx = 0;  x < newcolors;  ++x) {
        rgb_pixel px = to_rgb(acolormap[x].acolor);

        if (px.a == 255)
            remap[x] = top_idx--;
        else
            remap[x] = bot_idx++;
    }
    if (verbose) {
        fprintf(stderr, "%d entr%s left\n", bot_idx,
          (bot_idx == 1)? "y" : "ies");
        fflush(stderr);
    }

    /* sanity check:  top and bottom indices should have just crossed paths */
    if (bot_idx != top_idx + 1) {
        return INTERNAL_LOGIC_ERROR;
    }

    rwpng_info.num_palette = newcolors;
    rwpng_info.num_trans = bot_idx;

    /* GRR TO DO:  if bot_idx == 0, check whether all RGB samples are gray
                   and if so, whether grayscale sample_depth would be same
                   => skip following palette section and go grayscale */


    /*
    ** Step 3.6 [GRR]: (Technically, the actual remapping happens in here)
    */

    for (x = 0; x < newcolors; ++x) {
        rgb_pixel px = to_rgb(acolormap[x].acolor);
        acolormap[x].acolor = to_f(px); /* saves rounding error introduced by to_rgb, which makes remapping & dithering more accurate */

        rwpng_info.palette[remap[x]].red   = px.r;
        rwpng_info.palette[remap[x]].green = px.g;
        rwpng_info.palette[remap[x]].blue  = px.b;
        rwpng_info.trans[remap[x]]         = px.a;
    }

    return 0;
}

pngquant_error pngquant(char *filename, char *newext, int floyd, int force, int verbose, int using_stdin, int reqcolors, int ie_bug)
{
    FILE *infile, *outfile;
    rgb_pixel **input_pixels;
    rgb_pixel *pP;
    int col, limitcol;
    int ind;
    uch *pQ, *outrow, **row_pointers=NULL;
    ulg rows, cols;
    pixval min_opaque_val, almost_opaque_val;
    int ignorebits=0;
    acolorhist_vector achv, acolormap=NULL;
    acolorhash_table acht;
    int row;
    int colors;
    int newcolors = 0;
    int fs_direction = 0;
    int x;
    char outname[FNMAX];


    /* can't do much if we don't have an input file...but don't reopen stdin */

    if (using_stdin) {

#if defined(MSDOS) || defined(FLEXOS) || defined(OS2) || defined(WIN32)
#if (defined(__HIGHC__) && !defined(FLEXOS))
        setmode(stdin, _BINARY);
#else
        setmode(0, O_BINARY);
#endif
#endif
        /* GRR:  Reportedly "some buggy C libraries require BOTH the setmode()
         *       call AND fdopen() in binary mode," but it's not clear which
         *       ones or that any of them are still in use as of 2000.  Until
         *       someone reports a specific problem, we're skipping the fdopen
         *       part...  */
        infile = stdin;

    } else if ((infile = fopen(filename, "rb")) == NULL) {
        fprintf(stderr, "  error:  cannot open %s for reading\n", filename);
        fflush(stderr);
        return READ_ERROR;
    }


    /* build the output filename from the input name by inserting "-fs8" or
     * "-or8" before the ".png" extension (or by appending that plus ".png" if
     * there isn't any extension), then make sure it doesn't exist already */

    if (using_stdin) {

#if defined(MSDOS) || defined(FLEXOS) || defined(OS2) || defined(WIN32)
#if (defined(__HIGHC__) && !defined(FLEXOS))
        setmode(stdout, _BINARY);
#else
        setmode(1, O_BINARY);
#endif
#endif
        outfile = stdout;   /* GRR:  see comment above about fdopen() */

    } else {
        x = strlen(filename);
        if (x > FNMAX-strlen(newext)-1) {
            fprintf(stderr,
              "  warning:  base filename [%s] will be truncated\n", filename);
            fflush(stderr);
            x = FNMAX-strlen(newext)-1;
        }
        strncpy(outname, filename, x);
        if (strncmp(outname+x-4, ".png", 4) == 0)
            strcpy(outname+x-4, newext);
        else
            strcpy(outname+x, newext);
        if (!force) {
            if ((outfile = fopen(outname, "rb")) != NULL) {
                fprintf(stderr, "  error:  %s exists; not overwriting\n",
                  outname);
                fflush(stderr);
                fclose(outfile);
                return NOT_OVERWRITING_ERROR;
            }
        }
        if ((outfile = fopen(outname, "wb")) == NULL) {
            fprintf(stderr, "  error:  cannot open %s for writing\n", outname);
            fflush(stderr);
            return CANT_WRITE_ERROR;
        }
    }



    /*
    ** Step 1: read in the alpha-channel image.
    */
    /* GRR:  returns RGBA (4 channels), 8 bps */
    rwpng_read_image(infile, &rwpng_info);

    if (!using_stdin)
        fclose(infile);

    if (rwpng_info.retval) {
        fprintf(stderr, "  rwpng_read_image() error\n");
        fflush(stderr);
        if (!using_stdin)
            fclose(outfile);
        return rwpng_info.retval;
    }

    /* NOTE:  rgba_data and row_pointers are allocated but not freed in
     *        rwpng_read_image() */
    input_pixels = (rgb_pixel **)rwpng_info.row_pointers;
    cols = rwpng_info.width;
    rows = rwpng_info.height;
    /* channels = rwpng_info.channels; */

    /* IE6 makes colors with even slightest transparency completely transparent,
       thus to improve situation in IE, make colors that are less than ~10% transparent
       completely opaque */
    if (ie_bug) {
        min_opaque_val = 255 * 15 / 16; /* rest of the code uses min_opaque_val rather than checking for ie_bug */
        almost_opaque_val = min_opaque_val * 2 / 3;

        if (verbose) {
            fprintf(stderr, "  Working around IE6 bug by making image less transparent...\n");
            fflush(stderr);
        }
    } else {
        min_opaque_val = almost_opaque_val = 255;
    }

    for (row = 0; (ulg)row < rows; ++row)
        for (col = 0, pP = input_pixels[row]; (ulg)col < cols; ++col, ++pP) {
            /* set all completely transparent colors to black */
            if (!pP->a) {
                *pP = (rgb_pixel){0,0,0,pP->a};
            }
            /* ie bug: to avoid visible step caused by forced opaqueness, linearily raise opaqueness of almost-opaque colors */
            else if (pP->a < 255 && pP->a > almost_opaque_val) {
                assert((min_opaque_val-almost_opaque_val)>0);

                int al = almost_opaque_val + (pP->a-almost_opaque_val) * (255-almost_opaque_val) / (min_opaque_val-almost_opaque_val);
                if (al > 255) al = 255;
                pP->a = al;
            }
        }

    /* ie bug: despite increased opaqueness in the picture, color reduction could still produce
        non-opaque colors. to prevent that, set a treshold (it'll be used when remapping too) */
    if (min_opaque_val != 255) {
        min_opaque_val = 255*15/16;
    }

   /*
    ** Step 2: attempt to make a histogram of the colors, unclustered.
    ** If at first we don't succeed, increase ignorebits to increase color
    ** coherence and try again.
    */
    for (; ;) {
        if (verbose) {
            fprintf(stderr, "  making histogram...");
            fflush(stderr);
        }
        achv = pam_computeacolorhist(input_pixels, cols, rows, MAXCOLORS, ignorebits, &colors);
        if (achv != (acolorhist_vector) 0)
            break;

        ignorebits++;

        if (verbose) {
            fprintf(stderr, "too many colors!\n");
            fprintf(stderr, "  scaling colors to improve clustering...\n");
            fflush(stderr);
        }
    }
    if (verbose) {
        fprintf(stderr, "%d colors found\n", colors);
        fflush(stderr);
    }
    newcolors = MIN(colors, reqcolors);

    /*
    ** Step 3: apply median-cut to histogram, making the new acolormap.
    */
    if (verbose && colors > reqcolors) {
        fprintf(stderr, "  choosing %d colors...\n", newcolors);
        fflush(stderr);
    }
    acolormap = mediancut(achv, colors, rows * cols, min_opaque_val, newcolors);
    pam_freeacolorhist(achv);

    int remap[256];

    if (set_palette(newcolors,verbose,remap,acolormap)) {
        return INTERNAL_LOGIC_ERROR;
    }


    /*
    ** Step 3.7 [GRR]: allocate memory for either a single row (non-
    ** interlaced -> progressive write) or the entire indexed image
    ** (interlaced -> all at once); note that rwpng_info.row_pointers
    ** is still in use via apixels (INPUT data).
    */

    if (rwpng_info.interlaced) {
        if ((rwpng_info.indexed_data = malloc(rows * cols)) != NULL) {
            if ((row_pointers = (uch **)malloc(rows * sizeof(uch *))) != NULL) {
                for (row = 0;  (ulg)row < rows;  ++row)
                    row_pointers[row] = rwpng_info.indexed_data + row*cols;
            }
        }
    } else {
        rwpng_info.indexed_data = malloc(cols);
    }

    if (rwpng_info.indexed_data == NULL ||
        (rwpng_info.interlaced && row_pointers == NULL)) {
        fprintf(stderr,
          "  insufficient memory for indexed data and/or row pointers\n");
        fflush(stderr);
        if (rwpng_info.row_pointers)
            free(rwpng_info.row_pointers);
        if (rwpng_info.rgba_data)
            free(rwpng_info.rgba_data);
        if (rwpng_info.indexed_data)
            free(rwpng_info.indexed_data);
        if (!using_stdin)
            fclose(outfile);
        return OUT_OF_MEMORY_ERROR;
    }



    /*
    ** Step 4: map the colors in the image to their closest match in the
    ** new colormap, and write 'em out.
    */
    if (verbose) {
        fprintf(stderr, "  mapping image to new colors...\n" );
        fflush(stderr);
    }
    acht = pam_allocacolorhash();
    if (!acht) return OUT_OF_MEMORY_ERROR;

    if (rwpng_write_image_init(outfile, &rwpng_info) != 0) {
        fprintf(stderr, "  rwpng_write_image_init() error\n");
        fflush(stderr);
        if (rwpng_info.rgba_data)
            free(rwpng_info.rgba_data);
        if (rwpng_info.row_pointers)
            free(rwpng_info.row_pointers);
        if (rwpng_info.indexed_data)
            free(rwpng_info.indexed_data);
        if (row_pointers)
            free(row_pointers);
        if (!using_stdin)
            fclose(outfile);
        return rwpng_info.retval;
    }

    f_pixel *thiserr = NULL;
    f_pixel *nexterr = NULL;
    f_pixel *temperr;
    float sr=0, sg=0, sb=0, sa=0, err;

    if (floyd) {
        /* Initialize Floyd-Steinberg error vectors. */
        thiserr = malloc((cols + 2) * sizeof(*thiserr));
        nexterr = malloc((cols + 2) * sizeof(*thiserr));
        srandom(12345); /** deterministic dithering is better for comparing results */
        for (col = 0; (ulg)col < cols + 2; ++col) {
            thiserr[col].r = (float)random() / ((float)RAND_MAX/2.0) - 1.0;
            thiserr[col].g = (float)random() / ((float)RAND_MAX/2.0) - 1.0;
            thiserr[col].b = (float)random() / ((float)RAND_MAX/2.0) - 1.0;
            thiserr[col].a = (float)random() / ((float)RAND_MAX/2.0) - 1.0;
            /* (random errors in [-1 .. 1]) */
        }
        fs_direction = 1;
    }
    for (row = 0; (ulg)row < rows; ++row) {
        outrow = rwpng_info.interlaced? row_pointers[row] :
                                        rwpng_info.indexed_data;
        if (floyd)
            for (col = 0; (ulg)col < cols + 2; ++col)
                nexterr[col].r = nexterr[col].g =
                nexterr[col].b = nexterr[col].a = 0;
        if ((!floyd) || fs_direction) {
            col = 0;
            limitcol = cols;
            pP = input_pixels[row];
            pQ = outrow;
        } else {
            col = cols - 1;
            limitcol = -1;
            pP = &(input_pixels[row][col]);
            pQ = &(outrow[col]);
        }



        do {
            f_pixel px = to_f(*pP);

            if (floyd) {
                /* Use Floyd-Steinberg errors to adjust actual color. */
                sr = px.r + thiserr[col + 1].r;
                sg = px.g + thiserr[col + 1].g;
                sb = px.b + thiserr[col + 1].b;
                sa = px.a + thiserr[col + 1].a;

                if (sr < 0) sr = 0;
                else if (sr > 255) sr = 255;
                if (sg < 0) sg = 0;
                else if (sg > 255) sg = 255;
                if (sb < 0) sb = 0;
                else if (sb > 255) sb = 255;
                if (sa < 0) sa = 0;
                /* when fighting IE bug, dithering must not make opaque areas transparent */
                else if (sa > 255 || (ie_bug && px.a == 255)) sa = 255;

                /* GRR 20001228:  added casts to quiet warnings; 255 DEPENDENCY */
                px = (f_pixel){sr, sg, sb, sa};
            }


            /* Check hash table to see if we have already matched this color. */
            ind = pam_lookupacolor(acht, px);

            double colorimp = colorimportance(px.a);

            if (ind == -1) {
                /* No; search acolormap for closest match. */
                int i;
                double a1, r1, g1, b1, r2, g2, b2, a2;
                double dist = 1<<30, newdist;

                a1 = px.a;
                r1 = px.r;
                g1 = px.g;
                b1 = px.b;
                /* a1 read few lines earlier */

                for (i = 0; i < newcolors; ++i) {
                    r2 = acolormap[i].acolor.r;
                    g2 = acolormap[i].acolor.g;
                    b2 = acolormap[i].acolor.b;
                    a2 = acolormap[i].acolor.a;
/* GRR POSSIBLE BUG */

                    /* 8+1 shift is /256 for colorimportance and approx /3 for 3 channels vs 1 */
                    newdist = ((a1 - a2) * (a1 - a2) * 512.0) +
                              ((r1 - r2) * (r1 - r2) * colorimp +
                               (g1 - g2) * (g1 - g2) * colorimp +
                               (b1 - b2) * (b1 - b2) * colorimp);

                    /* penalty for making holes in IE */
                    if (a1 >= min_opaque_val && a2 < 255) newdist += 255*255/64;

                    if (newdist < dist) {
                        ind = i;
                        dist = newdist;
                    }
                }

                if (pam_addtoacolorhash(acht, px, ind) < 0) {
                    if (verbose) {
                        fprintf(stderr, "  out of memory adding to hash");
                        fflush(stderr);
                    }
                    return OUT_OF_MEMORY_ERROR;
                }
            }

            if (floyd) {
                /* Propagate Floyd-Steinberg error terms. */
                if (fs_direction) {
                    err = (sr - (float)acolormap[ind].acolor.r) * colorimp/256.0;
                    thiserr[col + 2].r += (err * 7) / 16.0;
                    nexterr[col    ].r += (err * 3) / 16.0;
                    nexterr[col + 1].r += (err * 5) / 16.0;
                    nexterr[col + 2].r += (err    ) / 16.0;
                    err = (sg - (float)acolormap[ind].acolor.g) * colorimp/256.0;
                    thiserr[col + 2].g += (err * 7) / 16.0;
                    nexterr[col    ].g += (err * 3) / 16.0;
                    nexterr[col + 1].g += (err * 5) / 16.0;
                    nexterr[col + 2].g += (err    ) / 16.0;
                    err = (sb - (float)acolormap[ind].acolor.b) * colorimp/256.0;
                    thiserr[col + 2].b += (err * 7) / 16.0;
                    nexterr[col    ].b += (err * 3) / 16.0;
                    nexterr[col + 1].b += (err * 5) / 16.0;
                    nexterr[col + 2].b += (err    ) / 16.0;
                    err = (sa - (float)acolormap[ind].acolor.a);
                    thiserr[col + 2].a += (err * 7) / 16.0;
                    nexterr[col    ].a += (err * 3) / 16.0;
                    nexterr[col + 1].a += (err * 5) / 16.0;
                    nexterr[col + 2].a += (err    ) / 16.0;
                } else {
                    err = (sr - (float)acolormap[ind].acolor.r) * colorimp/256.0;
                    thiserr[col    ].r += (err * 7) / 16.0;
                    nexterr[col + 2].r += (err * 3) / 16.0;
                    nexterr[col + 1].r += (err * 5) / 16.0;
                    nexterr[col    ].r += (err    ) / 16.0;
                    err = (sg - (float)acolormap[ind].acolor.g) * colorimp/256.0;
                    thiserr[col    ].g += (err * 7) / 16.0;
                    nexterr[col + 2].g += (err * 3) / 16.0;
                    nexterr[col + 1].g += (err * 5) / 16.0;
                    nexterr[col    ].g += (err    ) / 16.0;
                    err = (sb - (float)acolormap[ind].acolor.b) * colorimp/256.0;
                    thiserr[col    ].b += (err * 7) / 16.0;
                    nexterr[col + 2].b += (err * 3) / 16.0;
                    nexterr[col + 1].b += (err * 5) / 16.0;
                    nexterr[col    ].b += (err    ) / 16.0;
                    err = (sa - (float)acolormap[ind].acolor.a);
                    thiserr[col    ].a += (err * 7) / 16.0;
                    nexterr[col + 2].a += (err * 3) / 16.0;
                    nexterr[col + 1].a += (err * 5) / 16.0;
                    nexterr[col    ].a += (err    ) / 16.0;
                }
            }

            *pQ = (uch)remap[ind];

            if ((!floyd) || fs_direction) {
                ++col;
                ++pP;
                ++pQ;
            } else {
                --col;
                --pP;
                --pQ;
            }
        }
        while (col != limitcol);

        if (floyd) {
            temperr = thiserr;
            thiserr = nexterr;
            nexterr = temperr;
            fs_direction = !fs_direction;
        }

        /* if non-interlaced PNG, write row now */
        if (!rwpng_info.interlaced)
            rwpng_write_image_row(&rwpng_info);
    }


    /* now we're done with the INPUT data and row_pointers, so free 'em */

    if (rwpng_info.rgba_data) {
        free(rwpng_info.rgba_data);
        rwpng_info.rgba_data = NULL;
    }
    if (rwpng_info.row_pointers) {
        free(rwpng_info.row_pointers);
        rwpng_info.row_pointers = NULL;
    }


    /* write entire interlaced palette PNG, or finish/flush noninterlaced one */

    if (rwpng_info.interlaced) {
        rwpng_info.row_pointers = row_pointers;   /* now for OUTPUT data */
        rwpng_write_image_whole(&rwpng_info);
    } else
        rwpng_write_image_finish(&rwpng_info);

    if (!using_stdin)
        fclose(outfile);


    /* now we're done with the OUTPUT data and row_pointers, too */

    if (rwpng_info.indexed_data) {
        free(rwpng_info.indexed_data);
        rwpng_info.indexed_data = NULL;
    }
    if (row_pointers) {
        free(row_pointers);
        rwpng_info.row_pointers = NULL;
    }


    return 0;   /* success! */
}



/*
** Here is the fun part, the median-cut colormap generator.  This is based
** on Paul Heckbert's paper, "Color Image Quantization for Frame Buffer
** Display," SIGGRAPH 1982 Proceedings, page 297.
*/

static f_pixel background;

static acolorhist_vector mediancut(acolorhist_vector achv, int colors, int sum, pixval min_opaque_val, int newcolors)
{
    acolorhist_vector acolormap;
    box_vector bv;
    int bi, i;
    int boxes;

    bv = malloc(sizeof(struct box) * newcolors);
    acolormap = calloc(newcolors, sizeof(struct acolorhist_item));
    if (!bv || !acolormap) {
        return 0;
    }

    /*
    ** Set up the initial box.
    */
    bv[0].ind = 0;
    bv[0].colors = colors;
    bv[0].sum = sum;
    boxes = 1;

    /*
    ** Main loop: split boxes until we have enough.
    */
    while (boxes < newcolors) {
        int indx, clrs;
        int sm;
        double minr, maxr, ming, mina, maxg, minb, maxb, maxa, v;
        int halfsum, lowersum;

        /*
        ** Find the first splittable box.
        */
        for (bi = 0; bi < boxes; ++bi)
            if (bv[bi].colors >= 2)
                break;
        if (bi == boxes)
            break;        /* ran out of colors! */
        indx = bv[bi].ind;
        clrs = bv[bi].colors;
        sm = bv[bi].sum;

        /*
        ** Go through the box finding the minimum and maximum of each
        ** component - the boundaries of the box.
        */

        /* colors are blended with background color, to prevent transparent colors from widening range unneccesarily */
        /* background is global - used when sorting too */
        background = averagepixels(bv[bi].ind, bv[bi].colors, achv, min_opaque_val);

        minr = maxr = achv[indx].acolor.r;
        ming = maxg = achv[indx].acolor.g;
        minb = maxb = achv[indx].acolor.b;
        mina = maxa = achv[indx].acolor.a;

        for (i = 0; i < clrs; ++i) {
            v = achv[indx + i].acolor.a;
            if (v < mina) mina = v;
            if (v > maxa) maxa = v;

            /* linear blending makes it too obsessed with accurate alpha, but the optimum unfortunately seems to depend on image */
            int al = colorimportance(255-v);
            v = (achv[indx + i].acolor.r * (256-al) + al * background.r)/256; /* 256 is deliberate */
            if (v < minr) minr = v;
            if (v > maxr) maxr = v;
            v = (achv[indx + i].acolor.g * (256-al) + al * background.g)/256;
            if (v < ming) ming = v;
            if (v > maxg) maxg = v;
            v = (achv[indx + i].acolor.b * (256-al) + al * background.b)/256;
            if (v < minb) minb = v;
            if (v > maxb) maxb = v;

        }

        /*
        ** Find the largest dimension, and sort by that component
        ** by simply comparing the range in RGB space
        */

        if (maxa - mina >= maxr - minr && maxa - mina >= maxg - ming && maxa - mina >= maxb - minb)
            qsort(
                (char*) &(achv[indx]), clrs, sizeof(struct acolorhist_item),
                alphacompare );
        else if (maxr - minr >= maxg - ming && maxr - minr >= maxb - minb)
            qsort(
                (char*) &(achv[indx]), clrs, sizeof(struct acolorhist_item),
                redcompare );
        else if (maxg - ming >= maxb - minb)
            qsort(
                (char*) &(achv[indx]), clrs, sizeof(struct acolorhist_item),
                greencompare );
        else
            qsort(
                (char*) &(achv[indx]), clrs, sizeof(struct acolorhist_item),
                bluecompare );

        /*
        ** Now find the median based on the counts, so that about half the
        ** pixels (not colors, pixels) are in each subdivision.
        */
        lowersum = achv[indx].value;
        halfsum = sm / 2;
        for (i = 1; i < clrs - 1; ++i) {
            if (lowersum >= halfsum)
                break;
            lowersum += achv[indx + i].value;
        }

        /*
        ** Split the box, and sort to bring the biggest boxes to the top.
        */
        bv[bi].colors = i;
        bv[bi].sum = lowersum;
        bv[boxes].ind = indx + i;
        bv[boxes].colors = clrs - i;
        bv[boxes].sum = sm - lowersum;
        ++boxes;
        qsort((char*) bv, boxes, sizeof(struct box), sumcompare);
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
    for (bi = 0; bi < boxes; ++bi) {
#ifdef REP_CENTER_BOX
        acolormap[bi].acolor = centerbox(bv[bi].ind, bv[bi].colors);
#endif /*REP_CENTER_BOX*/
#ifdef REP_AVERAGE_COLORS
        acolormap[bi].acolor = averagecolors(bv[bi].ind, bv[bi].colors);
#endif /*REP_AVERAGE_COLORS*/
#ifdef REP_AVERAGE_PIXELS
        acolormap[bi].acolor = averagepixels(bv[bi].ind, bv[bi].colors, achv, min_opaque_val);
#endif /*REP_AVERAGE_PIXELS*/
    }

    /*
    ** All done.
    */
    return acolormap;
}

static f_pixel centerbox(int indx, int clrs, acolorhist_vector achv)
{
    double minr, maxr, ming, maxg, minb, maxb, mina, maxa, v;

    minr = maxr = achv[indx].acolor.r;
    ming = maxg = achv[indx].acolor.g;
    minb = maxb = achv[indx].acolor.b;
    mina = maxa = achv[indx].acolor.a;
    for (int i = 1; i < clrs; ++i) {
        v = achv[indx + i].acolor.r;
        minr = MIN(minr, v);
        maxr = MAX(maxr, v);
        v = achv[indx + i].acolor.g;
        ming = MIN(ming, v);
        maxg = MAX(maxg, v);
        v = achv[indx + i].acolor.b;
        minb = MIN(minb, v);
        maxb = MAX(maxb, v);
        v = achv[indx + i].acolor.a;
        mina = MIN(mina, v);
        maxa = MAX(maxa, v);
    }

    return (f_pixel){ (minr + maxr) / 2, (ming + maxg) / 2,
                      (minb + maxb) / 2, (mina + maxa) / 2 };
}

static f_pixel averagecolors(int indx, int clrs, acolorhist_vector achv)
{
    double r = 0, g = 0, b = 0, a = 0;

    for (int i = 0; i < clrs; ++i) {
        r += achv[indx + i].acolor.r;
        g += achv[indx + i].acolor.g;
        b += achv[indx + i].acolor.b;
        a += achv[indx + i].acolor.a;
    }

    r = r / clrs;
    g = g / clrs;
    b = b / clrs;
    a = a / clrs;

    return (f_pixel){r, g, b, a};
}

static f_pixel averagepixels(int indx, int clrs, acolorhist_vector achv, pixval min_opaque_val)
{
    /* use floating-point to avoid overflow. unsigned long will suffice for small images. */
    double r = 0, g = 0, b = 0, a = 0, sum = 0, colorsum = 0;
    unsigned int maxa = 0;
    int i;

    for (i = 0; i < clrs; ++i) {
        unsigned long weight = 1;
        int tmp;

        /* give more weight to colors that are further away from average (128,128,128)
            this is intended to prevent desaturation of images and fading of whites
         */
        tmp = 128 - achv[indx + i].acolor.r;
        weight += tmp*tmp;
        tmp = 128 - achv[indx + i].acolor.g;
        weight += tmp*tmp;
        tmp = 128 - achv[indx + i].acolor.b;
        weight += tmp*tmp;

        /* find if there are opaque colors, in case we're supposed to preserve opacity exactly (ie_bug) */
        if (achv[indx + i].acolor.a > maxa) maxa = achv[indx + i].acolor.a;

        a += achv[indx + i].acolor.a * achv[indx + i].value * weight;
        sum += achv[indx + i].value * weight;

        /* blend colors proportionally to their alpha. It has minor effect and doesn't need colorimportance() */
        weight *= colorimportance(achv[indx + i].acolor.a);

        r += achv[indx + i].acolor.r * achv[indx + i].value * weight;
        g += achv[indx + i].acolor.g * achv[indx + i].value * weight;
        b += achv[indx + i].acolor.b * achv[indx + i].value * weight;
        colorsum += achv[indx + i].value * weight;
    }

    if (!colorsum) colorsum=1;
    r /= colorsum;
    if (r > 255) r = 255;        /* avoid math/rounding errors */
    g /= colorsum;
    if (g > 255) g = 255;
    b /= colorsum;
    if (b > 255) b = 255;
    a /= sum;
    if (a >= 255) a = 255;

    /** if there was at least one completely opaque color, "round" final color to opaque */
    if (a >= min_opaque_val && maxa == 255) a = 255;

    return (f_pixel){r, g, b, a};
}

static int redcompare(const void *ch1, const void *ch2)
{
    return ((int) ((acolorhist_vector)ch1)->acolor.r) -
           ((int) ((acolorhist_vector)ch2)->acolor.r);
}

static int greencompare(const void *ch1, const void *ch2)
{
    return ((int) ((acolorhist_vector)ch1)->acolor.g) -
           ((int) ((acolorhist_vector)ch2)->acolor.g);
}

static int bluecompare(const void *ch1, const void *ch2)
{
    return ((int) ((acolorhist_vector)ch1)->acolor.b) -
           ((int) ((acolorhist_vector)ch2)->acolor.b);
}

static int alphacompare(const void *ch1, const void *ch2)
{
    return (int) ((acolorhist_vector)ch1)->acolor.a -
           (int) ((acolorhist_vector)ch2)->acolor.a;
}

static int sumcompare(const void *b1, const void *b2)
{
    return ((box_vector)b2)->sum -
           ((box_vector)b1)->sum;
}

/** expects alpha in range 0-255.
 Returns importance of color in range 1-256 (off-by-one error is deliberate to allow >>8 optimisation) */
static double colorimportance(double alpha)
{
    return 256.0-(255.0-alpha)*(255.0-alpha)/256.0;
}

inline static unsigned long colordiff(rgb_pixel a, rgb_pixel b)
{
    unsigned long diff; long t;
    t = a.r - b.r;
    diff = t*t;
    t = a.g - b.g;
    diff += t*t;
    t = a.b - b.b;
    diff += t*t;

    unsigned long colorimp = 256-(255-a.a)*(255-b.a)/256;
    diff = diff * colorimp;

    t = a.a - b.a;
    diff += (t*t)<<9;

    return diff;
}

/*===========================================================================*/

