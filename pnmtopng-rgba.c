/*
** pnmtopng.c -
** read a portable anymap and produce a Portable Network Graphics file
**
** derived from pnmtorast.c (c) 1990,1991 by Jef Poskanzer and some
** parts derived from ppmtogif.c by Marcel Wijkstra <wijkstra@fwi.uva.nl>
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
*/

#define VERSION "2.34-rgba of 31 July 1997"

#include "pnm.h"
#include "png.h"

#include "ppmcmap.h"

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef NONE
#define NONE 0
#endif
#define MAXCOLORS 256
#define MAXCOMMENTS 256

/* function prototypes */
#ifdef __STDC__
static int closestcolor (pixel color, colorhist_vector chv, int colors, xelval maxval);
static void read_text (png_info *info_ptr, FILE *tfp);
static void convertpnm (FILE *ifp, FILE *afp, FILE *tfp);
int main (int argc, char *argv[]);
#endif

static int verbose = FALSE;
static int interlace = FALSE;
static int downscale = FALSE;
static int transparent = -1;
static char *transstring;
static int alpha = FALSE;
static char *alpha_file;
static int background = -1;
static char *backstring;
static float gamma = -1.0;
static int hist = FALSE;
static float chroma_wx = -1.0;
static float chroma_wy = -1.0;
static float chroma_rx = -1.0;
static float chroma_ry = -1.0;
static float chroma_gx = -1.0;
static float chroma_gy = -1.0;
static float chroma_bx = -1.0;
static float chroma_by = -1.0;
static int phys_x = -1.0;
static int phys_y = -1.0;
static int phys_unit = -1.0;
static int text = FALSE;
static int ztxt = FALSE;
static char *text_file;
static int mtime = FALSE;
static char *date_string;
static char *time_string;
static struct tm time_struct;
static int filter = -1;
static int compression = -1;
static int force = FALSE;

#ifdef __STDC__
static int closestcolor (pixel color, colorhist_vector chv, int colors, xelval maxval)
#else
static int closestcolor (color, chv, colors, maxval)
pixel color;
colorhist_vector chv;
int colors;
xelval maxval;
#endif
{
  int i, r, g, b, d, imin, dmin;

  r = (int)PPM_GETR (color) * 255 / maxval;
  g = (int)PPM_GETG (color) * 255 / maxval;
  b = (int)PPM_GETB (color) * 255 / maxval;

  dmin = 1000000;
  for (i = 0 ; i < colors ; i++) {
    d = (r - PPM_GETR (chv[i].color)) * (r - PPM_GETR (chv[i].color))+
        (g - PPM_GETG (chv[i].color)) * (g - PPM_GETG (chv[i].color))+
        (b - PPM_GETB (chv[i].color)) * (b - PPM_GETB (chv[i].color));
    if (d < dmin) {
      dmin = d;
      imin = i;
    }
  }
  return imin;
}

#ifdef __STDC__
static void read_text (png_info *info_ptr, FILE *tfp)
#else
static void read_text (info_ptr, tfp)
png_info *info_ptr;
FILE *tfp;
#endif
{
  char textline[256];
  int textpos;
  int i, j;
  int c;
  char *cp;

  info_ptr->text = (png_text *)malloc (MAXCOMMENTS * sizeof (png_text));
  j = 0;
  textpos = 0;
  while ((c = getc (tfp)) != EOF) {
    if (c != '\n' && c != EOF) {
      textline[textpos++] = c;
    } else {
      textline[textpos++] = '\0';
      if ((textline[0] != ' ') && (textline[0] != '\t')) {
        /* the following is a not that accurate check on Author or Title */
        if ((!ztxt) || (textline[0] == 'A') || (textline[0] == 'T'))
          info_ptr->text[j].compression = -1;
        else
          info_ptr->text[j].compression = 0;
        cp = malloc (textpos);
        info_ptr->text[j].key = cp;
        i = 0;
        if (textline[0] == '"') {
          i++;
          while (textline[i] != '"' && textline[i] != '\n')
            (*(cp++) = textline[i++]);
          i++;
        } else {
          while (textline[i] != ' ' && textline[i] != '\t' && textline[i] != '\n')
            (*(cp++) = textline[i++]);
        }
        *(cp++) = '\0';
        cp = malloc (textpos);
        info_ptr->text[j].text = cp;
        while (textline[i] == ' ' || textline[i] == '\t')
          i++;
        strcpy (cp, &textline[i]);
        info_ptr->text[j].text_length = strlen (cp);
        j++;
      }        else {
        j--;
        cp = malloc (info_ptr->text[j].text_length + textpos);
        strcpy (cp, info_ptr->text[j].text);
        strcat (cp, "\n");
        info_ptr->text[j].text = cp;
        i = 0;
        while (textline[i] == ' ' || textline[i] == '\t')
        i++;
        strcat (cp, &textline[i]);
        info_ptr->text[j].text_length = strlen (cp);
        j++;
      }
      textpos = 0;
    }
  } /* end while */
  info_ptr->num_text = j;
}

#ifdef __STDC__
static void convertpnm (FILE *ifp, FILE *afp, FILE *tfp)
#else
static void convertpnm (ifp, afp, tfp)
FILE *ifp;
FILE *afp;
FILE *tfp;
#endif
{
  xel **xels;
  xel p;
  int rows, cols, format, pnm_type;
  xelval maxval;
  xelval maxmaxval = 255;
  xelval value;
  unsigned long scaleval;   /* GRR:  was double (unnecessarily) */
  pixel transcolor;
  int sbitval;
  int isgray;
  int mayscale;
  pixel backcolor;

  png_struct *png_ptr;
  png_info *info_ptr;

  png_color palette[MAXCOLORS];
  png_byte trans[MAXCOLORS];
  png_uint_16 histogram[MAXCOLORS];
  png_byte *line;
  png_byte *pp;
  int pass;
  int color;
  gray **alpha_mask;
  gray alpha_maxval;
  int alpha_rows;
  int alpha_cols;
  int alpha_sbitval;
  int alpha_trans;
  gray *alphas_of_color[MAXCOLORS];
  int alphas_of_color_cnt[MAXCOLORS];
  int alphas_first_index[MAXCOLORS+1];
  int mapping[MAXCOLORS];
  int palette_size;
  colorhist_vector chv;
  colorhash_table cht;
  int depth, colors;
  int fulldepth;
  int x, y;
  int i, j;

  png_ptr = (png_struct *)malloc (sizeof (png_struct));
  info_ptr = (png_info *)malloc (sizeof (png_info));

  if (png_ptr == NULL || info_ptr == NULL)
    pm_error ("Cannot allocate LIBPNG structures");

  xels = pnm_readpnm (ifp, &cols, &rows, &maxval, &format);

  pnm_type = PNM_FORMAT_TYPE (format);
  if (pnm_type == PGM_TYPE)
    maxmaxval = PGM_MAXMAXVAL;
  else if (pnm_type == PPM_TYPE)
    maxmaxval = PPM_MAXMAXVAL;

  if (transparent > 0)   /* -1 or 1 are the only possibilities so far */
    transcolor = ppm_parsecolor (transstring, maxmaxval);

  if (alpha) {
    /* GRR 970726:  if PGM alpha mask appended, file pointer in ifp should be
     *  ready to go after pnm_readpnm() above. */
    alpha_mask = pgm_readpgm (afp, &alpha_cols, &alpha_rows, &alpha_maxval);
    if (alpha_cols != cols || alpha_rows != rows) {
      pm_error ("dimensions for image and alpha mask do not agree");
    }
    /* check if the alpha mask can be represented by a single transparency
       value (i.e. all colors fully opaque except one fully transparent;
       the transparent color may not also occur as fully opaque.
       we have to do this before any scaling occurs, since alpha is only
       possible with 8 and 16 bit */
    /* first find the possible candidate */
    alpha_trans = FALSE;
    for (y = 0 ; y < rows && !alpha_trans ; y++)
      for (x = 0 ; x < cols && !alpha_trans ; x++) {
        if (alpha_mask[y][x] == 0) {
          if (transparent < 0) {
            alpha_trans = TRUE;
            transparent = 2;
            transcolor = xels[y][x];
          }
        }
      }
    /* if alpha_trans is TRUE check the whole image */
    for (y = 0 ; y < rows && alpha_trans ; y++)
      for (x = 0 ; x < cols && alpha_trans ; x++) {
        if (alpha_mask[y][x] == 0) { /* transparent one */
          if (pnm_type == PPM_TYPE) {
            if (!PPM_EQUAL (xels[y][x], transcolor))
              alpha_trans = FALSE;
          } else {
            if (PNM_GET1 (xels[y][x]) != PNM_GET1 (transcolor))
              alpha_trans = FALSE;
          }
        } else /* is it fully opaque ? */
        if (alpha_mask[y][x] != alpha_maxval) {
          alpha_trans = FALSE;
        } else /* does the transparent color also exists fully opaque */
        if (pnm_type == PPM_TYPE) {
          if (PPM_EQUAL (xels[y][x], transcolor))
            alpha_trans = FALSE;
        } else {
          if (PNM_GET1 (xels[y][x]) == PNM_GET1 (transcolor))
            alpha_trans = FALSE;
        }
      }
    if (alpha_trans && !force) {
      if (verbose)
        pm_message ("converting alpha mask to transparency index");
      alpha = FALSE;
    } else {
      transparent = -1;
    }
  }

  /* GRR 970730:  gcc 2.7.0 -fomit-frame-pointer causes stack corruption here */
  if (background > -1)  /* scale to maxval later: */
    backcolor = ppm_parsecolor (backstring, maxmaxval);

  /* first of all, check if we have a grayscale image written as PPM */

  isgray = TRUE;
  if (pnm_type == PPM_TYPE) {
    for (y = 0 ; y < rows && isgray ; y++)
      for (x = 0 ; x < cols && isgray ; x++) {
        p = xels[y][x];
        if (PPM_GETR (p) != PPM_GETG (p) || PPM_GETG (p) != PPM_GETB (p))
          isgray = FALSE;
      }
    if (isgray && !force)
      pnm_type = PGM_TYPE;
  }

  /* handle `odd' maxvalues */

  sbitval = 0;
  if (pnm_type != PBM_TYPE || alpha) {
    if (maxval > 65535 && !downscale)
      pm_error ("can only handle files up to 16-bit (use -downscale to override");

    if (maxval<65536) {
      sbitval = pm_maxvaltobits (maxval);
      if (maxval != pm_bitstomaxval (sbitval))
        sbitval = 0;
    }

    if (maxval != 255 && maxval != 65535 &&
        (alpha || pnm_type != PGM_TYPE ||
         (maxval != 1 && maxval != 3 && maxval != 15))) {
      if (!alpha && maxval == 7 && pnm_type == PGM_TYPE) {
        if (verbose)
          pm_message ("rescaling to 4-bit");
        scaleval = 15L;
      } else
      if (maxval<255) {
        if (verbose)
          pm_message ("rescaling to 8-bit");
        scaleval = 255L;
      } else {
        if (verbose)
          pm_message ("rescaling to 16-bit");
        scaleval = 65535L;
      }
      for (y = 0 ; y < rows ; y++)
        for (x = 0 ; x < cols ; x++) {
          p = xels[y][x];
          PPM_DEPTH (xels[y][x], p, maxval, scaleval);
        }
      if (transparent == 2)   /* "1" case (-transparent) handled below */
        PPM_DEPTH (transcolor, transcolor, maxval, scaleval);
      maxval = scaleval;
    } else {
      sbitval = 0; /* no scaling happened */
    }
  }

  /* GRR 970729:  now do a real scaling (better than ppm_parsecolor()) */
  if (maxval != 65535) {
    if (background > -1)
      PPM_DEPTH (backcolor, backcolor, maxmaxval, maxval);
    if (transparent == 1)   /* "2" case (-alpha) already done */
      PPM_DEPTH (transcolor, transcolor, maxmaxval, maxval);
  }

  /* check for 16-bit entries that are just scaled 8-bit entries, e.g.
     when converting an 8-bit palette TIFF to ppm */

  if (pnm_type != PBM_TYPE && maxval == 65535 && !force) {
    mayscale = TRUE;
    for (y = 0 ; y < rows && mayscale ; y++)
      for (x = 0 ; x < cols && mayscale ; x++) {
        p = xels[y][x];
        if (pnm_type == PGM_TYPE ?
            (PNM_GET1 (p)&0xff)*0x101 != PNM_GET1 (p) :
            (PPM_GETR (p)&0xff)*0x101 != PPM_GETR (p) ||
            (PPM_GETG (p)&0xff)*0x101 != PPM_GETG (p) ||
            (PPM_GETB (p)&0xff)*0x101 != PPM_GETB (p))
          mayscale = FALSE;
      }
    if (mayscale) {
      if (verbose)
        pm_message ("scaling to 8 bit (superflous 16 bit data)");
      for (y = 0 ; y < rows ; y++)
        for (x = 0 ; x < cols ; x++) {
          p = xels[y][x];
          if (pnm_type == PGM_TYPE) {
            PNM_ASSIGN1 (xels[y][x], PNM_GET1 (p)&0xff);
          } else {
            PPM_ASSIGN (xels[y][x], PPM_GETR (p)&0xff,  PPM_GETG (p)&0xff,
                        PPM_GETB (p)&0xff);
          }
        }
      maxval = 255;
      if (transparent > 0) {
        p = transcolor;
        if (pnm_type == PGM_TYPE) {
          PNM_ASSIGN1 (transcolor, PNM_GET1 (p)&0xff);
        } else {
          PPM_ASSIGN (transcolor, PPM_GETR (p)&0xff,  PPM_GETG (p)&0xff,
                      PPM_GETB (p)&0xff);
        }
      }

      if (sbitval > 0) sbitval >>= 1;
    }
  }

  /* scale alpha mask to match bit depth of image */

  if (alpha) {
    if (alpha_maxval<65536) {
      alpha_sbitval = pm_maxvaltobits (alpha_maxval);
      if (maxval != pm_bitstomaxval (sbitval))
        alpha_sbitval = 0;
    } else
      alpha_sbitval = 0;

    if (alpha_maxval != maxval) {
      if (verbose)
        pm_message ("rescaling alpha mask to match image bit depth");
      for (y = 0 ; y < rows ; y++)
        for (x = 0 ; x < cols ; x++)
          alpha_mask[y][x] = (alpha_mask[y][x] * maxval + alpha_maxval / 2) /
                                                                 alpha_maxval;
      alpha_maxval = maxval;
    } else {
      alpha_sbitval = 0; /* no scaling happened */
    }
  }

  /* now do scaling for bit depth 4, 2 and 1, only for grayscale pics, when
     we don't have an alpha channel */

  if (!alpha && pnm_type == PGM_TYPE && !force) {
    if (maxval == 255) {
      mayscale = TRUE;
      for (y = 0 ; y < rows && mayscale ; y++)
        for (x = 0 ; x < cols && mayscale ; x++) {
          if ((PNM_GET1 (xels[y][x]) & 0xf) * 0x11 != PNM_GET1 (xels[y][x]))
            mayscale = FALSE;
        }
      if (mayscale) {
        for (y = 0 ; y < rows ; y++)
          for (x = 0 ; x < cols ; x++) {
            PNM_ASSIGN1 (xels[y][x], PNM_GET1 (xels[y][x])&0xf);
          }

        if (transparent > 0) {
          PNM_ASSIGN1 (transcolor, PNM_GET1 (transcolor)&0xf);
        }

        maxval = 15;
        if (sbitval > 0) sbitval >>= 1;
      }
    }

    if (maxval == 15) {
      mayscale = TRUE;
      for (y = 0 ; y < rows && mayscale ; y++)
        for (x = 0 ; x < cols && mayscale ; x++) {
          if ((PNM_GET1 (xels[y][x])&3) * 5 != PNM_GET1 (xels[y][x]))
            mayscale = FALSE;
        }
      if (mayscale) {
        for (y = 0 ; y < rows ; y++)
          for (x = 0 ; x < cols ; x++) {
            PNM_ASSIGN1 (xels[y][x], PNM_GET1 (xels[y][x]) & 3);
          }
        if (transparent>0) {
          PNM_ASSIGN1 (transcolor, PNM_GET1 (transcolor) & 3);
        }
        maxval = 3;
        if (sbitval > 0) sbitval >>= 1;
      }
    }

    if (maxval == 3) {
      mayscale = TRUE;
      for (y = 0 ; y < rows && mayscale ; y++)
        for (x = 0 ; x < cols && mayscale ; x++) {
          if ((PNM_GET1 (xels[y][x])&1) * 3 != PNM_GET1 (xels[y][x]))
            mayscale = FALSE;
        }
      if (mayscale) {
        for (y = 0 ; y < rows ; y++)
          for (x = 0 ; x < cols ; x++) {
            PNM_ASSIGN1 (xels[y][x], PNM_GET1 (xels[y][x])&1);
          }
        if (transparent > 0) {
          PNM_ASSIGN1 (transcolor, PNM_GET1 (transcolor) & 1);
        }
        maxval = 1;
        sbitval = 0;
      }
    }
  }

  /* decide if we can write a palette file, either if we have <= 256 colors
     or if we want alpha, we have <= 256 color/transparency pairs, this
     makes sense with grayscale images also */

  if (alpha && pnm_type != PPM_TYPE) {
    /* we want to apply ppm functions to grayscale images also, for the
       palette check. To do this, we have to copy the grayscale values to
       the rgb channels */
    for (y = 0 ; y < rows ; y++)
      for (x = 0 ; x < cols ; x++) {
        value = PNM_GET1 (xels[y][x]);
        PPM_ASSIGN (xels[y][x], value, value, value);
      }
  }

  if ((maxval == 255) && (pnm_type == PPM_TYPE || alpha) && !force) {
    if (verbose)
      pm_message ("computing colormap...");
    chv = ppm_computecolorhist (xels, cols, rows, MAXCOLORS, &colors);
    if (verbose) {
      if (chv == (colorhist_vector) 0) {
        pm_message ("too many colors - proceeding to write a 24-bit non-mapped");
        pm_message ("image file.  If you want %d bits, try doing a 'ppmquant %d'.",
                    pm_maxvaltobits (MAXCOLORS-1), MAXCOLORS);
      } else {
        pm_message ("%d colors found", colors);
      }
    }

    if (chv != (colorhist_vector) 0) {

#ifdef INCONSISTENT  /* GRR:  moved below alpha-count section */
      /* add possible background color to palette */
      if (background > -1) {
        cht = ppm_colorhisttocolorhash (chv, colors);
        background = ppm_lookupcolor (cht, &backcolor);
        if (background == -1) {
          if (colors < MAXCOLORS) {
            background = colors;
            ppm_addtocolorhist (chv, &colors, MAXCOLORS, &backcolor, colors, colors);
            if (verbose)
              pm_message ("added background color to palette");
          } else {
            background = closestcolor (backcolor, chv, colors, maxval);
            if (verbose)
              pm_message ("no room in palette for background color; using closest match instead");
          }
        }
        ppm_freecolorhash (cht);
      }
#endif /* INCONSISTENT */

      if (alpha) {
        /* now check if there are different alpha values for the same color
           and if all pairs still fit into 256 (MAXCOLORS) entries; malloc
           one extra for possible background color */
        cht = ppm_colorhisttocolorhash (chv, colors);
        for (i = 0 ; i < colors+1 ; i++) {
          /* GRR MEMORY LEAK:  these are never freed: */
          if ((alphas_of_color[i] =
              (gray *)malloc (MAXCOLORS * sizeof (gray))) == NULL)
            pm_error ("out of memory allocating alpha/palette entries");
          alphas_of_color_cnt[i] = 0;
        }
        for (y = 0 ; y < rows ; y++)
          for (x = 0 ; x < cols ; x++) {
            color = ppm_lookupcolor (cht, &xels[y][x]);
            for (i = 0 ; i < alphas_of_color_cnt[color] ; i++) {
              if (alpha_mask[y][x] == alphas_of_color[color][i])
                break;
            }
            if (i == alphas_of_color_cnt[color]) {
              alphas_of_color[color][i] = alpha_mask[y][x];
              alphas_of_color_cnt[color]++;
            }
          }

        ppm_freecolorhash (cht); /* built again somewhere below */

#if 0
        /* GRR 970730:  The following is unnecessary.  PNG Spec, section 10.7:
         *  ``The background color given by bKGD is not to be considered
         *    transparent, even if it happens to match the color given by
         *    tRNS (or, in the case of an indexed-color image, refers to a
         *    palette index that is marked as transparent by tRNS).''
         */
        if (background > -1) {
          for (i = 0 ; i < alphas_of_color_cnt[background] ; i++) {
            if (alphas_of_color[background][i] == maxval) /* background is opaque */
              break;
          }
          if (i == alphas_of_color_cnt[background]) {
            alphas_of_color[background][i] = maxval;
            alphas_of_color_cnt[background]++;
          }
        }
#endif /* 0 */

        alphas_first_index[0] = 0;
        for (i = 0 ; i < colors ; i++)
          alphas_first_index[i+1] = alphas_first_index[i] + alphas_of_color_cnt[i];
        if (alphas_first_index[colors] > MAXCOLORS) {
          if (verbose)
            pm_message ("too many color/transparency pairs (%d), writing a non-mapped file",
              alphas_first_index[colors]);
          ppm_freecolorhist (chv);
          chv = NULL;
        } else if (verbose)
          pm_message ("%d color/transparency pairs found",
            alphas_first_index[colors]);
      }

#ifndef INCONSISTENT
      /* add possible background color to palette */
      if (background > -1) {
        cht = ppm_colorhisttocolorhash (chv, colors);
        background = ppm_lookupcolor (cht, &backcolor);
        if (background == -1) {
          if ((!alpha && colors < MAXCOLORS) ||
               (alpha && alphas_first_index[colors] < MAXCOLORS))
          {
            background = colors;
            ppm_addtocolorhist (chv, &colors, MAXCOLORS, &backcolor, colors, colors);
            if (alpha) {
              alphas_of_color[background][0] = maxval;  /* opaque */
              alphas_of_color_cnt[background] = 1;      /* unique */
              alphas_first_index[colors] = alphas_first_index[background] + 1;
            }
            if (verbose)
              pm_message ("added background color to palette");
          } else {
            background = closestcolor (backcolor, chv, colors, maxval);
            if (verbose)
              pm_message ("no room in palette for background color; using closest match instead");
          }
        }
        ppm_freecolorhash (cht);
      }
#endif /* !INCONSISTENT */
    }
  } else
    chv = NULL;

  if (chv) {
    if (alpha)
      palette_size = alphas_first_index[colors];
    else
      palette_size = colors;

    if (palette_size <= 2)
      depth = 1;
    else if (palette_size <= 4)
      depth = 2;
    else if (palette_size <= 16)
      depth = 4;
    else
      depth = 8;
    fulldepth = depth;
  } else {
    /* non-mapped color or grayscale */

    if (maxval == 65535)
      depth = 16;
    else if (maxval == 255)
      depth = 8;
    else if (maxval == 15)
      depth = 4;
    else if (maxval == 3)
      depth = 2;
    else if (maxval == 1)
      depth = 1;
    else
      pm_error (" (can't happen) undefined maxvalue");

    if (alpha) {
      if (pnm_type == PPM_TYPE)
        fulldepth = 4*depth;
      else
        fulldepth = 2*depth;
    } else if (pnm_type == PPM_TYPE)
      fulldepth = 3*depth;
  }

  if (verbose) {
    pm_message ("writing a%s %d-bit %s%s file%s",
                fulldepth == 8? "n" : "", fulldepth,
                chv? "palette": (pnm_type == PPM_TYPE ? "RGB" : "gray"),
                alpha? (chv? "+transparency" : (pnm_type == PPM_TYPE? "A" :
                "+alpha")) : "", interlace? " (interlaced)" : "");
  }

  /* now write the file */

  if (setjmp (png_ptr->jmpbuf)) {
    png_write_destroy (png_ptr);
    free (png_ptr);
    free (info_ptr);
    pm_error ("setjmp returns error condition");
  }

  png_write_init (png_ptr);
  png_info_init (info_ptr);
  png_init_io (png_ptr, stdout);
  info_ptr->width = cols;
  info_ptr->height = rows;
  info_ptr->bit_depth = depth;

  if (chv != NULL)
    info_ptr->color_type = PNG_COLOR_TYPE_PALETTE;
  else
  if (pnm_type == PPM_TYPE)
    info_ptr->color_type = PNG_COLOR_TYPE_RGB;
  else
    info_ptr->color_type = PNG_COLOR_TYPE_GRAY;

  if (alpha && info_ptr->color_type != PNG_COLOR_TYPE_PALETTE)
    info_ptr->color_type |= PNG_COLOR_MASK_ALPHA;

  info_ptr->interlace_type = interlace;

  /* gAMA chunk */
  if (gamma != -1.0) {
    info_ptr->valid |= PNG_INFO_gAMA;
    info_ptr->gamma = gamma;
  }

  /* cHRM chunk */
  if (chroma_wx != -1.0) {
    info_ptr->valid |= PNG_INFO_cHRM;
    info_ptr->x_white = chroma_wx;
    info_ptr->y_white = chroma_wy;
    info_ptr->x_red = chroma_rx;
    info_ptr->y_red = chroma_ry;
    info_ptr->x_green = chroma_gx;
    info_ptr->y_green = chroma_gy;
    info_ptr->x_blue = chroma_bx;
    info_ptr->y_blue = chroma_by;
  }

  /* pHYS chunk */
  if (phys_unit != -1.0) {
    info_ptr->valid |= PNG_INFO_pHYs;
    info_ptr->x_pixels_per_unit = phys_x;
    info_ptr->y_pixels_per_unit = phys_y;
    info_ptr->phys_unit_type = phys_unit;
  }

  /* PLTE chunk */
  if (info_ptr->color_type == PNG_COLOR_TYPE_PALETTE) {
    cht = ppm_colorhisttocolorhash (chv, colors);
    /* before creating palette figure out the transparent color */
    if (transparent > 0) {
      transparent = ppm_lookupcolor (cht, &transcolor);
      if (transparent == -1) {
        transparent = closestcolor (transcolor, chv, colors, maxval);
        transcolor = chv[transparent].color;
      }
      /* now put transparent color in entry 0 by swapping */
      chv[transparent].color = chv[0].color;
      chv[0].color = transcolor;
      /* check if background color was by bad luck part of swap */
      if (background == transparent)
        background = 0;
      else if (background == 0)
        background = transparent;
      /* rebuild hashtable */
      ppm_freecolorhash (cht);
      cht = ppm_colorhisttocolorhash (chv, colors);
      transparent = 0;
      trans[0] = 0; /* fully transparent */
      info_ptr->valid |= PNG_INFO_tRNS;
      info_ptr->trans = trans;
      info_ptr->num_trans = 1;
    }

    /* creating PNG palette (tRNS *not* valid) */
    if (alpha) {
      int bot_idx = 0;
      int top_idx = alphas_first_index[colors] - 1;

      /* GRR: remap palette indices so opaque entries are last (omittable) */
      for (i = 0;  i < colors;  ++i) {
        for (j = alphas_first_index[i];  j < alphas_first_index[i+1];  ++j) {
          if (alphas_of_color[i][j-alphas_first_index[i]] == 255)
            mapping[j] = top_idx--;
          else
            mapping[j] = bot_idx++;
        }
      }
      /* indices should have just crossed paths */
      if (bot_idx != top_idx + 1)
        pm_error ("internal inconsistency: remapped bot_idx = %d, top_idx = %d",
                  bot_idx, top_idx);
      for (i = 0 ; i < colors ; i++) {
        for (j = alphas_first_index[i];j<alphas_first_index[i+1] ; j++) {
          palette[mapping[j]].red = PPM_GETR (chv[i].color);
          palette[mapping[j]].green = PPM_GETG (chv[i].color);
          palette[mapping[j]].blue = PPM_GETB (chv[i].color);
          trans[mapping[j]] = alphas_of_color[i][j-alphas_first_index[i]];
        }
      }
      info_ptr->valid |= PNG_INFO_tRNS;
      info_ptr->trans = trans;
      info_ptr->num_trans = bot_idx;   /* GRR 970731:  omit opaque values */
      pm_message ("writing %d non-opaque transparency values", bot_idx);
    } else {
      for (i = 0 ; i < MAXCOLORS ; i++) {
        palette[i].red = PPM_GETR (chv[i].color);
        palette[i].green = PPM_GETG (chv[i].color);
        palette[i].blue = PPM_GETB (chv[i].color);
      }
    }
    info_ptr->valid |= PNG_INFO_PLTE;
    info_ptr->palette = palette;
    info_ptr->num_palette = palette_size;

    /* creating hIST chunk */
    if (hist) {
      for (i = 0 ; i < MAXCOLORS ; i++)
        histogram[i] = chv[i].value;
      info_ptr->valid |= PNG_INFO_hIST;
      info_ptr->hist = histogram;
      if (verbose)
        pm_message ("histogram created");
    }
    ppm_freecolorhist (chv);

  } else /* color_type != PNG_COLOR_TYPE_PALETTE */
    if (info_ptr->color_type == PNG_COLOR_TYPE_GRAY) {
      if (transparent > 0) {
        info_ptr->valid |= PNG_INFO_tRNS;
        info_ptr->trans_values.gray = PNM_GET1 (transcolor);
      }
    } else
    if (info_ptr->color_type == PNG_COLOR_TYPE_RGB) {
      if (transparent > 0) {
        info_ptr->valid |= PNG_INFO_tRNS;
        info_ptr->trans_values.red = PPM_GETR (transcolor);
        info_ptr->trans_values.green = PPM_GETG (transcolor);
        info_ptr->trans_values.blue = PPM_GETB (transcolor);
      }
    } else {
      if (transparent > 0)
        pm_error (" (can't happen) transparency AND alpha");
    }

  /* bKGD chunk */
  if (background > -1) {
    info_ptr->valid |= PNG_INFO_bKGD;
    if (info_ptr->color_type == PNG_COLOR_TYPE_PALETTE) {
      if (alpha)
#if 0   /* GRR:  see PNG spec, section 10.7, quoted above */
        for (i = 0 ; i < alphas_of_color_cnt[background] ; i++) {
          if (alphas_of_color[background][i] == maxval) {
            info_ptr->background.index = alphas_first_index[background]+i;
            break;
          }
        }
#else
        info_ptr->background.index = mapping[alphas_first_index[background]];
#endif
      else
        info_ptr->background.index = background;
    } else
    if (info_ptr->color_type == PNG_COLOR_TYPE_RGB ||
        info_ptr->color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
      info_ptr->background.red = PPM_GETR (backcolor);
      info_ptr->background.green = PPM_GETG (backcolor);
      info_ptr->background.blue = PPM_GETB (backcolor);
    } else
    if (info_ptr->color_type == PNG_COLOR_TYPE_GRAY ||
        info_ptr->color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
      info_ptr->background.gray = PNM_GET1 (backcolor);
    }
  }

  /* sBIT chunk */
  if (sbitval != 0 || alpha && alpha_sbitval != 0) {
    info_ptr->valid |= PNG_INFO_sBIT;

    if (sbitval == 0)
      sbitval = pm_maxvaltobits (maxval);
    if (alpha_sbitval == 0)
      alpha_sbitval = pm_maxvaltobits (maxval);

    if (info_ptr->color_type & PNG_COLOR_MASK_COLOR) {
      info_ptr->sig_bit.red   = sbitval;
      info_ptr->sig_bit.green = sbitval;
      info_ptr->sig_bit.blue  = sbitval;
    } else {
      info_ptr->sig_bit.gray = sbitval;
    }

    if (info_ptr->color_type & PNG_COLOR_MASK_ALPHA) {
      info_ptr->sig_bit.alpha = alpha_sbitval;
    }
  }

  /* tEXT and zTXT chunks */
  if ((text) || (ztxt)) {
    read_text (info_ptr, tfp);
  }

  /* tIME chunk */
  if (mtime) {
    info_ptr->valid |= PNG_INFO_tIME;
    sscanf (date_string, "%d-%d-%d", &time_struct.tm_year,
                                     &time_struct.tm_mon,
                                     &time_struct.tm_mday);
    if (time_struct.tm_year > 1900)
      time_struct.tm_year -= 1900;
    time_struct.tm_mon--; /* tm has monthes 0..11 */
    sscanf (time_string, "%d:%d:%d", &time_struct.tm_hour,
                                     &time_struct.tm_min,
                                     &time_struct.tm_sec);
    png_convert_from_struct_tm (&info_ptr->mod_time, &time_struct);
  }

  /* explicit filter-type (or none) required */
  if ((filter >= 0) && (filter <= 4))
  {
    png_set_filter (png_ptr, 0, filter);
  }

  /* zlib compression-level (or none) required */
  if ((compression >= 0) && (compression <= 9))
  {
    png_set_compression_level (png_ptr, compression);
  }

  /* write the png-info struct */
  png_write_info (png_ptr, info_ptr);

  if ((text) || (ztxt))
    /* prevent from being written twice with png_write_end */
    info_ptr->num_text = 0;

  if (mtime)
    /* prevent from being written twice with png_write_end */
    info_ptr->valid &= ~PNG_INFO_tIME;

  /* let libpng take care of a.o. bit-depth conversions */
  png_set_packing (png_ptr);

  line = malloc (cols*8); /* max: 3 color channels, one alpha channel, 16-bit */

  for (pass = 0;pass<png_set_interlace_handling (png_ptr);pass++) {
    for (y = 0 ; y < rows ; y++) {
      pp = line;
      for (x = 0 ; x < cols ; x++) {
        p = xels[y][x];
        if (info_ptr->color_type == PNG_COLOR_TYPE_GRAY ||
            info_ptr->color_type == PNG_COLOR_TYPE_GRAY_ALPHA) {
          if (depth == 16)
            *pp++ = PNM_GET1 (p)>>8;
          *pp++ = PNM_GET1 (p)&0xff;
        } else if (info_ptr->color_type == PNG_COLOR_TYPE_PALETTE) {
          color = ppm_lookupcolor (cht, &p);
          if (alpha) {
            for (i = alphas_first_index[color] ; i < alphas_first_index[color+1] ; i++)
              if (alpha_mask[y][x] == alphas_of_color[color][i - alphas_first_index[color]]) {
                color = mapping[i];  /* GRR 970731 */
                break;
              }
          }
          *pp++ = color;
        } else if (info_ptr->color_type == PNG_COLOR_TYPE_RGB ||
                   info_ptr->color_type == PNG_COLOR_TYPE_RGB_ALPHA) {
          if (depth == 16)
            *pp++ = PPM_GETR (p)>>8;
          *pp++ = PPM_GETR (p)&0xff;
          if (depth == 16)
            *pp++ = PPM_GETG (p)>>8;
          *pp++ = PPM_GETG (p)&0xff;
          if (depth == 16)
            *pp++ = PPM_GETB (p)>>8;
          *pp++ = PPM_GETB (p)&0xff;
        } else {
          pm_error (" (can't happen) undefined color_type");
        }
        if (info_ptr->color_type & PNG_COLOR_MASK_ALPHA) {
          if (depth == 16)
            *pp++ = alpha_mask[y][x]>>8;
          *pp++ = alpha_mask[y][x]&0xff;
        }
      }
      png_write_row (png_ptr, line);
    }
  }
  png_write_end (png_ptr, info_ptr);
  png_write_destroy (png_ptr);

  /* GRR:  free(png_ptr) segfaults under Linux if _BSD_SOURCE not defined;
   *   presumably due to jmpbuf problems in png_write_destroy().  fflush()
   *   saves output file, at least.  (Really should use new allocate/destroy
   *   functions instead of malloc/free for png_struct and png_info...) */
  fflush (stdout);

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
  FILE *ifp, *tfp, *afp;
  int argn;
  int alpha_appended = FALSE;   /* GRR 970726 */

  char *usage = "[-verbose] [-downscale] [-interlace] [-background color] ...\n\
             ... [-rgba | -alpha file | -transparent color] [-gamma value] ...\n\
             ... [-hist] [-chroma wx wy rx ry gx gy bx by] [-phys x y unit] ...\n\
             ... [-text file] [-ztxt file] [-time [yy]yy-mm-dd hh:mm:ss] ...\n\
             ... [-filter 0..4] [-compression 0..9] [-force] [pnmfile]";

  pnm_init (&argc, argv);
  argn = 1;

  while (argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0') {
    if (pm_keymatch (argv[argn], "-verbose", 2)) {
      verbose = TRUE;
    } else
    if (pm_keymatch (argv[argn], "-downscale", 2)) {
      downscale = TRUE;
    } else
    if (pm_keymatch (argv[argn], "-interlace", 2)) {
      interlace = TRUE;
    } else
    if (pm_keymatch (argv[argn], "-rgba", 2)) {   /* GRR 970726 */
      if (transparent > 0)
        pm_error ("-rgba and -transparent are mutually exclusive");
      if (alpha)
        pm_error ("-rgba and -alpha are mutually exclusive");
      alpha_appended = TRUE;
    } else
    if (pm_keymatch (argv[argn], "-alpha", 2)) {
      if (transparent > 0)
        pm_error ("-alpha and -transparent are mutually exclusive");
      if (alpha_appended)
        pm_error ("-rgba and -alpha are mutually exclusive");
      alpha = TRUE;
      if (++argn < argc)
        alpha_file = argv[argn];
      else
        pm_usage (usage);
    } else
    if (pm_keymatch (argv[argn], "-transparent", 3)) {
      if (alpha)
        pm_error ("-alpha and -transparent are mutually exclusive");
      if (alpha_appended)
        pm_error ("-rgba and -transparent are mutually exclusive");
      transparent = 1;
      if (++argn < argc)
        transstring = argv[argn];
      else
        pm_usage (usage);
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
        sscanf (argv[argn], "%f", &gamma);
      else
        pm_usage (usage);
    } else
    if (pm_keymatch (argv[argn], "-hist", 2)) {
      hist = TRUE;
    } else
    if (pm_keymatch (argv[argn], "-chroma", 3)) {
      if (++argn < argc)
        sscanf (argv[argn], "%f", &chroma_wx);
      else
        pm_usage (usage);
      if (++argn < argc)
        sscanf (argv[argn], "%f", &chroma_wy);
      else
        pm_usage (usage);
      if (++argn < argc)
        sscanf (argv[argn], "%f", &chroma_rx);
      else
        pm_usage (usage);
      if (++argn < argc)
        sscanf (argv[argn], "%f", &chroma_ry);
      else
        pm_usage (usage);
      if (++argn < argc)
        sscanf (argv[argn], "%f", &chroma_gx);
      else
        pm_usage (usage);
      if (++argn < argc)
        sscanf (argv[argn], "%f", &chroma_gy);
      else
        pm_usage (usage);
      if (++argn < argc)
        sscanf (argv[argn], "%f", &chroma_bx);
      else
        pm_usage (usage);
      if (++argn < argc)
        sscanf (argv[argn], "%f", &chroma_by);
      else
        pm_usage (usage);
    } else
    if (pm_keymatch (argv[argn], "-phys", 3)) {
      if (++argn < argc)
        sscanf (argv[argn], "%d", &phys_x);
      else
        pm_usage (usage);
      if (++argn < argc)
        sscanf (argv[argn], "%d", &phys_y);
      else
        pm_usage (usage);
      if (++argn < argc)
        sscanf (argv[argn], "%d", &phys_unit);
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
    if (pm_keymatch (argv[argn], "-ztxt", 2)) {
      ztxt = TRUE;
      if (++argn < argc)
        text_file = argv[argn];
      else
        pm_usage (usage);
    } else
    if (pm_keymatch (argv[argn], "-time", 3)) {
      mtime = TRUE;
      if (++argn < argc) {
        date_string = argv[argn];
        if (++argn < argc)
          time_string = argv[argn];
        else
          pm_usage (usage);
      } else {
        pm_usage (usage);
      }
    } else 
    if (pm_keymatch (argv[argn], "-filter", 3)) {
      if (++argn < argc)
      {
        sscanf (argv[argn], "%d", &filter);
        if ((filter < 0) || (filter > 4))
        {
          pm_message ("filter must be 0 (none), 1 (sub), 2 (up), 3 (avg) or 4 (paeth)");
          pm_usage (usage);
        }
      }
      else
        pm_usage (usage);
    } else
    if (pm_keymatch (argv[argn], "-compression", 3)) {
      if (++argn < argc)
      {
        sscanf (argv[argn], "%d", &compression);
        if ((compression < 0) || (compression > 9))
        {
          pm_message ("zlib compression must be between 0 (none) and 9 (max)");
          pm_usage (usage);
        }
      }
      else
        pm_usage (usage);
    } else
    if (pm_keymatch (argv[argn], "-force", 3)) {
      force = TRUE;
    } else {
      fprintf(stderr,
        "pnmtopng, version %s.\n  Compiled with libpng version %s.\n\n",
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

  if (alpha || alpha_appended) {
    if (alpha_appended) {   /* GRR 970726 */
      alpha = TRUE;
      afp = ifp;
    } else
      afp = pm_openr (alpha_file);
  } else
    afp = NULL;

  if ((text) || (ztxt))
    tfp = pm_openr (text_file);
  else
    tfp = NULL;

  convertpnm (ifp, afp, tfp);

  if (alpha && !alpha_appended)
    pm_close (afp);
  if ((text) || (ztxt))
    pm_close (tfp);

  pm_close (ifp);
  pm_close (stdout);

  exit (0);
}
