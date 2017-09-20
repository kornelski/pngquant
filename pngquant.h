/* pngquant.c - quantize the colors in an alphamap down to a specified number
**
** Copyright (C) 1989, 1991 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
**
** - - - -
**
** © 1997-2002 by Greg Roelofs; based on an idea by Stefan Schneider.
** © 2009-2015 by Kornel Lesiński.
**
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without modification,
** are permitted provided that the following conditions are met:
**
** 1. Redistributions of source code must retain the above copyright notice,
**    this list of conditions and the following disclaimer.
**
** 2. Redistributions in binary form must reproduce the above copyright notice,
**    this list of conditions and the following disclaimer in the documentation
**    and/or other materials provided with the distribution.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
** OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**
*/

#define PNGQUANT_VERSION LIQ_VERSION_STRING " (August 2017)"

#define PNGQUANT_USAGE "\
usage:  pngquant [options] [ncolors] -- pngfile [pngfile ...]\n\
        pngquant [options] [ncolors] - >stdout <stdin\n\n\
options:\n\
  --force           overwrite existing output files (synonym: -f)\n\
  --skip-if-larger  only save converted files if they're smaller than original\n\
  --output file     destination file path to use instead of --ext (synonym: -o)\n\
  --ext new.png     set custom suffix/extension for output filenames\n\
  --quality min-max don't save below min, use fewer colors below max (0-100)\n\
  --speed N         speed/quality trade-off. 1=slow, 3=default, 11=fast & rough\n\
  --nofs            disable Floyd-Steinberg dithering\n\
  --posterize N     output lower-precision color (e.g. for ARGB4444 output)\n\
  --strip           remove optional metadata (default on Mac)\n\
  --verbose         print status messages (synonym: -v)\n\
\n\
Quantizes one or more 32-bit RGBA PNGs to 8-bit (or smaller) RGBA-palette.\n\
The output filename is the same as the input name except that\n\
it ends in \"-fs8.png\", \"-or8.png\" or your custom extension (unless the\n\
input is stdin, in which case the quantized image will go to stdout).\n\
If you pass the special output path \"-\" and a single input file, that file\n\
will be processed and the quantized image will go to stdout.\n\
The default behavior if the output file exists is to skip the conversion;\n\
use --force to overwrite. See man page for full list of options.\n"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <math.h>

#if defined(_WIN32) || defined(WIN32) || defined(__WIN32__)
#  include <fcntl.h>    /* O_BINARY */
#  include <io.h>   /* setmode() */
#else
#  include <unistd.h>
#endif

#ifdef _OPENMP
#include <omp.h>
#else
#define omp_get_max_threads() 1
#define omp_get_thread_num() 0
#endif

#include "rwpng.h"  /* typedefs, common macros, public prototypes */
#include "libimagequant.h" /* if it fails here, run: git submodule update; ./configure; or add -Ilib to compiler flags */
#include "pngquant_opts.h"

static pngquant_error prepare_output_image(liq_result *result, liq_image *input_image, rwpng_color_transform tag, png8_image *output_image);
static void set_palette(liq_result *result, png8_image *output_image);
static pngquant_error read_image(liq_attr *options, const char *filename, int using_stdin, png24_image *input_image_p, liq_image **liq_image_p, bool keep_input_pixels, bool strip, bool verbose);
static pngquant_error write_image(png8_image *output_image, png24_image *output_image24, const char *outname, struct pngquant_options *options);
static char *add_filename_extension(const char *filename, const char *newext);
static bool file_exists(const char *outname);
pngquant_error pngquant_main(struct pngquant_options *options);
pngquant_error pngquant_file(const char *filename, const char *outname, struct pngquant_options *options);
