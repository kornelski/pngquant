/*
** pngtopnm.c -
** read a Portable Network Graphics file and produce a portable anymap
**
** Copyright (C) 1995,1997 by Alexander Lehmann <alex@hal.rhein-main.de>
**                        and Willem van Schaik <willem@gintic.gov.sg>
** Alpha-channel concatenation support (-rgba) by Greg Roelofs, 27 July 1997.
**
** version 2.34 - February 1997
** version 2.34-rgba - July 1997
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
**
** modeled after giftopnm by David Koblas and
** with lots of bits pasted from libpng.txt by Guy Eric Schalnat
*/

#define VERSION "2.34-rgba of 27 July 1997"

#include <math.h>
#include "pnm.h"
#include "png.h"

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NONE
#define NONE 0
#endif

/* function prototypes */
#ifdef __STDC__
static png_uint_16 _get_png_val (png_byte **pp, int bit_depth);
static void store_pixel (xel *pix, png_uint_16 r, png_uint_16 g, png_uint_16 b, png_uint_16 a);
static int iscolor (png_color c);
static void save_text (png_info *info_ptr, FILE *tfp);
static void show_time (png_info *info_ptr);
static void convertpng (FILE *ifp, FILE *tfp);
int main (int argc, char *argv[]);
#endif

enum alpha_handling
  {none, alpha_only, mix, alpha_appended};

static png_uint_16 maxval;
static png_uint_16 bgr, bgg, bgb; /* background colors */
static int verbose = FALSE;
static enum alpha_handling alpha = none;
static png_uint_16 *alpha_pixel;   /* GRR 970727:  for alpha_appended only */
static int alpha_exists = FALSE;   /* GRR 970727:  for alpha_appended only */
static int background = -1;
static char *backstring;
static float displaygamma = -1.0; /* display gamma */
static int text = FALSE;
static char *text_file;
static int mtime = FALSE;

#define get_png_val(p) _get_png_val (&(p), info_ptr->bit_depth)

#ifdef __STDC__
static png_uint_16 _get_png_val (png_byte **pp, int bit_depth)
#else
static png_uint_16 _get_png_val (pp, bit_depth)
png_byte **pp;
int bit_depth;
#endif
{
  png_uint_16 c = 0;

  if (bit_depth == 16) {
    c = (*((*pp)++)) << 8;
  }
  c |= (*((*pp)++));

  return c;
}

#ifdef __STDC__
static void store_pixel (xel *pix, png_uint_16 r, png_uint_16 g, png_uint_16 b, png_uint_16 a)
#else
static void store_pixel (pix, r, g, b, a)
xel *pix;
png_uint_16 r, g, b, a;
#endif
{
  if (alpha == alpha_only) {
    PNM_ASSIGN1 (*pix, a);
  } else {
    if ((alpha == mix) && (a != maxval)) {
      r = r * (double)a / maxval + ((1.0 - (double)a / maxval) * bgr);
      g = g * (double)a / maxval + ((1.0 - (double)a / maxval) * bgg);
      b = b * (double)a / maxval + ((1.0 - (double)a / maxval) * bgb);
    } else if (alpha == alpha_appended) {
      if (a != maxval)
        alpha_exists = TRUE;
      *alpha_pixel++ = a;
    }
    PPM_ASSIGN (*pix, r, g, b);
  }
}

#ifdef __STDC__
static int iscolor (png_color c)
#else
static int iscolor (c)
png_color c;
#endif
{
  return c.red != c.green || c.green != c.blue;
}

#ifdef __STDC__
static void save_text (png_info *info_ptr, FILE *tfp)
#else
static void save_text (info_ptr, tfp)
png_info *info_ptr;
FILE *tfp;
#endif
{
  int i, j, k;

  for (i = 0 ; i < info_ptr->num_text ; i++) {
    j = 0;
    while (info_ptr->text[i].key[j] != '\0' && info_ptr->text[i].key[j] != ' ')
      j++;    
    if (info_ptr->text[i].key[j] != ' ') {
      fprintf (tfp, "%s", info_ptr->text[i].key);
      for (j = strlen (info_ptr->text[i].key) ; j < 15 ; j++)
        putc (' ', tfp);
    } else {
      fprintf (tfp, "\"%s\"", info_ptr->text[i].key);
      for (j = strlen (info_ptr->text[i].key) ; j < 13 ; j++)
        putc (' ', tfp);
    }
    putc (' ', tfp); /* at least one space between key and text */
    
    for (j = 0 ; j < info_ptr->text[i].text_length ; j++) {
      putc (info_ptr->text[i].text[j], tfp);
      if (info_ptr->text[i].text[j] == '\n')
        for (k = 0 ; k < 16 ; k++)
          putc ((int)' ', tfp);
    }
    putc ((int)'\n', tfp);
  }
}

#ifdef __STDC__
static void show_time (png_info *info_ptr)
#else
static void show_time (info_ptr)
png_info *info_ptr;
#endif
{
  static char *month[] =
    {"", "January", "February", "March", "April", "May", "June",
     "July", "August", "September", "October", "November", "December"};

  if (info_ptr->valid & PNG_INFO_tIME) {
    pm_message ("modification time: %02d %s %d %02d:%02d:%02d",
                info_ptr->mod_time.day, month[info_ptr->mod_time.month],
                info_ptr->mod_time.year, info_ptr->mod_time.hour,
                info_ptr->mod_time.minute, info_ptr->mod_time.second);
  }
}

#ifdef __STDC__
static void convertpng (FILE *ifp, FILE *tfp)
#else
static void convertpng (ifp, tfp)
FILE *ifp;
FILE *tfp;
#endif
{
  png_struct *png_ptr;
  png_info *info_ptr;
  pixel *row;
  png_byte **png_image;
  png_uint_16 *alpha_channel;
  png_byte *png_pixel;
  pixel *pnm_pixel;
  int x, y;
  int linesize;
  png_uint_16 c, c2, c3, a;
  int pnm_type;
  int i;
  char *type_string;
  char *alpha_string;
  int trans_mix;
  pixel backcolor;
  char gamma_string[80];

  png_ptr = (png_struct *)malloc (sizeof (png_struct));
  info_ptr = (png_info *)malloc (sizeof (png_info));

  if (png_ptr == NULL || info_ptr == NULL)
    pm_error ("Cannot allocate LIBPNG structures");

  if (setjmp (png_ptr->jmpbuf)) {
    png_read_destroy (png_ptr, info_ptr, (png_info *)0);
    free (png_ptr);
    free (info_ptr);
    pm_error ("setjmp returns error condition");
  }

  png_read_init (png_ptr);
  png_info_init (info_ptr);
  png_init_io (png_ptr, ifp);
  png_read_info (png_ptr, info_ptr);

  if (verbose) {
    switch (info_ptr->color_type) {
      case PNG_COLOR_TYPE_GRAY:
        type_string = "gray";
        alpha_string = "";
        break;

      case PNG_COLOR_TYPE_GRAY_ALPHA:
        type_string = "gray";
        alpha_string = "+alpha";
        break;

      case PNG_COLOR_TYPE_PALETTE:
        type_string = "palette";
        alpha_string = "";
        break;

      case PNG_COLOR_TYPE_RGB:
        type_string = "truecolor";
        alpha_string = "";
        break;

      case PNG_COLOR_TYPE_RGB_ALPHA:
        type_string = "truecolor";
        alpha_string = "+alpha";
        break;
    }
    if (info_ptr->valid & PNG_INFO_tRNS) {
      alpha_string = "+transparency";
    }

    if (info_ptr->valid & PNG_INFO_gAMA) {
      sprintf (gamma_string, ", image gamma = %4.2f", info_ptr->gamma);
    } else {
      strcpy (gamma_string, "");
    }

    if (verbose)
      pm_message ("reading a %d x %d image, %d bit%s %s%s%s%s",
		  info_ptr->width, info_ptr->height,
		  info_ptr->bit_depth, info_ptr->bit_depth>1 ? "s" : "",
		  type_string, alpha_string, gamma_string,
		  info_ptr->interlace_type ? ", Adam7 interlaced" : "");
  }

  png_image = (png_byte **)malloc (info_ptr->height * sizeof (png_byte*));
  if (png_image == NULL) {
    png_read_destroy (png_ptr, info_ptr, (png_info *)0);
    free (png_ptr);
    free (info_ptr);
    pm_error ("couldn't alloc space for image");
  }

  if (alpha == alpha_appended) {
    /* GRR:  could do this instead, with a little more effort in store_pixel():
    pgmrows = pgm_allocarray (info_ptr->width, info_ptr->height);
     */
    alpha_pixel = alpha_channel = (png_uint_16 *)
      malloc (info_ptr->width * info_ptr->height * sizeof (png_uint_16));
    if (alpha_channel == NULL) {
      free (png_image);
      png_read_destroy (png_ptr, info_ptr, (png_info *)0);
      free (png_ptr);
      free (info_ptr);
      pm_error ("couldn't alloc space for appended alpha channel");
    }
  }

  if (info_ptr->bit_depth == 16)
    linesize = 2 * info_ptr->width;
  else
    linesize = info_ptr->width;

  if (info_ptr->color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
    linesize *= 2;
  else
  if (info_ptr->color_type == PNG_COLOR_TYPE_RGB)
    linesize *= 3;
  else
  if (info_ptr->color_type == PNG_COLOR_TYPE_RGB_ALPHA)
    linesize *= 4;

  for (y = 0 ; y < info_ptr->height ; y++) {
    png_image[y] = malloc (linesize);
    if (png_image[y] == NULL) {
      for (x = 0 ; x < y ; x++)
        free (png_image[x]);
      if (alpha == alpha_appended)
        free (alpha_channel);
      free (png_image);
      png_read_destroy (png_ptr, info_ptr, (png_info *)0);
      free (png_ptr);
      free (info_ptr);
      pm_error ("couldn't alloc space for image");
    }
  }

  if (info_ptr->bit_depth < 8)
    png_set_packing (png_ptr);

  if (info_ptr->color_type == PNG_COLOR_TYPE_PALETTE) {
    maxval = 255;
  } else {
    maxval = (1l << info_ptr->bit_depth) - 1;
  }

  /* sBIT handling is a bit tricky. If we are extracting only the image, we
     can use the sBIT info for grayscale and color images, if the three
     values agree. If we extract the transparency/alpha mask, sBIT is
     irrelevant for trans and valid for alpha. If we mix both, the
     multiplication may result in values that require the normal bit depth,
     so we will use the sBIT info only for transparency, if we know that only
     solid and fully transparent is used */

  if (info_ptr->valid & PNG_INFO_sBIT) {
    switch (alpha) {
      case mix:
        if (info_ptr->color_type == PNG_COLOR_TYPE_RGB_ALPHA ||
            info_ptr->color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
          break;
        if (info_ptr->color_type == PNG_COLOR_TYPE_PALETTE &&
            (info_ptr->valid & PNG_INFO_tRNS)) {
          trans_mix = TRUE;
          for (i = 0 ; i < info_ptr->num_trans ; i++)
            if (info_ptr->trans[i] != 0 && info_ptr->trans[i] != 255) {
              trans_mix = FALSE;
              break;
            }
          if (!trans_mix)
            break;
        }

        /* else fall though to normal case */

      case none:
        if ((info_ptr->color_type == PNG_COLOR_TYPE_PALETTE ||
             info_ptr->color_type == PNG_COLOR_TYPE_RGB ||
             info_ptr->color_type == PNG_COLOR_TYPE_RGB_ALPHA) &&
            (info_ptr->sig_bit.red != info_ptr->sig_bit.green ||
             info_ptr->sig_bit.green != info_ptr->sig_bit.blue) &&
            alpha == none) {
	  pm_message ("different bit depths for color channels not supported");
	  pm_message ("writing file with %dbit resolution", info_ptr->bit_depth);
        } else {
          if ((info_ptr->color_type == PNG_COLOR_TYPE_PALETTE) &&
	      (info_ptr->sig_bit.red < info_ptr->bit_depth)) {
	    for (i = 0 ; i < info_ptr->num_palette ; i++) {
	      info_ptr->palette[i].red   >>= 8-info_ptr->sig_bit.red;
	      info_ptr->palette[i].green >>= 8-info_ptr->sig_bit.red;
	      info_ptr->palette[i].blue  >>= 8-info_ptr->sig_bit.red;
	    }
	    if (verbose)
	      pm_message ("image has fewer significant bits, writing file with %d bits per channel", 
		info_ptr->sig_bit.red);
	    maxval = (1l << info_ptr->sig_bit.red) - 1;
          } else
          if ((info_ptr->color_type == PNG_COLOR_TYPE_RGB ||
               info_ptr->color_type == PNG_COLOR_TYPE_RGB_ALPHA) &&
	      (info_ptr->sig_bit.red < info_ptr->bit_depth)) {
	    png_set_shift (png_ptr, &(info_ptr->sig_bit));
	    if (verbose)
	      pm_message ("image has fewer significant bits, writing file with %d bits per channel", 
		info_ptr->sig_bit.red);
	    maxval = (1l << info_ptr->sig_bit.red) - 1;
          } else 
          if ((info_ptr->color_type == PNG_COLOR_TYPE_GRAY ||
               info_ptr->color_type == PNG_COLOR_TYPE_GRAY_ALPHA) &&
	      (info_ptr->sig_bit.gray < info_ptr->bit_depth)) {
	    png_set_shift (png_ptr, &(info_ptr->sig_bit));
	    if (verbose)
	      pm_message ("image has fewer significant bits, writing file with %d bits",
		info_ptr->sig_bit.gray);
	    maxval = (1l << info_ptr->sig_bit.gray) - 1;
          }
        }
        break;

      case alpha_only:
        if ((info_ptr->color_type == PNG_COLOR_TYPE_RGB_ALPHA ||
             info_ptr->color_type == PNG_COLOR_TYPE_GRAY_ALPHA) && 
	    (info_ptr->sig_bit.gray < info_ptr->bit_depth)) {
	  png_set_shift (png_ptr, &(info_ptr->sig_bit));
	  if (verbose)
	    pm_message ("image has fewer significant bits, writing file with %d bits", 
		info_ptr->sig_bit.alpha);
	  maxval = (1l << info_ptr->sig_bit.alpha) - 1;
        }
        break;

      }
  }

  /* didn't manage to get libpng to work (bugs?) concerning background */
  /* processing, therefore we do our own using bgr, bgg and bgb        */
  if (info_ptr->valid & PNG_INFO_bKGD)
    switch (info_ptr->color_type) {
      case PNG_COLOR_TYPE_GRAY:
      case PNG_COLOR_TYPE_GRAY_ALPHA:
        bgr = bgg = bgb = info_ptr->background.gray;
        break;
      case PNG_COLOR_TYPE_PALETTE:
        bgr = info_ptr->palette[info_ptr->background.index].red;
        bgg = info_ptr->palette[info_ptr->background.index].green;
        bgb = info_ptr->palette[info_ptr->background.index].blue;
        break;
      case PNG_COLOR_TYPE_RGB:
      case PNG_COLOR_TYPE_RGB_ALPHA:
        bgr = info_ptr->background.red;
        bgg = info_ptr->background.green;
        bgb = info_ptr->background.blue;
        break;
    }
  else
    bgr = bgg = bgb = 0;

  /* but if background was specified from the command-line, we always use that */
  if (background > -1)
  {
    backcolor = ppm_parsecolor (backstring, maxval);
    switch (info_ptr->color_type) {
      case PNG_COLOR_TYPE_GRAY:
      case PNG_COLOR_TYPE_GRAY_ALPHA:
        bgr = bgg = bgb = PNM_GET1 (backcolor);
        break;
      case PNG_COLOR_TYPE_PALETTE:
      case PNG_COLOR_TYPE_RGB:
      case PNG_COLOR_TYPE_RGB_ALPHA:
        bgr = PPM_GETR (backcolor);
        bgg = PPM_GETG (backcolor);
        bgb = PPM_GETB (backcolor);
        break;
    }
  }

  /* gamma-correction */
  if (displaygamma != -1.0) {
    if (info_ptr->valid & PNG_INFO_gAMA) {
      png_set_gamma (png_ptr, displaygamma, info_ptr->gamma);
      if (verbose)
        pm_message ("image gamma is %4.2f, converted for display gamma of %4.2f",
                    info_ptr->gamma, displaygamma);
    } else {
      png_set_gamma (png_ptr, displaygamma, 1.0);
      if (verbose)
        pm_message ("image gamma assumed 1.0, converted for display gamma of %4.2f",
                    displaygamma);
    }
  }

  png_read_image (png_ptr, png_image);
  png_read_end (png_ptr, info_ptr);

  if (mtime)
    show_time (info_ptr);
  if (text)
    save_text (info_ptr, tfp);

  if (info_ptr->valid & PNG_INFO_pHYs) {
    float r;
    r = (float)info_ptr->x_pixels_per_unit / info_ptr->y_pixels_per_unit;
    if (r != 1.0) {
      pm_message ("warning - non-square pixels; to fix do a 'pnmscale -%cscale %g'",
		    r < 1.0 ? 'x' : 'y',
		    r < 1.0 ? 1.0 / r : r );
    }
  }

  if ((row = pnm_allocrow (info_ptr->width)) == NULL) {
    for (y = 0 ; y < info_ptr->height ; y++)
      free (png_image[y]);
    if (alpha == alpha_appended)
      free (alpha_channel);
    free (png_image);
    png_read_destroy (png_ptr, info_ptr, (png_info *)0);
    free (png_ptr);
    free (info_ptr);
    pm_error ("couldn't alloc space for image");
  }

  if (alpha == alpha_only) {
    if (info_ptr->color_type == PNG_COLOR_TYPE_GRAY ||
        info_ptr->color_type == PNG_COLOR_TYPE_RGB) {
      pnm_type = PBM_TYPE;
    } else
      if (info_ptr->color_type == PNG_COLOR_TYPE_PALETTE) {
        pnm_type = PBM_TYPE;
        if (info_ptr->valid & PNG_INFO_tRNS) {
          for (i = 0 ; i < info_ptr->num_trans ; i++) {
            if (info_ptr->trans[i] != 0 && info_ptr->trans[i] != maxval) {
              pnm_type = PGM_TYPE;
              break;
            }
          }
        }
      } else {
        if (maxval == 1)
          pnm_type = PBM_TYPE;
        else
          pnm_type = PGM_TYPE;
      }
  } else {
    if (info_ptr->color_type == PNG_COLOR_TYPE_GRAY ||
        info_ptr->color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
      if (info_ptr->bit_depth == 1) {
        pnm_type = PBM_TYPE;
      } else {
        pnm_type = PGM_TYPE;
      }
    } else
    if (info_ptr->color_type == PNG_COLOR_TYPE_PALETTE) {
      pnm_type = PGM_TYPE;
      for (i = 0 ; i < info_ptr->num_palette ; i++) {
        if (iscolor (info_ptr->palette[i])) {
          pnm_type = PPM_TYPE;
          break;
        }
      }
    } else {
      pnm_type = PPM_TYPE;
    }
  }

  if (verbose)
    pm_message ("writing a %s file",
                pnm_type == PBM_TYPE ? "PBM" :
                pnm_type == PGM_TYPE ? "PGM" :
                pnm_type == PPM_TYPE ? "PPM" :
                                               "UNKNOWN!");

  pnm_writepnminit (stdout, info_ptr->width, info_ptr->height, maxval,
                    pnm_type, FALSE);

  for (y = 0 ; y < info_ptr->height ; y++) {
    png_pixel = png_image[y];
    pnm_pixel = row;
    for (x = 0 ; x < info_ptr->width ; x++) {
      c = get_png_val (png_pixel);
      switch (info_ptr->color_type) {
        case PNG_COLOR_TYPE_GRAY:
          store_pixel (pnm_pixel, c, c, c,
                       (info_ptr->valid & PNG_INFO_tRNS) &&
                       c == info_ptr->trans_values.gray ?
                       0 : maxval);
          break;

        case PNG_COLOR_TYPE_GRAY_ALPHA:
          a = get_png_val (png_pixel);
          store_pixel (pnm_pixel, c, c, c, a);
          break;

        case PNG_COLOR_TYPE_PALETTE:
          store_pixel (pnm_pixel, info_ptr->palette[c].red,
                       info_ptr->palette[c].green, info_ptr->palette[c].blue,
                       (info_ptr->valid & PNG_INFO_tRNS) &&
                        c<info_ptr->num_trans ?
                        info_ptr->trans[c] : maxval);
          break;

        case PNG_COLOR_TYPE_RGB:
          c2 = get_png_val (png_pixel);
          c3 = get_png_val (png_pixel);
          store_pixel (pnm_pixel, c, c2, c3,
                       (info_ptr->valid & PNG_INFO_tRNS) &&
                        c  == info_ptr->trans_values.red &&
                        c2 == info_ptr->trans_values.green &&
                        c3 == info_ptr->trans_values.blue ?
                        0 : maxval);
          break;

        case PNG_COLOR_TYPE_RGB_ALPHA:
          c2 = get_png_val (png_pixel);
          c3 = get_png_val (png_pixel);
          a = get_png_val (png_pixel);
          store_pixel (pnm_pixel, c, c2, c3, a);
          break;

        default:
          pnm_freerow (row);
          for (i = 0 ; i < info_ptr->height ; i++)
            free (png_image[i]);
          if (alpha == alpha_appended)
            free (alpha_channel);
          free (png_image);
          png_read_destroy (png_ptr, info_ptr, (png_info *)0);
          free (png_ptr);
          free (info_ptr);
          pm_error ("unknown PNG color type");
      }
      pnm_pixel++;
    }
    pnm_writepnmrow (stdout, row, info_ptr->width, maxval, pnm_type, FALSE);
  }

  fflush(stdout);

  /* GRR 970727:  append alpha channel (if any) as PGM stream, if requested */
  if (alpha == alpha_appended && alpha_exists) {
    pnm_writepnminit (stdout, info_ptr->width, info_ptr->height, maxval,
                      PGM_TYPE, FALSE);
    alpha_pixel = alpha_channel;  /* reset pointer after store_pixel() calls */
    for (y = 0 ; y < info_ptr->height ; y++) {
      pnm_pixel = row;
      for (x = 0 ; x < info_ptr->width ; x++) {
        c = *alpha_pixel++;
        PPM_ASSIGN (*pnm_pixel, c, c, c);
        pnm_pixel++;
      }
      pnm_writepnmrow (stdout, row, info_ptr->width, maxval, PGM_TYPE, FALSE);
    }
  }

  pnm_freerow (row);
  for (y = 0 ; y < info_ptr->height ; y++)
    free (png_image[y]);
  if (alpha == alpha_appended)
    free (alpha_channel);
  free (png_image);
  png_read_destroy (png_ptr, info_ptr, (png_info *)0);
  free (png_ptr);
  free (info_ptr);
}

#ifdef __STDC__
int main (int argc, char *argv[])
#else
int main (argc, argv)
int argc;
char *argv[];
#endif
{
  FILE *ifp, *tfp;
  int argn;

  char *usage = "[-verbose] [-rgba | -alpha | -mix] [-background color] ...\n\
             ... [-gamma value] [-text file] [-time] [pngfile]";

  pnm_init (&argc, argv);
  argn = 1;

  while (argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0') {
    if (pm_keymatch (argv[argn], "-verbose", 2)) {
      verbose = TRUE;
    } else
    if (pm_keymatch (argv[argn], "-rgba", 2)) {   /* GRR 970727 */
      alpha = alpha_appended;
    } else
    if (pm_keymatch (argv[argn], "-alpha", 2)) {
      alpha = alpha_only;
    } else
    if (pm_keymatch (argv[argn], "-mix", 2)) {
      alpha = mix;
    } else
    if (pm_keymatch (argv[argn], "-background", 2)) {
      background = 1;
      if (++argn < argc)
        backstring = argv[argn];
      else
        pm_usage (usage);
    } else
    if (pm_keymatch (argv[argn], "-gamma", 2)) {
      if (++argn < argc)
        sscanf (argv[argn], "%f", &displaygamma);
      else
        pm_usage (usage);
    } else
    if (pm_keymatch (argv[argn], "-text", 3)) {
      text = TRUE;
      if (++argn < argc)
        text_file = argv[argn];
      else
        pm_usage (usage);
    } else
    if (pm_keymatch (argv[argn], "-time", 3)) {
      mtime = TRUE;
    } else {
      fprintf(stderr,
        "pngtopnm, version %s.\n  Compiled with libpng version %s.\n\n",
        VERSION, PNG_LIBPNG_VER_STRING);
      pm_usage (usage);
    }
    argn++;
  }

  if (argn != argc) {
    ifp = pm_openr (argv[argn]);
    ++argn;
  } else {
    ifp = stdin;
  }
  if (argn != argc)
    pm_usage (usage);

  if (text)
    tfp = pm_openw (text_file);
  else
    tfp = NULL;

  convertpng (ifp, tfp);

  if (text)
    pm_close (tfp);
  pm_close (ifp);
  pm_close (stdout);
  exit (0);
}
