/* pngquant.c - quantize the colors in an alphamap down to a specified number
**
** Copyright (C) 1989, 1991 by Jef Poskanzer.
** Copyright (C) 1997, 2000 by Greg Roelofs; based on an idea by
**                          Stefan Schneider.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#define VERSION "0.7 of 24 December 2000"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "png.h"        /* libpng header; includes zlib.h */
#include "rwpng.h"      /* typedefs, common macros, public prototypes */

typedef uch pixval;	/* GRR: hardcoded for now; later add 16-bit support */


/* from pam.h */

typedef struct {
    uch r, g, b, a;
} apixel;

/*
typedef struct {
    ush r, g, b, a;
} apixel16;
 */

#define pam_freearray(pixels,rows) pm_freearray((char **)pixels, rows)
#define PAM_GETR(p) ((p).r)
#define PAM_GETG(p) ((p).g)
#define PAM_GETB(p) ((p).b)
#define PAM_GETA(p) ((p).a)
#define PAM_ASSIGN(p,red,grn,blu,alf) \
   do { (p).r = (red); (p).g = (grn); (p).b = (blu); (p).a = (alf); } while (0)
#define PAM_EQUAL(p,q) \
   ((p).r == (q).r && (p).g == (q).g && (p).b == (q).b && (p).a == (q).a)
#define PAM_DEPTH(newp,p,oldmaxval,newmaxval) \
   PAM_ASSIGN( (newp), \
      ( (int) PAM_GETR(p) * (newmaxval) + (oldmaxval) / 2 ) / (oldmaxval), \
      ( (int) PAM_GETG(p) * (newmaxval) + (oldmaxval) / 2 ) / (oldmaxval), \
      ( (int) PAM_GETB(p) * (newmaxval) + (oldmaxval) / 2 ) / (oldmaxval), \
      ( (int) PAM_GETA(p) * (newmaxval) + (oldmaxval) / 2 ) / (oldmaxval) )


/* from pamcmap.h */

typedef struct acolorhist_item *acolorhist_vector;
struct acolorhist_item {
    apixel acolor;
    int value;
};

typedef struct acolorhist_list_item *acolorhist_list;
struct acolorhist_list_item {
    struct acolorhist_item ch;
    acolorhist_list next;
};

typedef acolorhist_list *acolorhash_table;



static mainprog_info rwpng_info;

#define FNMAX     1024

#define MAXCOLORS 32767

#define LARGE_NORM
/* #define LARGE_LUM */   /* GRR 19970727:  this isn't well-defined for RGBA */

/* #define REP_CENTER_BOX */
/* #define REP_AVERAGE_COLORS */
#define REP_AVERAGE_PIXELS

typedef struct box *box_vector;
struct box {
    int ind;
    int colors;
    int sum;
};

static acolorhist_vector mediancut(acolorhist_vector achv, int colors,
                                   int sum, pixval maxval, int newcolors);
static int redcompare(const void *ch1, const void *ch2);
static int greencompare(const void *ch1, const void *ch2);
static int bluecompare(const void *ch1, const void *ch2);
static int alphacompare(const void *ch1, const void *ch2);
static int sumcompare(const void *b1, const void *b2);

static acolorhist_vector pam_computeacolorhist (apixel **apixels, int cols,
                                                int rows, int maxacolors,
                                                int* acolorsP);
static acolorhash_table pam_computeacolorhash (apixel** apixels, int cols,
                                               int rows, int maxacolors,
                                               int* acolorsP);
static acolorhash_table pam_allocacolorhash (void);
static int pam_addtoacolorhash (acolorhash_table acht, apixel *acolorP,
                                int value);
static acolorhist_vector pam_acolorhashtoacolorhist (acolorhash_table acht,
                                                     int maxacolors);
static int pam_lookupacolor (acolorhash_table acht, apixel* acolorP);
static void pam_freeacolorhist (acolorhist_vector achv);
static void pam_freeacolorhash (acolorhash_table acht);

static char *pm_allocrow (int cols, int size);
static void pm_freerow (char* itrow);
static char **pm_allocarray (int cols, int rows, int size);
static void pm_freearray (char **its, int rows);

int
main( argc, argv )
    int argc;
    char *argv[];
{
    FILE *infile, *outfile;
    apixel **apixels;
    apixel **mapapixels;
    register apixel *pP;
    uch *pQ, *outrow, **row_pointers=NULL;
    int argn, row;
    register int col, limitcol;
    ulg rows, cols;
    pixval maxval, newmaxval;
#ifdef SUPPORT_MAPFILE
    ulg maprows, mapcols;
    pixval mapmaxval;
#endif
    int newcolors, colors;
    register int ind;
    acolorhist_vector achv, acolormap=NULL;
    acolorhash_table acht;
    int floyd=0, force=0;
    int usehash;
    long *thisrerr = NULL;
    long *nextrerr = NULL;
    long *thisgerr = NULL;
    long *nextgerr = NULL;
    long *thisberr = NULL;
    long *nextberr = NULL;
    long *thisaerr = NULL;
    long *nextaerr = NULL;
    long *temperr;
    register long sr=0, sg=0, sb=0, sa=0, err;
#define FS_SCALE 1024
    int fs_direction = 0;
    int x;
    int using_stdin = FALSE;
    int channels;
    char *filename, outname[FNMAX];
    char *pq_usage =
      "usage:  pngquant [-floyd|-fs] [-force] <ncolors> [pngfile]\n"
      "                 [-floyd|-fs] [-force] -map mapfile [pngfile]\n\n"
      "Quantizes a 32-bit RGBA PNG into an 8-bit (or smaller) RGBA-palette PNG\n"
      "using either ordered dithering (default) or Floyd-Steinberg diffusion\n"
      "dithering.  The output filename is the same as the input name except that\n"
      "it ends in \"-8.png\" .  Use -force to overwrite existing files.\n\n"
      "NOTE:  the -map option is NOT YET SUPPORTED.\n";


    argn = 1;
    floyd = 0;
    mapapixels = (apixel **)0;

    while ( argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0' ) {
        if ( 0 == strncmp( argv[argn], "-fs", 3 ) ||
             0 == strncmp( argv[argn], "-floyd", 3 ) )
            floyd = 1;
        else if ( 0 == strncmp( argv[argn], "-nofs", 5 ) ||
             0 == strncmp( argv[argn], "-nofloyd", 5 ) )
            floyd = 0;
        else if ( 0 == strncmp( argv[argn], "-force", 2 ) )
            force = 1;
        else if ( 0 == strncmp( argv[argn], "-noforce", 4 ) )
            force = 0;
        /* GRR TO DO:  add "-ordered" option */
        /* GRR TO DO:  add option to preserve background color (if any) exactly */
#ifdef SUPPORT_MAPFILE
        else if ( 0 == strncmp( argv[argn], "-map", 2 ) ) {
            ++argn;
            if ( argn == argc ) {
                fprintf( stderr, pq_usage );
                fflush( stderr );
                return 1;
            }
            if ((infile = fopen( argv[argn], "rb" )) == NULL) {
                fprintf(stderr, "cannot open mapfile %s for reading\n",
                  argv[argn]);
                fflush( stderr );
                return 2;
            }
            mapapixels = pam_readpam( infile, &mapcols, &maprows, &mapmaxval );
            fclose( infile );
            if ( mapcols == 0 || maprows == 0 ) {
                fprintf( stderr, "null colormap??\n" );
                fflush( stderr );
                return 3;
            }
        }
#endif /* SUPPORT_MAPFILE */
        else {
            fprintf( stderr, "pngquant, version %s, by Greg Roelofs.\n",
              VERSION );
            rwpng_version_info();
            fprintf( stderr, "\n" );
            fprintf( stderr, pq_usage );
            fflush( stderr );
            return 1;
        }
        ++argn;
    }

    if ( mapapixels == (apixel**) 0 ) {
        if ( argn == argc ) {
            /* GRR TO DO:  default to 256 colors if number not specified */
            fprintf( stderr, "pngquant, version %s, by Greg Roelofs.\n",
              VERSION );
            rwpng_version_info();
            fprintf( stderr, "\n" );
            fprintf( stderr, pq_usage );
            fflush( stderr );
            return 1;
        }
        if ( sscanf( argv[argn], "%d", &newcolors ) != 1 ) {
            fprintf( stderr, pq_usage );
            fflush( stderr );
            return 1;
        }
        if ( newcolors <= 1 ) {
            fprintf( stderr, "number of colors must be greater than 1\n" );
            fflush( stderr );
            return 4;
        }
        if ( newcolors > 256 ) {
            fprintf( stderr, "number of colors cannot be more than 256\n" );
            fflush( stderr );
            return 4;
        }
        ++argn;
    }

    if ( argn != argc ) {
        if ((infile = fopen( argv[argn], "rb" )) == NULL) {
            fprintf(stderr, "cannot open %s for reading\n", argv[argn]);
            fflush( stderr );
            return 2;
        }
        filename = argv[argn];
        ++argn;
    } else {
        infile = stdin;
        filename = "stdin";
        using_stdin = TRUE;
    }

    if ( argn != argc ) {
        fprintf( stderr, pq_usage );
        fflush( stderr );
        return 1;
    }

    /* build the output filename from the input name by inserting "-8" before
     * the ".png" extension (or by appending "-8.png" if no extension) */

    x = strlen(filename);
    if (x > FNMAX-7) {
        fprintf(stderr, "warning:  filename will be truncated\n");
        fflush(stderr);
        x = FNMAX-7;
    }
    strncpy(outname, filename, FNMAX-7);
    if (strncmp(outname+x-4, ".png", 4) == 0) 
        strcpy(outname+x-4, "-8.png");
    else
        strcpy(outname+x, "-8.png");

    if (!force) {
        if ((outfile = fopen( outname, "rb" )) != NULL) {
            fprintf(stderr, "warning:  %s exists; not overwriting\n", outname);
            fflush(stderr);
            fclose( outfile );
            return 15;
        }
    }
    if ((outfile = fopen( outname, "wb" )) == NULL) {
        fprintf(stderr, "cannot open %s for writing\n", outname);
        fflush(stderr);
        return 16;
    }

    /*
    ** Step 1: read in the alpha-channel image.
    */
    /* GRR:  returns RGB or RGBA (3 or 4 channels), 8 bps */
/*  apixels = pam_readpam( infile, &cols, &rows, &maxval );  */
    rwpng_read_image(infile, &rwpng_info);

    if (!using_stdin)
        fclose(infile);

    if (rwpng_info.retval) {
        fprintf(stderr, "rwpng_read_image() error\n");
        fflush(stderr);
        return rwpng_info.retval;
    }

    /* note:  rgba_data and row_pointers are allocated but not freed */
    apixels = (apixel **)rwpng_info.row_pointers;
    cols = rwpng_info.width;
    rows = rwpng_info.height;
    channels = rwpng_info.channels;
    maxval = 255;	/* GRR TO DO:  allow 8 or 16 bps */


    if ( mapapixels == (apixel**) 0 ) {
        /*
        ** Step 2: attempt to make a histogram of the colors, unclustered.
        ** If at first we don't succeed, lower maxval to increase color
        ** coherence and try again.  This will eventually terminate, with
        ** maxval at worst 15, since 32^3 is approximately MAXCOLORS.
        */
        for ( ; ; ) {
            fprintf(stderr, "making histogram..." );
            fflush(stderr);
            achv = pam_computeacolorhist(
                apixels, cols, rows, MAXCOLORS, &colors );
            if ( achv != (acolorhist_vector) 0 )
                break;
            fprintf(stderr, "too many colors!\n" );
            fflush(stderr);
            newmaxval = maxval / 2;
            fprintf(stderr,
        "scaling colors from maxval=%d to maxval=%d to improve clustering...\n",
                    maxval, newmaxval );
            fflush(stderr);
            for ( row = 0; row < rows; ++row )
                for ( col = 0, pP = apixels[row]; col < cols; ++col, ++pP )
                    PAM_DEPTH( *pP, *pP, maxval, newmaxval );
            maxval = newmaxval;
        }
        fprintf(stderr, "%d colors found\n", colors );
        fflush(stderr);

        /*
        ** Step 3: apply median-cut to histogram, making the new acolormap.
        */
        fprintf(stderr, "choosing %d colors...\n", newcolors );
        fflush(stderr);
        acolormap = mediancut( achv, colors, rows * cols, maxval, newcolors );
        pam_freeacolorhist( achv );
    }
#ifdef SUPPORT_MAPFILE
    else {
        /*
        ** Reverse steps 2 & 3 : Turn mapapixels into an acolormap.
        */
        if (mapmaxval != maxval) {
            if (mapmaxval > maxval) {
                fprintf(stderr, "rescaling colormap colors\n" );
                fflush(stderr);
            }
            for (row = 0; row < maprows; ++row)
                for (col = 0, pP = mapapixels[row]; col < mapcols; ++col, ++pP)
                    PAM_DEPTH(*pP, *pP, mapmaxval, maxval);
            mapmaxval = maxval;
        }
        acolormap = pam_computeacolorhist(
            mapapixels, mapcols, maprows, MAXCOLORS, &newcolors );
        if ( acolormap == (acolorhist_vector) 0 ) {
            fprintf( stderr, "too many colors in acolormap!\n" );
            fflush(stderr);
            if (rwpng_info.row_pointers)
                free(rwpng_info.row_pointers);
            if (rwpng_info.rgba_data)
                free(rwpng_info.rgba_data);
            return 5;
        }
        pam_freearray( mapapixels, maprows );
        fprintf(stderr, "%d colors found in acolormap\n", newcolors );
        fflush(stderr);
    }
#endif /* SUPPORT_MAPFILE */



    /* GRR TO DO:  remap palette to lose opaque tRNS entries */
    rwpng_info.num_palette = rwpng_info.num_trans = newcolors;

    for (x = 0; x < newcolors; ++x) {
        rwpng_info.palette[x].red   = PAM_GETR( acolormap[x].acolor );
        rwpng_info.palette[x].green = PAM_GETG( acolormap[x].acolor );
        rwpng_info.palette[x].blue  = PAM_GETB( acolormap[x].acolor );
        rwpng_info.trans[x]         = PAM_GETA( acolormap[x].acolor );
    }


    /* allocate memory for either a single row (non-interlaced -> progressive
     *  write) or the entire indexed image (if interlaced -> all at once);
     *  note that rwpng_info.row_pointers is still used via apixels (INPUT) */

    if (rwpng_info.interlaced) {
        if ((rwpng_info.indexed_data = (uch *)malloc(rows * cols)) != NULL) {
            if ((row_pointers = (uch **)malloc(rows * sizeof(uch *))) != NULL) {
                for (row = 0;  row < rows;  ++row)
                    row_pointers[row] = rwpng_info.indexed_data + row*cols;
            }
        }
    } else
        rwpng_info.indexed_data = (uch *)malloc(cols);

    if (rwpng_info.indexed_data == NULL ||
        (rwpng_info.interlaced && row_pointers == NULL))
    {
        fprintf(stderr,
          "insufficient memory for indexed data and/or row pointers");
        fflush(stderr);
        if (rwpng_info.row_pointers)
            free(rwpng_info.row_pointers);
        if (rwpng_info.rgba_data)
            free(rwpng_info.rgba_data);
        if (rwpng_info.indexed_data)
            free(rwpng_info.indexed_data);
        return 17;
    }



    /*
    ** Step 4: map the colors in the image to their closest match in the
    ** new colormap, and write 'em out.
    */
    fprintf(stderr, "mapping image to new colors...\n" );
    fflush(stderr);
    acht = pam_allocacolorhash( );
    usehash = 1;

    if (rwpng_write_image_init(outfile, &rwpng_info) != 0) {
        fprintf( stderr, "rwpng_write_image_init() error\n" );
        fflush( stderr );
        if (rwpng_info.rgba_data)
            free(rwpng_info.rgba_data);
        if (rwpng_info.row_pointers)
            free(rwpng_info.row_pointers);
        if (rwpng_info.indexed_data)
            free(rwpng_info.indexed_data);
        if (row_pointers)
            free(row_pointers);
        return rwpng_info.retval;
    }

    if ( floyd ) {
        /* Initialize Floyd-Steinberg error vectors. */
        thisrerr = (long*) pm_allocrow( cols + 2, sizeof(long) );
        nextrerr = (long*) pm_allocrow( cols + 2, sizeof(long) );
        thisgerr = (long*) pm_allocrow( cols + 2, sizeof(long) );
        nextgerr = (long*) pm_allocrow( cols + 2, sizeof(long) );
        thisberr = (long*) pm_allocrow( cols + 2, sizeof(long) );
        nextberr = (long*) pm_allocrow( cols + 2, sizeof(long) );
        thisaerr = (long*) pm_allocrow( cols + 2, sizeof(long) );
        nextaerr = (long*) pm_allocrow( cols + 2, sizeof(long) );
        srandom( (int) ( time( 0 ) ^ getpid( ) ) );
        for ( col = 0; col < cols + 2; ++col ) {
            thisrerr[col] = random( ) % ( FS_SCALE * 2 ) - FS_SCALE;
            thisgerr[col] = random( ) % ( FS_SCALE * 2 ) - FS_SCALE;
            thisberr[col] = random( ) % ( FS_SCALE * 2 ) - FS_SCALE;
            thisaerr[col] = random( ) % ( FS_SCALE * 2 ) - FS_SCALE;
            /* (random errors in [-1 .. 1]) */
        }
        fs_direction = 1;
    }
    for ( row = 0; row < rows; ++row ) {
        outrow = rwpng_info.interlaced? row_pointers[row] :
                                        rwpng_info.indexed_data;
        if ( floyd )
            for ( col = 0; col < cols + 2; ++col )
                nextrerr[col] = nextgerr[col] = nextberr[col] = nextaerr[col] = 0;
        if ( ( ! floyd ) || fs_direction ) {
            col = 0;
            limitcol = cols;
            pP = apixels[row];
            pQ = outrow;
        } else {
            col = cols - 1;
            limitcol = -1;
            pP = &(apixels[row][col]);
            pQ = &(outrow[col]);
        }
        do {
            if ( floyd ) {
                /* Use Floyd-Steinberg errors to adjust actual color. */
                sr = PAM_GETR(*pP) + thisrerr[col + 1] / FS_SCALE;
                sg = PAM_GETG(*pP) + thisgerr[col + 1] / FS_SCALE;
                sb = PAM_GETB(*pP) + thisberr[col + 1] / FS_SCALE;
                sa = PAM_GETA(*pP) + thisaerr[col + 1] / FS_SCALE;
                if ( sr < 0 ) sr = 0;
                else if ( sr > maxval ) sr = maxval;
                if ( sg < 0 ) sg = 0;
                else if ( sg > maxval ) sg = maxval;
                if ( sb < 0 ) sb = 0;
                else if ( sb > maxval ) sb = maxval;
                if ( sa < 0 ) sa = 0;
                else if ( sa > maxval ) sa = maxval;
                PAM_ASSIGN( *pP, sr, sg, sb, sa );
            }

            /* Check hash table to see if we have already matched this color. */
            ind = pam_lookupacolor( acht, pP );
            if ( ind == -1 ) {
                /* No; search acolormap for closest match. */
                register int i, r1, g1, b1, a1, r2, g2, b2, a2;
                register long dist, newdist;

                r1 = PAM_GETR( *pP );
                g1 = PAM_GETG( *pP );
                b1 = PAM_GETB( *pP );
                a1 = PAM_GETA( *pP );
                dist = 2000000000;
                for ( i = 0; i < newcolors; ++i ) {
                    r2 = PAM_GETR( acolormap[i].acolor );
                    g2 = PAM_GETG( acolormap[i].acolor );
                    b2 = PAM_GETB( acolormap[i].acolor );
                    a2 = PAM_GETA( acolormap[i].acolor );
/* GRR POSSIBLE BUG */
                    newdist = ( r1 - r2 ) * ( r1 - r2 ) +  /* may overflow? */
                              ( g1 - g2 ) * ( g1 - g2 ) +
                              ( b1 - b2 ) * ( b1 - b2 ) +
                              ( a1 - a2 ) * ( a1 - a2 );
                    if ( newdist < dist ) {
                        ind = i;
                        dist = newdist;
                    }
                }
                if ( usehash ) {
                    if ( pam_addtoacolorhash( acht, pP, ind ) < 0 ) {
                        fprintf(stderr,
                   "out of memory adding to hash table, proceeding without it");
                        fflush(stderr);
                        usehash = 0;
                    }
                }
            }

            if ( floyd ) {
                /* Propagate Floyd-Steinberg error terms. */
                if ( fs_direction ) {
                    err = (sr - (long)PAM_GETR(acolormap[ind].acolor)) * FS_SCALE;
                    thisrerr[col + 2] += ( err * 7 ) / 16;
                    nextrerr[col    ] += ( err * 3 ) / 16;
                    nextrerr[col + 1] += ( err * 5 ) / 16;
                    nextrerr[col + 2] += ( err     ) / 16;
                    err = (sg - (long)PAM_GETG(acolormap[ind].acolor)) * FS_SCALE;
                    thisgerr[col + 2] += ( err * 7 ) / 16;
                    nextgerr[col    ] += ( err * 3 ) / 16;
                    nextgerr[col + 1] += ( err * 5 ) / 16;
                    nextgerr[col + 2] += ( err     ) / 16;
                    err = (sb - (long)PAM_GETB(acolormap[ind].acolor)) * FS_SCALE;
                    thisberr[col + 2] += ( err * 7 ) / 16;
                    nextberr[col    ] += ( err * 3 ) / 16;
                    nextberr[col + 1] += ( err * 5 ) / 16;
                    nextberr[col + 2] += ( err     ) / 16;
                    err = (sa - (long)PAM_GETA(acolormap[ind].acolor)) * FS_SCALE;
                    thisaerr[col + 2] += ( err * 7 ) / 16;
                    nextaerr[col    ] += ( err * 3 ) / 16;
                    nextaerr[col + 1] += ( err * 5 ) / 16;
                    nextaerr[col + 2] += ( err     ) / 16;
                } else {
                    err = (sr - (long)PAM_GETR(acolormap[ind].acolor)) * FS_SCALE;
                    thisrerr[col    ] += ( err * 7 ) / 16;
                    nextrerr[col + 2] += ( err * 3 ) / 16;
                    nextrerr[col + 1] += ( err * 5 ) / 16;
                    nextrerr[col    ] += ( err     ) / 16;
                    err = (sg - (long)PAM_GETG(acolormap[ind].acolor)) * FS_SCALE;
                    thisgerr[col    ] += ( err * 7 ) / 16;
                    nextgerr[col + 2] += ( err * 3 ) / 16;
                    nextgerr[col + 1] += ( err * 5 ) / 16;
                    nextgerr[col    ] += ( err     ) / 16;
                    err = (sb - (long)PAM_GETB(acolormap[ind].acolor)) * FS_SCALE;
                    thisberr[col    ] += ( err * 7 ) / 16;
                    nextberr[col + 2] += ( err * 3 ) / 16;
                    nextberr[col + 1] += ( err * 5 ) / 16;
                    nextberr[col    ] += ( err     ) / 16;
                    err = (sa - (long)PAM_GETA(acolormap[ind].acolor)) * FS_SCALE;
                    thisaerr[col    ] += ( err * 7 ) / 16;
                    nextaerr[col + 2] += ( err * 3 ) / 16;
                    nextaerr[col + 1] += ( err * 5 ) / 16;
                    nextaerr[col    ] += ( err     ) / 16;
                }
            }

/*          *pP = acolormap[ind].acolor;  */
            *pQ = (uch)ind;

            if ( ( ! floyd ) || fs_direction ) {
                ++col;
                ++pP;
                ++pQ;
            } else {
                --col;
                --pP;
                --pQ;
            }
        }
        while ( col != limitcol );

        if ( floyd ) {
            temperr = thisrerr;
            thisrerr = nextrerr;
            nextrerr = temperr;
            temperr = thisgerr;
            thisgerr = nextgerr;
            nextgerr = temperr;
            temperr = thisberr;
            thisberr = nextberr;
            nextberr = temperr;
            temperr = thisaerr;
            thisaerr = nextaerr;
            nextaerr = temperr;
            fs_direction = ! fs_direction;
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

    fclose(outfile);


    /* now we're done with the OUTPUT data and row_pointers, too */

    if (rwpng_info.indexed_data) {
        free(rwpng_info.indexed_data);
        rwpng_info.indexed_data = NULL;
    }
    if (row_pointers) {
        free(row_pointers);
        row_pointers = rwpng_info.row_pointers = NULL;
    }


    return 0;   /* success! */
}



/*
** Here is the fun part, the median-cut colormap generator.  This is based
** on Paul Heckbert's paper, "Color Image Quantization for Frame Buffer
** Display," SIGGRAPH 1982 Proceedings, page 297.
*/

static acolorhist_vector
mediancut( achv, colors, sum, maxval, newcolors )
    acolorhist_vector achv;
    int colors, sum, newcolors;
    pixval maxval;
{
    acolorhist_vector acolormap;
    box_vector bv;
    register int bi, i;
    int boxes;

    bv = (box_vector) malloc( sizeof(struct box) * newcolors );
    acolormap =
        (acolorhist_vector) malloc( sizeof(struct acolorhist_item) * newcolors);
    if ( bv == (box_vector) 0 || acolormap == (acolorhist_vector) 0 ) {
        fprintf( stderr, "out of memory allocating box vector\n" );
        fflush(stderr);
        exit(6);
    }
    for ( i = 0; i < newcolors; ++i )
        PAM_ASSIGN( acolormap[i].acolor, 0, 0, 0, 0 );

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
    while ( boxes < newcolors ) {
        register int indx, clrs;
        int sm;
        register int minr, maxr, ming, mina, maxg, minb, maxb, maxa, v;
        int halfsum, lowersum;

        /*
        ** Find the first splittable box.
        */
        for ( bi = 0; bi < boxes; ++bi )
            if ( bv[bi].colors >= 2 )
                break;
        if ( bi == boxes )
            break;        /* ran out of colors! */
        indx = bv[bi].ind;
        clrs = bv[bi].colors;
        sm = bv[bi].sum;

        /*
        ** Go through the box finding the minimum and maximum of each
        ** component - the boundaries of the box.
        */
        minr = maxr = PAM_GETR( achv[indx].acolor );
        ming = maxg = PAM_GETG( achv[indx].acolor );
        minb = maxb = PAM_GETB( achv[indx].acolor );
        mina = maxa = PAM_GETA( achv[indx].acolor );
        for ( i = 1; i < clrs; ++i )
            {
            v = PAM_GETR( achv[indx + i].acolor );
            if ( v < minr ) minr = v;
            if ( v > maxr ) maxr = v;
            v = PAM_GETG( achv[indx + i].acolor );
            if ( v < ming ) ming = v;
            if ( v > maxg ) maxg = v;
            v = PAM_GETB( achv[indx + i].acolor );
            if ( v < minb ) minb = v;
            if ( v > maxb ) maxb = v;
            v = PAM_GETA( achv[indx + i].acolor );
            if ( v < mina ) mina = v;
            if ( v > maxa ) maxa = v;
            }

        /*
        ** Find the largest dimension, and sort by that component.  I have
        ** included two methods for determining the "largest" dimension;
        ** first by simply comparing the range in RGB space, and second
        ** by transforming into luminosities before the comparison.  You
        ** can switch which method is used by switching the commenting on
        ** the LARGE_ defines at the beginning of this source file.
        */
#ifdef LARGE_NORM
        if ( maxa - mina >= maxr - minr && maxa - mina >= maxg - ming && maxa - mina >= maxb - minb )
            qsort(
                (char*) &(achv[indx]), clrs, sizeof(struct acolorhist_item),
                alphacompare );
        else if ( maxr - minr >= maxg - ming && maxr - minr >= maxb - minb )
            qsort(
                (char*) &(achv[indx]), clrs, sizeof(struct acolorhist_item),
                redcompare );
        else if ( maxg - ming >= maxb - minb )
            qsort(
                (char*) &(achv[indx]), clrs, sizeof(struct acolorhist_item),
                greencompare );
        else
            qsort(
                (char*) &(achv[indx]), clrs, sizeof(struct acolorhist_item),
                bluecompare );
#endif /*LARGE_NORM*/
#ifdef LARGE_LUM
        {
        apixel p;
        float rl, gl, bl, al;

        PAM_ASSIGN(p, maxr - minr, 0, 0, 0);
        rl = PPM_LUMIN(p);
        PAM_ASSIGN(p, 0, maxg - ming, 0, 0);
        gl = PPM_LUMIN(p);
        PAM_ASSIGN(p, 0, 0, maxb - minb, 0);
        bl = PPM_LUMIN(p);

/*
GRR: treat alpha as grayscale and assign (maxa - mina) to each of R, G, B?
     assign (maxa - mina)/3 to each?
     use alpha-fractional luminosity?  (normalized_alpha * lum(r,g,b))
        al = dunno ...
     [probably should read Heckbert's paper to decide]
 */

        if ( al >= rl && al >= gl && al >= bl )
            qsort(
                (char*) &(achv[indx]), clrs, sizeof(struct acolorhist_item),
                alphacompare );
        else if ( rl >= gl && rl >= bl )
            qsort(
                (char*) &(achv[indx]), clrs, sizeof(struct acolorhist_item),
                redcompare );
        else if ( gl >= bl )
            qsort(
                (char*) &(achv[indx]), clrs, sizeof(struct acolorhist_item),
                greencompare );
        else
            qsort(
                (char*) &(achv[indx]), clrs, sizeof(struct acolorhist_item),
                bluecompare );
        }
#endif /*LARGE_LUM*/

        /*
        ** Now find the median based on the counts, so that about half the
        ** pixels (not colors, pixels) are in each subdivision.
        */
        lowersum = achv[indx].value;
        halfsum = sm / 2;
        for ( i = 1; i < clrs - 1; ++i )
            {
            if ( lowersum >= halfsum )
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
        qsort( (char*) bv, boxes, sizeof(struct box), sumcompare );
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
    for ( bi = 0; bi < boxes; ++bi ) {
#ifdef REP_CENTER_BOX
        register int indx = bv[bi].ind;
        register int clrs = bv[bi].colors;
        register int minr, maxr, ming, maxg, minb, maxb, mina, maxa, v;

        minr = maxr = PAM_GETR( achv[indx].acolor );
        ming = maxg = PAM_GETG( achv[indx].acolor );
        minb = maxb = PAM_GETB( achv[indx].acolor );
        mina = maxa = PAM_GETA( achv[indx].acolor );
        for ( i = 1; i < clrs; ++i )
            {
            v = PAM_GETR( achv[indx + i].acolor );
            minr = min( minr, v );
            maxr = max( maxr, v );
            v = PAM_GETG( achv[indx + i].acolor );
            ming = min( ming, v );
            maxg = max( maxg, v );
            v = PAM_GETB( achv[indx + i].acolor );
            minb = min( minb, v );
            maxb = max( maxb, v );
            v = PAM_GETA( achv[indx + i].acolor );
            mina = min( mina, v );
            maxa = max( maxa, v );
            }
        PAM_ASSIGN(
            acolormap[bi].acolor, ( minr + maxr ) / 2, ( ming + maxg ) / 2,
            ( minb + maxb ) / 2, ( mina + maxa ) / 2 );
#endif /*REP_CENTER_BOX*/
#ifdef REP_AVERAGE_COLORS
        register int indx = bv[bi].ind;
        register int clrs = bv[bi].colors;
        register long r = 0, g = 0, b = 0, a = 0;

        for ( i = 0; i < clrs; ++i )
            {
            r += PAM_GETR( achv[indx + i].acolor );
            g += PAM_GETG( achv[indx + i].acolor );
            b += PAM_GETB( achv[indx + i].acolor );
            a += PAM_GETA( achv[indx + i].acolor );
            }
        r = r / clrs;
        g = g / clrs;
        b = b / clrs;
        a = a / clrs;
        PAM_ASSIGN( acolormap[bi].acolor, r, g, b, a );
#endif /*REP_AVERAGE_COLORS*/
#ifdef REP_AVERAGE_PIXELS
        register int indx = bv[bi].ind;
        register int clrs = bv[bi].colors;
        register long r = 0, g = 0, b = 0, a = 0, sum = 0;

        for ( i = 0; i < clrs; ++i )
            {
            r += PAM_GETR( achv[indx + i].acolor ) * achv[indx + i].value;
            g += PAM_GETG( achv[indx + i].acolor ) * achv[indx + i].value;
            b += PAM_GETB( achv[indx + i].acolor ) * achv[indx + i].value;
            a += PAM_GETA( achv[indx + i].acolor ) * achv[indx + i].value;
            sum += achv[indx + i].value;
            }
        r = r / sum;
        if ( r > maxval ) r = maxval;        /* avoid math errors */
        g = g / sum;
        if ( g > maxval ) g = maxval;
        b = b / sum;
        if ( b > maxval ) b = maxval;
        a = a / sum;
        if ( a > maxval ) a = maxval;
        PAM_ASSIGN( acolormap[bi].acolor, r, g, b, a );
#endif /*REP_AVERAGE_PIXELS*/
    }

    /*
    ** All done.
    */
    return acolormap;
}

static int
redcompare( const void *ch1, const void *ch2 )
{
    return (int) PAM_GETR( ((acolorhist_vector)ch1)->acolor ) -
           (int) PAM_GETR( ((acolorhist_vector)ch2)->acolor );
}

static int
greencompare( const void *ch1, const void *ch2 )
{
    return (int) PAM_GETG( ((acolorhist_vector)ch1)->acolor ) -
           (int) PAM_GETG( ((acolorhist_vector)ch2)->acolor );
}

static int
bluecompare( const void *ch1, const void *ch2 )
{
    return (int) PAM_GETB( ((acolorhist_vector)ch1)->acolor ) -
           (int) PAM_GETB( ((acolorhist_vector)ch2)->acolor );
}

static int
alphacompare( const void *ch1, const void *ch2 )
{
    return (int) PAM_GETA( ((acolorhist_vector)ch1)->acolor ) -
           (int) PAM_GETA( ((acolorhist_vector)ch2)->acolor );
}

static int
sumcompare( const void *b1, const void *b2 )
{
    return ((box_vector)b2)->sum -
           ((box_vector)b1)->sum;
}



/*

libpam3.c:
	pam_computeacolorhist( )
NOTUSED	pam_addtoacolorhist( )
	pam_computeacolorhash( )
	pam_allocacolorhash( )
	pam_addtoacolorhash( )
	pam_acolorhashtoacolorhist( )
NOTUSED	pam_acolorhisttoacolorhash( )
	pam_lookupacolor( )
	pam_freeacolorhist( )
	pam_freeacolorhash( )

libpbm1.c:
	pm_allocarray( )
	pm_freearray( )
	pm_allocrow( )

pam.h:
	pam_freearray( )
 */



/* libpam3.c - pam (portable alpha map) utility library part 3
**
** Colormap routines.
**
** Copyright (C) 1989, 1991 by Jef Poskanzer.
** Copyright (C) 1997 by Greg Roelofs.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

/*
#include "pam.h"
#include "pamcmap.h"
 */

#define HASH_SIZE 20023

#define pam_hashapixel(p) ( ( ( (long) PAM_GETR(p) * 33023 + \
                                (long) PAM_GETG(p) * 30013 + \
                                (long) PAM_GETB(p) * 27011 + \
                                (long) PAM_GETA(p) * 24007 ) \
                              & 0x7fffffff ) % HASH_SIZE )

static acolorhist_vector
pam_computeacolorhist( apixels, cols, rows, maxacolors, acolorsP )
    apixel** apixels;
    int cols, rows, maxacolors;
    int* acolorsP;
{
    acolorhash_table acht;
    acolorhist_vector achv;

    acht = pam_computeacolorhash( apixels, cols, rows, maxacolors, acolorsP );
    if ( acht == (acolorhash_table) 0 )
	return (acolorhist_vector) 0;
    achv = pam_acolorhashtoacolorhist( acht, maxacolors );
    pam_freeacolorhash( acht );
    return achv;
}



static acolorhash_table
pam_computeacolorhash( apixels, cols, rows, maxacolors, acolorsP )
    apixel** apixels;
    int cols, rows, maxacolors;
    int* acolorsP;
{
    acolorhash_table acht;
    register apixel* pP;
    acolorhist_list achl;
    int col, row, hash;

    acht = pam_allocacolorhash( );
    *acolorsP = 0;

    /* Go through the entire image, building a hash table of colors. */
    for ( row = 0; row < rows; ++row )
	for ( col = 0, pP = apixels[row]; col < cols; ++col, ++pP )
	    {
	    hash = pam_hashapixel( *pP );
	    for ( achl = acht[hash]; achl != (acolorhist_list) 0; achl = achl->next )
		if ( PAM_EQUAL( achl->ch.acolor, *pP ) )
		    break;
	    if ( achl != (acolorhist_list) 0 )
		++(achl->ch.value);
	    else
		{
		if ( ++(*acolorsP) > maxacolors )
		    {
		    pam_freeacolorhash( acht );
		    return (acolorhash_table) 0;
		    }
		achl = (acolorhist_list) malloc( sizeof(struct acolorhist_list_item) );
		if ( achl == 0 ) {
                    fprintf( stderr, "out of memory computing hash table\n" );
                    exit(7);
                }
		achl->ch.acolor = *pP;
		achl->ch.value = 1;
		achl->next = acht[hash];
		acht[hash] = achl;
		}
	    }
    
    return acht;
}



static acolorhash_table
pam_allocacolorhash( )
{
    acolorhash_table acht;
    int i;

    acht = (acolorhash_table) malloc( HASH_SIZE * sizeof(acolorhist_list) );
    if ( acht == 0 ) {
        fprintf( stderr, "out of memory allocating hash table\n" );
        exit(8);
    }

    for ( i = 0; i < HASH_SIZE; ++i )
	acht[i] = (acolorhist_list) 0;

    return acht;
}



static int
pam_addtoacolorhash( acht, acolorP, value )
    acolorhash_table acht;
    apixel* acolorP;
    int value;
{
    register int hash;
    register acolorhist_list achl;

    achl = (acolorhist_list) malloc( sizeof(struct acolorhist_list_item) );
    if ( achl == 0 )
	return -1;
    hash = pam_hashapixel( *acolorP );
    achl->ch.acolor = *acolorP;
    achl->ch.value = value;
    achl->next = acht[hash];
    acht[hash] = achl;
    return 0;
}



static acolorhist_vector
pam_acolorhashtoacolorhist( acht, maxacolors )
    acolorhash_table acht;
    int maxacolors;
{
    acolorhist_vector achv;
    acolorhist_list achl;
    int i, j;

    /* Now collate the hash table into a simple acolorhist array. */
    achv = (acolorhist_vector) malloc( maxacolors * sizeof(struct acolorhist_item) );
    /* (Leave room for expansion by caller.) */
    if ( achv == (acolorhist_vector) 0 ) {
        fprintf( stderr, "out of memory generating histogram\n" );
        exit(9);
    }

    /* Loop through the hash table. */
    j = 0;
    for ( i = 0; i < HASH_SIZE; ++i )
	for ( achl = acht[i]; achl != (acolorhist_list) 0; achl = achl->next )
	    {
	    /* Add the new entry. */
	    achv[j] = achl->ch;
	    ++j;
	    }

    /* All done. */
    return achv;
}



static int
pam_lookupacolor( acht, acolorP )
    acolorhash_table acht;
    apixel* acolorP;
{
    int hash;
    acolorhist_list achl;

    hash = pam_hashapixel( *acolorP );
    for ( achl = acht[hash]; achl != (acolorhist_list) 0; achl = achl->next )
	if ( PAM_EQUAL( achl->ch.acolor, *acolorP ) )
	    return achl->ch.value;

    return -1;
}



static void
pam_freeacolorhist( achv )
    acolorhist_vector achv;
{
    free( (char*) achv );
}



static void
pam_freeacolorhash( acht )
    acolorhash_table acht;
{
    int i;
    acolorhist_list achl, achlnext;

    for ( i = 0; i < HASH_SIZE; ++i )
	for ( achl = acht[i]; achl != (acolorhist_list) 0; achl = achlnext )
	    {
	    achlnext = achl->next;
	    free( (char*) achl );
	    }
    free( (char*) acht );
}


/* 00000000 */ /* 00000000 */ /* 00000000 */ /* 00000000 */ /* 00000000 */

/* from libpbm1.c */

static char*
pm_allocrow( cols, size )
    int cols;
    int size;
{
    register char* itrow;

    itrow = (char*) malloc( cols * size );
    if ( itrow == (char*) 0 ) {
        fprintf( stderr, "out of memory allocating a row\n" );
        fflush( stderr );
        exit(12);
    }
    return itrow;
}



static void
pm_freerow( itrow )
    char* itrow;
{
    free( itrow );
}



static char**
pm_allocarray( cols, rows, size )
    int cols, rows;
    int size;
{
    char** its;
    int i;

    its = (char**) malloc( rows * sizeof(char*) );
    if ( its == (char**) 0 ) {
        fprintf( stderr, "out of memory allocating an array\n" );
        fflush( stderr );
        exit(13);
    }
    its[0] = (char*) malloc( rows * cols * size );
    if ( its[0] == (char*) 0 ) {
        fprintf( stderr, "out of memory allocating an array\n" );
        fflush( stderr );
        exit(14);
    }
    for ( i = 1; i < rows; ++i )
        its[i] = &(its[0][i * cols * size]);
    return its;
}



static void
pm_freearray( its, rows )
    char** its;
    int rows;
{
    free( its[0] );
    free( its );
}
