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

#ifndef TRUE
#  define TRUE 1
#  define FALSE 0
#endif

#ifndef MAX
#  define MAX(a,b)  ((a) > (b)? (a) : (b))
#  define MIN(a,b)  ((a) < (b)? (a) : (b))
#endif

#ifdef DEBUG
#  define Trace(x)  {fprintf x ; fflush(stderr); fflush(stdout);}
#else
#  define Trace(x)  ;
#endif

typedef unsigned char   uch;
typedef unsigned short  ush;
typedef unsigned long   ulg;

typedef struct _rwpng_color_struct {
   png_byte red;
   png_byte green;
   png_byte blue;
} rwpng_color;

typedef struct _mainprog_info {
    double gamma;
    ulg width;			/* read/write */
    ulg height;			/* read/write */
    ulg rowbytes;		/* read */
    void *png_ptr;		/* read/write */
    void *info_ptr;		/* read/write */
    rwpng_color palette[256];	/* write */
    uch trans[256];		/* write */
    uch *rgba_data;		/* read */
    uch *indexed_data;		/* write */
    uch **row_pointers;		/* read/write */
    jmp_buf jmpbuf;		/* read/write */
    int interlaced;		/* read/write */
    int channels;		/* read (currently not used) */
    int sample_depth;		/* write */
    int num_palette;		/* write */
    int num_trans;		/* write */
    int retval;			/* read/write */
} mainprog_info;


/* prototypes for public functions in rwpng.c */

void rwpng_version_info(void);

int rwpng_read_image(FILE *infile, mainprog_info *mainprog_ptr);

int rwpng_write_image_init(FILE *outfile, mainprog_info *mainprog_ptr);

int rwpng_write_image_whole(mainprog_info *mainprog_ptr);

int rwpng_write_image_row(mainprog_info *mainprog_ptr);

int rwpng_write_image_finish(mainprog_info *mainprog_ptr);
