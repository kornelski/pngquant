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

#define PNGQUANT_VERSION LIQ_VERSION_STRING " (May 2016)"

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
  --verbose         print status messages (synonym: -v)\n\
\n\
Quantizes one or more 32-bit RGBA PNGs to 8-bit (or smaller) RGBA-palette.\n\
The output filename is the same as the input name except that\n\
it ends in \"-fs8.png\", \"-or8.png\" or your custom extension (unless the\n\
input is stdin, in which case the quantized image will go to stdout).\n\
The default behavior if the output file exists is to skip the conversion;\n\
use --force to overwrite. See man page for full list of options.\n"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdbool.h>
#include <getopt.h>
#include <unistd.h>
#include <math.h>

extern char *optarg;
extern int optind, opterr;

#if defined(WIN32) || defined(__WIN32__)
#  include <fcntl.h>    /* O_BINARY */
#  include <io.h>   /* setmode() */
#endif

#ifdef _OPENMP
#include <omp.h>
#else
#define omp_get_max_threads() 1
#define omp_get_thread_num() 0
#endif

#include "rwpng.h"  /* typedefs, common macros, public prototypes */
#include "lib/libimagequant.h"

struct pngquant_options {
    liq_attr *liq;
    liq_image *fixed_palette_image;
    liq_log_callback_function *log_callback;
    void *log_callback_user_info;
    float floyd;
    bool using_stdin, using_stdout, force, fast_compression, ie_mode,
        min_quality_limit, skip_if_larger,
        verbose;
};

static pngquant_error prepare_output_image(liq_result *result, liq_image *input_image, rwpng_color_transform tag, png8_image *output_image);
static void set_palette(liq_result *result, png8_image *output_image);
static pngquant_error read_image(liq_attr *options, const char *filename, int using_stdin, png24_image *input_image_p, liq_image **liq_image_p, bool keep_input_pixels, bool verbose);
static pngquant_error write_image(png8_image *output_image, png24_image *output_image24, const char *outname, struct pngquant_options *options);
static char *add_filename_extension(const char *filename, const char *newext);
static bool file_exists(const char *outname);

static void verbose_printf(struct pngquant_options *context, const char *fmt, ...)
{
    if (context->log_callback) {
        va_list va;
        va_start(va, fmt);
        int required_space = vsnprintf(NULL, 0, fmt, va)+1; // +\0
        va_end(va);

        char buf[required_space];
        va_start(va, fmt);
        vsnprintf(buf, required_space, fmt, va);
        va_end(va);

        context->log_callback(context->liq, buf, context->log_callback_user_info);
    }
}

static void log_callback(const liq_attr *attr, const char *msg, void* user_info)
{
    fprintf(stderr, "%s\n", msg);
}

#ifdef _OPENMP
#define LOG_BUFFER_SIZE 1300
struct buffered_log {
    int buf_used;
    char buf[LOG_BUFFER_SIZE];
};

static void log_callback_buferred_flush(const liq_attr *attr, void *context)
{
    struct buffered_log *log = context;
    if (log->buf_used) {
        fwrite(log->buf, 1, log->buf_used, stderr);
        fflush(stderr);
        log->buf_used = 0;
    }
}

static void log_callback_buferred(const liq_attr *attr, const char *msg, void* context)
{
    struct buffered_log *log = context;
    int len = strlen(msg);
    if (len > LOG_BUFFER_SIZE-2) len = LOG_BUFFER_SIZE-2;

    if (len > LOG_BUFFER_SIZE - log->buf_used - 2) log_callback_buferred_flush(attr, log);
    memcpy(&log->buf[log->buf_used], msg, len);
    log->buf_used += len+1;
    log->buf[log->buf_used-1] = '\n';
    log->buf[log->buf_used] = '\0';
}
#endif

static void print_full_version(FILE *fd)
{
    fprintf(fd, "pngquant, %s, by Kornel Lesinski, Greg Roelofs.\n"
        #ifndef NDEBUG
                    "   WARNING: this is a DEBUG (slow) version.\n" /* NDEBUG disables assert() */
        #endif
        #if !USE_SSE && (defined(__SSE__) || defined(__amd64__) || defined(__X86_64__) || defined(__i386__))
                    "   SSE acceleration disabled.\n"
        #endif
        #if _OPENMP
                    "   Compiled with OpenMP (multicore support).\n"
        #endif
        , PNGQUANT_VERSION);
    rwpng_version_info(fd);
    fputs("\n", fd);
}

static void print_usage(FILE *fd)
{
    fputs(PNGQUANT_USAGE, fd);
}

/**
 *   N = automatic quality, uses limit unless force is set (N-N or 0-N)
 *  -N = no better than N (same as 0-N)
 * N-M = no worse than N, no better than M
 * N-  = no worse than N, perfect if possible (same as N-100)
 *
 * where N,M are numbers between 0 (lousy) and 100 (perfect)
 */
static bool parse_quality(const char *quality, liq_attr *options, bool *min_quality_limit)
{
    long limit, target;
    const char *str = quality; char *end;

    long t1 = strtol(str, &end, 10);
    if (str == end) return false;
    str = end;

    if ('\0' == end[0] && t1 < 0) { // quality="-%d"
        target = -t1;
        limit = 0;
    } else if ('\0' == end[0]) { // quality="%d"
        target = t1;
        limit = t1*9/10;
    } else if ('-' == end[0] && '\0' == end[1]) { // quality="%d-"
        target = 100;
        limit = t1;
    } else { // quality="%d-%d"
        long t2 = strtol(str, &end, 10);
        if (str == end || t2 > 0) return false;
        target = -t2;
        limit = t1;
    }

    *min_quality_limit = (limit > 0);
    return LIQ_OK == liq_set_quality(options, limit, target);
}

static const struct {const char *old; const char *newopt;} obsolete_options[] = {
    {"-fs","--floyd=1"},
    {"-nofs", "--ordered"},
    {"-floyd", "--floyd=1"},
    {"-nofloyd", "--ordered"},
    {"-ordered", "--ordered"},
    {"-force", "--force"},
    {"-noforce", "--no-force"},
    {"-verbose", "--verbose"},
    {"-quiet", "--quiet"},
    {"-noverbose", "--quiet"},
    {"-noquiet", "--verbose"},
    {"-help", "--help"},
    {"-version", "--version"},
    {"-ext", "--ext"},
    {"-speed", "--speed"},
};

static void fix_obsolete_options(const unsigned int argc, char *argv[])
{
    for(unsigned int argn=1; argn < argc; argn++) {
        if ('-' != argv[argn][0]) continue;

        if ('-' == argv[argn][1]) break; // stop on first --option or --

        for(unsigned int i=0; i < sizeof(obsolete_options)/sizeof(obsolete_options[0]); i++) {
            if (0 == strcmp(obsolete_options[i].old, argv[argn])) {
                fprintf(stderr, "  warning: option '%s' has been replaced with '%s'.\n", obsolete_options[i].old, obsolete_options[i].newopt);
                argv[argn] = (char*)obsolete_options[i].newopt;
            }
        }
    }
}

enum {arg_floyd=1, arg_ordered, arg_ext, arg_no_force, arg_iebug,
    arg_transbug, arg_map, arg_posterize, arg_skip_larger};

static const struct option long_options[] = {
    {"verbose", no_argument, NULL, 'v'},
    {"quiet", no_argument, NULL, 'q'},
    {"force", no_argument, NULL, 'f'},
    {"no-force", no_argument, NULL, arg_no_force},
    {"floyd", optional_argument, NULL, arg_floyd},
    {"ordered", no_argument, NULL, arg_ordered},
    {"nofs", no_argument, NULL, arg_ordered},
    {"iebug", no_argument, NULL, arg_iebug},
    {"transbug", no_argument, NULL, arg_transbug},
    {"ext", required_argument, NULL, arg_ext},
    {"skip-if-larger", no_argument, NULL, arg_skip_larger},
    {"output", required_argument, NULL, 'o'},
    {"speed", required_argument, NULL, 's'},
    {"quality", required_argument, NULL, 'Q'},
    {"posterize", required_argument, NULL, arg_posterize},
    {"map", required_argument, NULL, arg_map},
    {"version", no_argument, NULL, 'V'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0},
};

pngquant_error pngquant_file(const char *filename, const char *outname, struct pngquant_options *options);


int main(int argc, char *argv[])
{
    struct pngquant_options options = {
        .floyd = 1.f, // floyd-steinberg dithering
    };
    options.liq = liq_attr_create();

    if (!options.liq) {
        fputs("SSE-capable CPU is required for this build.\n", stderr);
        return WRONG_ARCHITECTURE;
    }

    unsigned int error_count=0, skipped_count=0, file_count=0;
    pngquant_error latest_error=SUCCESS;
    const char *newext = NULL, *output_file_path = NULL;

    fix_obsolete_options(argc, argv);

    int opt;
    do {
        opt = getopt_long(argc, argv, "Vvqfhs:Q:o:", long_options, NULL);
        switch (opt) {
            case 'v':
                options.verbose = true;
                break;
            case 'q':
                options.verbose = false;
                break;

            case arg_floyd:
                options.floyd = optarg ? atof(optarg) : 1.f;
                if (options.floyd < 0 || options.floyd > 1.f) {
                    fputs("--floyd argument must be in 0..1 range\n", stderr);
                    return INVALID_ARGUMENT;
                }
                break;
            case arg_ordered: options.floyd = 0; break;

            case 'f': options.force = true; break;
            case arg_no_force: options.force = false; break;

            case arg_ext: newext = optarg; break;
            case 'o':
                if (output_file_path) {
                    fputs("--output option can be used only once\n", stderr);
                    return INVALID_ARGUMENT;
                }
                output_file_path = optarg; break;

            case arg_iebug:
                // opacities above 238 will be rounded up to 255, because IE6 truncates <255 to 0.
                liq_set_min_opacity(options.liq, 238);
                fputs("  warning: the workaround for IE6 is deprecated\n", stderr);
                break;

            case arg_transbug:
                liq_set_last_index_transparent(options.liq, true);
                break;

            case arg_skip_larger:
                options.skip_if_larger = true;
                break;

            case 's':
                {
                    int speed = atoi(optarg);
                    if (speed >= 10) {
                        options.fast_compression = true;
                    }
                    if (speed == 11) {
                        options.floyd = 0;
                        speed = 10;
                    }
                    if (LIQ_OK != liq_set_speed(options.liq, speed)) {
                        fputs("Speed should be between 1 (slow) and 11 (fast).\n", stderr);
                        return INVALID_ARGUMENT;
                    }
                }
                break;

            case 'Q':
                if (!parse_quality(optarg, options.liq, &options.min_quality_limit)) {
                    fputs("Quality should be in format min-max where min and max are numbers in range 0-100.\n", stderr);
                    return INVALID_ARGUMENT;
                }
                break;

            case arg_posterize:
                if (LIQ_OK != liq_set_min_posterization(options.liq, atoi(optarg))) {
                    fputs("Posterization should be number of bits in range 0-4.\n", stderr);
                    return INVALID_ARGUMENT;
                }
                break;

            case arg_map:
                {
                    png24_image tmp = {};
                    if (SUCCESS != read_image(options.liq, optarg, false, &tmp, &options.fixed_palette_image, true, false)) {
                        fprintf(stderr, "  error: unable to load %s", optarg);
                        return INVALID_ARGUMENT;
                    }
                    liq_result *tmp_quantize = liq_quantize_image(options.liq, options.fixed_palette_image);
                    const liq_palette *pal = liq_get_palette(tmp_quantize);
                    if (!pal) {
                        fprintf(stderr, "  error: unable to read colors from %s", optarg);
                        return INVALID_ARGUMENT;
                    }
                    for(int i=0; i < pal->count; i++) {
                        liq_image_add_fixed_color(options.fixed_palette_image, pal->entries[i]);
                    }
                    liq_result_destroy(tmp_quantize);
                }
                break;

            case 'h':
                print_full_version(stdout);
                print_usage(stdout);
                return SUCCESS;

            case 'V':
                puts(PNGQUANT_VERSION);
                return SUCCESS;

            case -1: break;

            default:
                return INVALID_ARGUMENT;
        }
    } while (opt != -1);

    int argn = optind;

    if (argn >= argc) {
        if (argn > 1) {
            fputs("No input files specified.\n", stderr);
        } else {
            print_full_version(stderr);
        }
        print_usage(stderr);
        return MISSING_ARGUMENT;
    }

    if (options.verbose) {
        liq_set_log_callback(options.liq, log_callback, NULL);
        options.log_callback = log_callback;
    }

    char *colors_end;
    unsigned long colors = strtoul(argv[argn], &colors_end, 10);
    if (colors_end != argv[argn] && '\0' == colors_end[0]) {
        if (LIQ_OK != liq_set_max_colors(options.liq, colors)) {
            fputs("Number of colors must be between 2 and 256.\n", stderr);
            return INVALID_ARGUMENT;
        }
        argn++;
    }

    if (newext && output_file_path) {
        fputs("--ext and --output options can't be used at the same time\n", stderr);
        return INVALID_ARGUMENT;
    }

    // new filename extension depends on options used. Typically basename-fs8.png
    if (newext == NULL) {
        newext = options.floyd > 0 ? "-ie-fs8.png" : "-ie-or8.png";
        if (!options.ie_mode) {
            newext += 3;    /* skip "-ie" */
        }
    }

    if (argn == argc || (argn == argc-1 && 0==strcmp(argv[argn],"-"))) {
        options.using_stdin = true;
        options.using_stdout = !output_file_path;
        argn = argc-1;
    }

    const int num_files = argc-argn;

    if (output_file_path && num_files != 1) {
        fputs("Only one input file is allowed when --output is used\n", stderr);
        return INVALID_ARGUMENT;
    }

#ifdef _OPENMP
    // if there's a lot of files, coarse parallelism can be used
    if (num_files > 2*omp_get_max_threads()) {
        omp_set_nested(0);
        omp_set_dynamic(1);
    } else {
        omp_set_nested(1);
    }
#endif

    #pragma omp parallel for \
        schedule(static, 1) reduction(+:skipped_count) reduction(+:error_count) reduction(+:file_count) shared(latest_error)
    for(int i=0; i < num_files; i++) {
        struct pngquant_options opts = options;
        opts.liq = liq_attr_copy(options.liq);

        const char *filename = opts.using_stdin ? "stdin" : argv[argn+i];

        #ifdef _OPENMP
        struct buffered_log buf = {};
        if (opts.log_callback && omp_get_num_threads() > 1 && num_files > 1) {
            liq_set_log_callback(opts.liq, log_callback_buferred, &buf);
            liq_set_log_flush_callback(opts.liq, log_callback_buferred_flush, &buf);
            opts.log_callback = log_callback_buferred;
            opts.log_callback_user_info = &buf;
        }
        #endif


        pngquant_error retval = SUCCESS;

        const char *outname = output_file_path;
        char *outname_free = NULL;
        if (!opts.using_stdout) {
            if (!outname) {
                outname = outname_free = add_filename_extension(filename, newext);
            }
            if (!opts.force && file_exists(outname)) {
                fprintf(stderr, "  error: '%s' exists; not overwriting\n", outname);
                retval = NOT_OVERWRITING_ERROR;
            }
        }

        if (SUCCESS == retval) {
            retval = pngquant_file(filename, outname, &opts);
        }

        free(outname_free);

        liq_attr_destroy(opts.liq);

        if (retval) {
            #pragma omp critical
            {
                latest_error = retval;
            }
            if (retval == TOO_LOW_QUALITY || retval == TOO_LARGE_FILE) {
                skipped_count++;
            } else {
                error_count++;
            }
        }
        ++file_count;
    }

    if (error_count) {
        verbose_printf(&options, "There were errors quantizing %d file%s out of a total of %d file%s.",
                       error_count, (error_count == 1)? "" : "s", file_count, (file_count == 1)? "" : "s");
    }
    if (skipped_count) {
        verbose_printf(&options, "Skipped %d file%s out of a total of %d file%s.",
                       skipped_count, (skipped_count == 1)? "" : "s", file_count, (file_count == 1)? "" : "s");
    }
    if (!skipped_count && !error_count) {
        verbose_printf(&options, "No errors detected while quantizing %d image%s.",
                       file_count, (file_count == 1)? "" : "s");
    }

    liq_image_destroy(options.fixed_palette_image);
    liq_attr_destroy(options.liq);

    return latest_error;
}

pngquant_error pngquant_file(const char *filename, const char *outname, struct pngquant_options *options)
{
    pngquant_error retval = SUCCESS;

    verbose_printf(options, "%s:", filename);

    liq_image *input_image = NULL;
    png24_image input_image_rwpng = {};
    bool keep_input_pixels = options->skip_if_larger || (options->using_stdout && options->min_quality_limit); // original may need to be output to stdout
    if (SUCCESS == retval) {
        retval = read_image(options->liq, filename, options->using_stdin, &input_image_rwpng, &input_image, keep_input_pixels, options->verbose);
    }

    int quality_percent = 90; // quality on 0-100 scale, updated upon successful remap
    png8_image output_image = {};
    if (SUCCESS == retval) {
        verbose_printf(options, "  read %luKB file", (input_image_rwpng.file_size+1023UL)/1024UL);

        if (RWPNG_ICCP == input_image_rwpng.input_color) {
            verbose_printf(options, "  used embedded ICC profile to transform image to sRGB colorspace");
        } else if (RWPNG_GAMA_CHRM == input_image_rwpng.input_color) {
            verbose_printf(options, "  used gAMA and cHRM chunks to transform image to sRGB colorspace");
        } else if (RWPNG_ICCP_WARN_GRAY == input_image_rwpng.input_color) {
            verbose_printf(options, "  warning: ignored ICC profile in GRAY colorspace");
        } else if (RWPNG_COCOA == input_image_rwpng.input_color) {
            // No comment
        } else if (RWPNG_SRGB == input_image_rwpng.input_color) {
            verbose_printf(options, "  passing sRGB tag from the input");
        } else if (input_image_rwpng.gamma != 0.45455) {
            verbose_printf(options, "  converted image from gamma %2.1f to gamma 2.2",
                           1.0/input_image_rwpng.gamma);
        }

        // when using image as source of a fixed palette the palette is extracted using regular quantization
        liq_result *remap = liq_quantize_image(options->liq, options->fixed_palette_image ? options->fixed_palette_image : input_image);

        if (remap) {

            // fixed gamma ~2.2 for the web. PNG can't store exact 1/2.2
            // NB: can't change gamma here, because output_color is allowed to be an sRGB tag
            liq_set_output_gamma(remap, 0.45455);
            liq_set_dithering_level(remap, options->floyd);

            retval = prepare_output_image(remap, input_image, input_image_rwpng.output_color, &output_image);
            if (SUCCESS == retval) {
                if (LIQ_OK != liq_write_remapped_image_rows(remap, input_image, output_image.row_pointers)) {
                    retval = OUT_OF_MEMORY_ERROR;
                }

                set_palette(remap, &output_image);

                double palette_error = liq_get_quantization_error(remap);
                if (palette_error >= 0) {
                    quality_percent = liq_get_quantization_quality(remap);
                    verbose_printf(options, "  mapped image to new colors...MSE=%.3f (Q=%d)", palette_error, quality_percent);
                }
            }
            liq_result_destroy(remap);
        } else {
            retval = TOO_LOW_QUALITY;
        }
    }

    if (SUCCESS == retval) {

        if (options->skip_if_larger) {
            // this is very rough approximation, but generally avoid losing more quality than is gained in file size.
            // Quality is raised to 1.5, because even greater savings are needed to justify big quality loss.
            // but >50% savings are considered always worthwile in order to allow low quality conversions to work at all
            const double quality = quality_percent/100.0;
            const double expected_reduced_size = pow(quality, 1.5);
            output_image.maximum_file_size = (input_image_rwpng.file_size-1) * (expected_reduced_size < 0.5 ? 0.5 : expected_reduced_size);
        }

        output_image.fast_compression = options->fast_compression;
        output_image.chunks = input_image_rwpng.chunks; input_image_rwpng.chunks = NULL;
        retval = write_image(&output_image, NULL, outname, options);

        if (TOO_LARGE_FILE == retval) {
            verbose_printf(options, "  file exceeded expected size of %luKB", (unsigned long)output_image.maximum_file_size/1024UL);
        }
    }

    if (options->using_stdout && keep_input_pixels && (TOO_LARGE_FILE == retval || TOO_LOW_QUALITY == retval)) {
        // when outputting to stdout it'd be nasty to create 0-byte file
        // so if quality is too low, output 24-bit original
        pngquant_error write_retval = write_image(NULL, &input_image_rwpng, outname, options);
        if (write_retval) {
            retval = write_retval;
        }
    }

    if (input_image) liq_image_destroy(input_image);
    rwpng_free_image24(&input_image_rwpng);
    rwpng_free_image8(&output_image);

    return retval;
}

static void set_palette(liq_result *result, png8_image *output_image)
{
    const liq_palette *palette = liq_get_palette(result);

    output_image->num_palette = palette->count;
    for(unsigned int i=0; i < palette->count; i++) {
        liq_color px = palette->entries[i];
        output_image->palette[i] = (rwpng_rgba){.r=px.r, .g=px.g, .b=px.b, .a=px.a};
    }
}


static bool file_exists(const char *outname)
{
    FILE *outfile = fopen(outname, "rb");
    if ((outfile ) != NULL) {
        fclose(outfile);
        return true;
    }
    return false;
}

/* build the output filename from the input name by inserting "-fs8" or
 * "-or8" before the ".png" extension (or by appending that plus ".png" if
 * there isn't any extension), then make sure it doesn't exist already */
static char *add_filename_extension(const char *filename, const char *newext)
{
    size_t x = strlen(filename);

    char* outname = malloc(x+4+strlen(newext)+1);
    if (!outname) return NULL;

    strncpy(outname, filename, x);
    if (strncmp(outname+x-4, ".png", 4) == 0 || strncmp(outname+x-4, ".PNG", 4) == 0) {
        strcpy(outname+x-4, newext);
    } else {
        strcpy(outname+x, newext);
    }

    return outname;
}

static char *temp_filename(const char *basename) {
    size_t x = strlen(basename);

    char *outname = malloc(x+1+4);
    if (!outname) return NULL;

    strcpy(outname, basename);
    strcpy(outname+x, ".tmp");

    return outname;
}

static void set_binary_mode(FILE *fp)
{
#if defined(WIN32) || defined(__WIN32__)
    setmode(fp == stdout ? 1 : 0, O_BINARY);
#endif
}

static const char *filename_part(const char *path)
{
    const char *outfilename = strrchr(path, '/');
    if (outfilename) {
        return outfilename+1;
    } else {
        return path;
    }
}

static bool replace_file(const char *from, const char *to, const bool force) {
#if defined(WIN32) || defined(__WIN32__)
    if (force) {
        // On Windows rename doesn't replace
        unlink(to);
    }
#endif
    return (0 == rename(from, to));
}

static pngquant_error write_image(png8_image *output_image, png24_image *output_image24, const char *outname, struct pngquant_options *options)
{
    FILE *outfile;
    char *tempname = NULL;

    if (options->using_stdout) {
        set_binary_mode(stdout);
        outfile = stdout;

        if (output_image) {
            verbose_printf(options, "  writing %d-color image to stdout", output_image->num_palette);
        } else {
            verbose_printf(options, "  writing truecolor image to stdout");
        }
    } else {
        tempname = temp_filename(outname);
        if (!tempname) return OUT_OF_MEMORY_ERROR;

        if ((outfile = fopen(tempname, "wb")) == NULL) {
            fprintf(stderr, "  error: cannot open '%s' for writing\n", tempname);
            free(tempname);
            return CANT_WRITE_ERROR;
        }

        if (output_image) {
            verbose_printf(options, "  writing %d-color image as %s", output_image->num_palette, filename_part(outname));
        } else {
            verbose_printf(options, "  writing truecolor image as %s", filename_part(outname));
        }
    }

    pngquant_error retval;
    #pragma omp critical (libpng)
    {
        if (output_image) {
            retval = rwpng_write_image8(outfile, output_image);
        } else {
            retval = rwpng_write_image24(outfile, output_image24);
        }
    }

    if (!options->using_stdout) {
        fclose(outfile);

        if (SUCCESS == retval) {
            // Image has been written to a temporary file and then moved over destination.
            // This makes replacement atomic and avoids damaging destination file on write error.
            if (!replace_file(tempname, outname, options->force)) {
                retval = CANT_WRITE_ERROR;
            }
        }

        if (retval) {
            unlink(tempname);
        }
    }
    free(tempname);

    if (retval && retval != TOO_LARGE_FILE) {
        fprintf(stderr, "  error: failed writing image to %s\n", outname);
    }

    return retval;
}

static pngquant_error read_image(liq_attr *options, const char *filename, int using_stdin, png24_image *input_image_p, liq_image **liq_image_p, bool keep_input_pixels, bool verbose)
{
    FILE *infile;

    if (using_stdin) {
        set_binary_mode(stdin);
        infile = stdin;
    } else if ((infile = fopen(filename, "rb")) == NULL) {
        fprintf(stderr, "  error: cannot open %s for reading\n", filename);
        return READ_ERROR;
    }

    pngquant_error retval;
    #pragma omp critical (libpng)
    {
        retval = rwpng_read_image24(infile, input_image_p, verbose);
    }

    if (!using_stdin) {
        fclose(infile);
    }

    if (retval) {
        fprintf(stderr, "  error: cannot decode image %s\n", using_stdin ? "from stdin" : filename_part(filename));
        return retval;
    }

    *liq_image_p = liq_image_create_rgba_rows(options, (void**)input_image_p->row_pointers, input_image_p->width, input_image_p->height, input_image_p->gamma);

    if (!*liq_image_p) {
        return OUT_OF_MEMORY_ERROR;
    }

    if (!keep_input_pixels) {
        if (LIQ_OK != liq_image_set_memory_ownership(*liq_image_p, LIQ_OWN_ROWS | LIQ_OWN_PIXELS)) {
            return OUT_OF_MEMORY_ERROR;
        }
        input_image_p->row_pointers = NULL;
        input_image_p->rgba_data = NULL;
    }

    return SUCCESS;
}

static pngquant_error prepare_output_image(liq_result *result, liq_image *input_image, rwpng_color_transform output_color, png8_image *output_image)
{
    output_image->width = liq_image_get_width(input_image);
    output_image->height = liq_image_get_height(input_image);
    output_image->gamma = liq_get_output_gamma(result);
    output_image->output_color = output_color;

    /*
    ** Step 3.7 [GRR]: allocate memory for the entire indexed image
    */

    output_image->indexed_data = malloc(output_image->height * output_image->width);
    output_image->row_pointers = malloc(output_image->height * sizeof(output_image->row_pointers[0]));

    if (!output_image->indexed_data || !output_image->row_pointers) {
        return OUT_OF_MEMORY_ERROR;
    }

    for(size_t row = 0; row < output_image->height; row++) {
        output_image->row_pointers[row] = output_image->indexed_data + row * output_image->width;
    }

    const liq_palette *palette = liq_get_palette(result);
    // tRNS, etc.
    output_image->num_palette = palette->count;

    return SUCCESS;
}
