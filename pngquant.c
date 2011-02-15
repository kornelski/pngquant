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


#define PNGQUANT_VERSION "1.2b (2011)"

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

static mainprog_info rwpng_info;

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

static pngquant_error pngquant(const char *filename, const char *newext, int floyd, int force, int verbose,
                               int using_stdin, int reqcolors, int ie_bug);

static acolorhist_vector mediancut(acolorhist_vector achv, int colors, int sum, double min_opaque_val, int newcolors);
static int redcompare (const void *ch1, const void *ch2);
static int greencompare (const void *ch1, const void *ch2);
static int bluecompare (const void *ch1, const void *ch2);
static int alphacompare (const void *ch1, const void *ch2);
static int valuecompare(const void *ch1, const void *ch2);
static int sumcompare (const void *b1, const void *b2);

static float colorimportance(float alpha);

static f_pixel centerbox(int indx, int clrs, acolorhist_vector achv);
static f_pixel averagecolors(int indx, int clrs, acolorhist_vector achv);
static f_pixel averagepixels(int indx, int clrs, acolorhist_vector achv, double min_opaque_val);


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
    const char *filename, *newext = NULL;
    const char *pq_usage = PNGQUANT_USAGE;

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

        retval = pngquant(filename, newext, floyd, force, verbose, using_stdin, reqcolors, ie_bug);

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

int set_palette(int newcolors, int verbose, int* remap, acolorhist_vector acolormap)
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
        rgb_pixel px = to_rgb(rwpng_info.gamma, acolormap[x].acolor);

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
        rgb_pixel px = to_rgb(rwpng_info.gamma, acolormap[x].acolor);
        acolormap[x].acolor = to_f(rwpng_info.gamma, px); /* saves rounding error introduced by to_rgb, which makes remapping & dithering more accurate */

        rwpng_info.palette[remap[x]].red   = px.r;
        rwpng_info.palette[remap[x]].green = px.g;
        rwpng_info.palette[remap[x]].blue  = px.b;
        rwpng_info.trans[remap[x]]         = px.a;
    }

    return 0;
}

int remap_to_palette(int floyd, double min_opaque_val, int ie_bug, rgb_pixel **input_pixels, int rows, int cols, uch **row_pointers, int newcolors, int* remap, acolorhist_vector acolormap)
{
    int col;
    uch *pQ;
    rgb_pixel *pP;
    int ind=0;
    int limitcol;
    uch *outrow;
    int row;


    f_pixel *thiserr = NULL;
    f_pixel *nexterr = NULL;
    f_pixel *temperr;
    float sr=0, sg=0, sb=0, sa=0, err;
    int fs_direction = 0;

    if (floyd) {
        /* Initialize Floyd-Steinberg error vectors. */
        thiserr = malloc((cols + 2) * sizeof(*thiserr));
        nexterr = malloc((cols + 2) * sizeof(*thiserr));
        srandom(12345); /** deterministic dithering is better for comparing results */

        for (col = 0; col < cols + 2; ++col) {
            const double rand_max = RAND_MAX;
            thiserr[col].r = ((double)random() - rand_max/2.0)/rand_max/255.0;
            thiserr[col].g = ((double)random() - rand_max/2.0)/rand_max/255.0;
            thiserr[col].b = ((double)random() - rand_max/2.0)/rand_max/255.0;
            thiserr[col].a = ((double)random() - rand_max/2.0)/rand_max/255.0;
        }
        fs_direction = 1;
    }
    for (row = 0; row < rows; ++row) {
        outrow = row_pointers[row];

        if (floyd)
            for (col = 0; col < cols + 2; ++col)
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
            f_pixel px = to_f(rwpng_info.gamma, *pP);

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
                else if (sa > 1 || (ie_bug && px.a > 0.999)) sa = 1;

                px = (f_pixel){sr, sg, sb, sa};
            }


                float a1, r1, g1, b1, r2, g2, b2, a2;
            float dist = 1<<30, newdist;

                r1 = px.r;
                g1 = px.g;
                b1 = px.b;
            a1 = px.a;

            for (int i = 0; i < newcolors; ++i) {
                float colorimp = colorimportance(MAX(acolormap[i].acolor.a, px.a));

                r2 = acolormap[i].acolor.r;
                g2 = acolormap[i].acolor.g;
                b2 = acolormap[i].acolor.b;
                a2 = acolormap[i].acolor.a;

                newdist =  (a1 - a2) * (a1 - a2) +
                               (r1 - r2) * (r1 - r2) * colorimp +
                               (g1 - g2) * (g1 - g2) * colorimp +
                               (b1 - b2) * (b1 - b2) * colorimp;

                /* penalty for making holes in IE */
                if (a1 > min_opaque_val && a2 < 1) newdist += 1.0;

                if (newdist < dist) {
                    ind = i;
                    dist = newdist;
                }
            }

            if (floyd) {
                double colorimp = (1.0/256.0) + colorimportance(acolormap[ind].acolor.a);

                /* Propagate Floyd-Steinberg error terms. */
                if (fs_direction) {
                    err = (sr - acolormap[ind].acolor.r) * colorimp;
                    thiserr[col + 2].r += (err * 7.0f) / 16.0f;
                    nexterr[col    ].r += (err * 3.0f) / 16.0f;
                    nexterr[col + 1].r += (err * 5.0f) / 16.0f;
                    nexterr[col + 2].r += (err    ) / 16.0f;
                    err = (sg - acolormap[ind].acolor.g) * colorimp;
                    thiserr[col + 2].g += (err * 7.0f) / 16.0f;
                    nexterr[col    ].g += (err * 3.0f) / 16.0f;
                    nexterr[col + 1].g += (err * 5.0f) / 16.0f;
                    nexterr[col + 2].g += (err    ) / 16.0f;
                    err = (sb - acolormap[ind].acolor.b) * colorimp;
                    thiserr[col + 2].b += (err * 7.0f) / 16.0f;
                    nexterr[col    ].b += (err * 3.0f) / 16.0f;
                    nexterr[col + 1].b += (err * 5.0f) / 16.0f;
                    nexterr[col + 2].b += (err    ) / 16.0f;
                    err = (sa - acolormap[ind].acolor.a);
                    thiserr[col + 2].a += (err * 7.0f) / 16.0f;
                    nexterr[col    ].a += (err * 3.0f) / 16.0f;
                    nexterr[col + 1].a += (err * 5.0f) / 16.0f;
                    nexterr[col + 2].a += (err    ) / 16.0f;
                } else {
                    err = (sr - acolormap[ind].acolor.r) * colorimp;
                    thiserr[col    ].r += (err * 7.0f) / 16.0f;
                    nexterr[col + 2].r += (err * 3.0f) / 16.0f;
                    nexterr[col + 1].r += (err * 5.0f) / 16.0f;
                    nexterr[col    ].r += (err    ) / 16.0f;
                    err = (sg - acolormap[ind].acolor.g) * colorimp;
                    thiserr[col    ].g += (err * 7.0f) / 16.0f;
                    nexterr[col + 2].g += (err * 3.0f) / 16.0f;
                    nexterr[col + 1].g += (err * 5.0f) / 16.0f;
                    nexterr[col    ].g += (err    ) / 16.0f;
                    err = (sb - acolormap[ind].acolor.b) * colorimp;
                    thiserr[col    ].b += (err * 7.0f) / 16.0f;
                    nexterr[col + 2].b += (err * 3.0f) / 16.0f;
                    nexterr[col + 1].b += (err * 5.0f) / 16.0f;
                    nexterr[col    ].b += (err    ) / 16.0f;
                    err = (sa - acolormap[ind].acolor.a);
                    thiserr[col    ].a += (err * 7.0f) / 16.0f;
                    nexterr[col + 2].a += (err * 3.0f) / 16.0f;
                    nexterr[col + 1].a += (err * 5.0f) / 16.0f;
                    nexterr[col    ].a += (err    ) / 16.0f;
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
    }
    return 0;
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

pngquant_error pngquant(const char *filename, const char *newext, int floyd, int force, int verbose, int using_stdin, int reqcolors, int ie_bug)
{
    FILE *infile, *outfile;
    rgb_pixel **input_pixels;
    rgb_pixel *pP;
    int col;
    uch **row_pointers=NULL;
    int rows, cols;
    float min_opaque_val, almost_opaque_val;
    int ignorebits=0;
    acolorhist_vector achv, acolormap=NULL;
    int row;
    int colors;
    int newcolors = 0;

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
        min_opaque_val = 0.93; /* rest of the code uses min_opaque_val rather than checking for ie_bug */
        almost_opaque_val = min_opaque_val * 0.66;

        if (verbose) {
            fprintf(stderr, "  Working around IE6 bug by making image less transparent...\n");
            fflush(stderr);
        }
    } else {
        min_opaque_val = almost_opaque_val = 1;
    }

    for (row = 0; row < rows; ++row) {
        for (col = 0, pP = input_pixels[row]; col < cols; ++col, ++pP) {

            f_pixel px = to_f(rwpng_info.gamma, *pP);
            rgb_pixel rgbcheck = to_rgb(rwpng_info.gamma, px);

            if (pP->r != rgbcheck.r || pP->g != rgbcheck.g || pP->b != rgbcheck.b || pP->a != rgbcheck.a) {
                fprintf(stderr, "Conversion error: expected %d,%d,%d,%d got %d,%d,%d,%d\n",
                        pP->r,pP->g,pP->b,pP->a, rgbcheck.r,rgbcheck.g,rgbcheck.b,rgbcheck.a);
                return INTERNAL_LOGIC_ERROR;
            }

            /* set all completely transparent colors to black */
            if (!pP->a) {
                *pP = (rgb_pixel){0,0,0,pP->a};
            }
            /* ie bug: to avoid visible step caused by forced opaqueness, linearily raise opaqueness of almost-opaque colors */
            else if (pP->a < 255 && px.a > almost_opaque_val) {
                assert((min_opaque_val-almost_opaque_val)>0);

                double al = almost_opaque_val + (px.a-almost_opaque_val) * (1-almost_opaque_val) / (min_opaque_val-almost_opaque_val);
                if (al > 1) al = 1;
                px.a = al;
                pP->a = to_rgb(rwpng_info.gamma, px).a;
            }
        }
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
        assert(rwpng_info.gamma > 0);
        achv = pam_computeacolorhist(input_pixels, cols, rows, rwpng_info.gamma, MAXCOLORS, ignorebits, &colors);
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

    /* sort palette by (estimated) popularity */
    qsort(acolormap, newcolors, sizeof(acolormap[0]), valuecompare);

    int remap[256];
    if (set_palette(newcolors,verbose,remap,acolormap)) {
        return INTERNAL_LOGIC_ERROR;
    }


    /*
    ** Step 3.7 [GRR]: allocate memory for the entire indexed image
    ** note that rwpng_info.row_pointers
    ** is still in use via apixels (INPUT data).
    */

    rwpng_info.indexed_data = malloc(rows * cols);
    row_pointers = malloc(rows * sizeof(row_pointers[0]));

    if (!rwpng_info.indexed_data || !row_pointers) {
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

    for (row = 0;  row < rows;  ++row) {
        row_pointers[row] = rwpng_info.indexed_data + row*cols;
    }


    /*
    ** Step 4: map the colors in the image to their closest match in the
    ** new colormap, and write 'em out.
    */
    if (verbose) {
        fprintf(stderr, "  mapping image to new colors...\n" );
        fflush(stderr);
    }

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

    if (remap_to_palette(floyd,min_opaque_val,ie_bug,input_pixels,rows,cols,row_pointers,newcolors,remap,acolormap)) {
        return OUT_OF_MEMORY_ERROR;
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


    /* write entire interlaced palette PNG */

    rwpng_info.row_pointers = row_pointers;   /* now for OUTPUT data */
    rwpng_write_image_whole(&rwpng_info);

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

    return SUCCESS;
}



/*
** Here is the fun part, the median-cut colormap generator.  This is based
** on Paul Heckbert's paper, "Color Image Quantization for Frame Buffer
** Display," SIGGRAPH 1982 Proceedings, page 297.
*/

static f_pixel background;

static acolorhist_vector mediancut(acolorhist_vector achv, int colors, int sum, double min_opaque_val, int newcolors)
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
            float a = achv[indx + i].acolor.a;
            v = (achv[indx + i].acolor.r * a + (1.0f-a) * background.r);
            if (v < minr) minr = v;
            if (v > maxr) maxr = v;
            v = (achv[indx + i].acolor.g * a + (1.0f-a) * background.g);
            if (v < ming) ming = v;
            if (v > maxg) maxg = v;
            v = (achv[indx + i].acolor.b * a + (1.0f-a) * background.b);
            if (v < minb) minb = v;
            if (v > maxb) maxb = v;

        }

        /*
        ** Find the largest dimension, and sort by that component
        ** by simply comparing the range in RGB space
        */

        double adelta = (maxa-mina);
        double rdelta = (maxr-minr);
        double gdelta = (maxg-ming);
        double bdelta = (maxb-minb);

        if (adelta >= rdelta && adelta >= gdelta && adelta >= bdelta)
            qsort(&(achv[indx]), clrs, sizeof(struct acolorhist_item),
                alphacompare );
        else if (rdelta >= gdelta && rdelta >= bdelta)
            qsort(&(achv[indx]), clrs, sizeof(struct acolorhist_item),
                redcompare );
        else if (gdelta >= bdelta)
            qsort(&(achv[indx]), clrs, sizeof(struct acolorhist_item),
                greencompare );
        else
            qsort(&(achv[indx]), clrs, sizeof(struct acolorhist_item),
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
        qsort(bv, boxes, sizeof(struct box), sumcompare);
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

        /* store total color popularity */
        for(int i=0; i < bv[bi].colors; i++) {
            acolormap[bi].value += achv[bv[bi].ind + i].value;
        }
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

static f_pixel averagepixels(int indx, int clrs, acolorhist_vector achv, double min_opaque_val)
{
    double r = 0, g = 0, b = 0, a = 0, sum = 0, colorsum = 0;
    double maxa = 0;
    int i;

    for (i = 0; i < clrs; ++i) {
        float weight = 1.0f;
        float tmp;

        /* give more weight to colors that are further away from average
            this is intended to prevent desaturation of images and fading of whites
         */
        tmp = (0.5f - achv[indx + i].acolor.r);
        weight += tmp*tmp;
        tmp = (0.5f - achv[indx + i].acolor.g);
        weight += tmp*tmp;
        tmp = (0.5f - achv[indx + i].acolor.b);
        weight += tmp*tmp;

        /* find if there are opaque colors, in case we're supposed to preserve opacity exactly (ie_bug) */
        if (achv[indx + i].acolor.a > maxa) maxa = achv[indx + i].acolor.a;

        a += achv[indx + i].acolor.a * achv[indx + i].value * weight;
        sum += achv[indx + i].value * weight;

        /* blend colors proportionally to their alpha */
        weight *= 1.0+colorimportance(achv[indx + i].acolor.a);

        r += achv[indx + i].acolor.r*achv[indx + i].acolor.a * achv[indx + i].value * weight;
        g += achv[indx + i].acolor.g*achv[indx + i].acolor.a * achv[indx + i].value * weight;
        b += achv[indx + i].acolor.b*achv[indx + i].acolor.a * achv[indx + i].value * weight;
        colorsum += achv[indx + i].value * weight;
    }

    if (!sum) sum=1;
    a /= sum;

    if (!colorsum) colorsum=1;
    r /= colorsum*a;
    g /= colorsum*a;
    b /= colorsum*a;


    /** if there was at least one completely opaque color, "round" final color to opaque */
    if (a >= min_opaque_val && maxa >= (255.0/256.0)) a = 1;

    return (f_pixel){r, g, b, a};
}

#define compare(ch1,ch2,r,fallback) ( \
    ((acolorhist_vector)ch1)->acolor.r > ((acolorhist_vector)ch2)->acolor.r ? 1 : \
   (((acolorhist_vector)ch1)->acolor.r < ((acolorhist_vector)ch2)->acolor.r ? -1 : fallback))

static int redcompare(const void *ch1, const void *ch2)
{
    return compare(ch1,ch2,r, compare(ch1,ch2,g,0));
}

static int greencompare(const void *ch1, const void *ch2)
{
    return compare(ch1,ch2,g, compare(ch1,ch2,b,0));
}

static int bluecompare(const void *ch1, const void *ch2)
{
    return compare(ch1,ch2,b, compare(ch1,ch2,a,0));
}

static int alphacompare(const void *ch1, const void *ch2)
{
    return compare(ch1,ch2,a, compare(ch1,ch2,r,0));
}

static int valuecompare(const void *ch1, const void *ch2)
{
    return ((acolorhist_vector)ch1)->value > ((acolorhist_vector)ch2)->value ? -1 :
          (((acolorhist_vector)ch1)->value < ((acolorhist_vector)ch2)->value ? 1 : compare(ch1,ch2,g,0));
}

static int sumcompare(const void *b1, const void *b2)
{
    return ((box_vector)b2)->sum -
           ((box_vector)b1)->sum;
}

/** expects alpha in range 0-1 */
static float colorimportance(float alpha)
{
    return (1.0f-(1.0f-alpha)*(1.0f-alpha));
}


