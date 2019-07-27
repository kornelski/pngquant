/* pngquant.c - quantize the colors in an alphamap down to a specified number
**
** © 2009-2019 by Kornel Lesiński.
** © 1989, 1991 by Jef Poskanzer.
** © 1997-2002 by Greg Roelofs; based on an idea by Stefan Schneider.
**
** See COPYRIGHT file for license.
*/

char *PNGQUANT_USAGE = "\
usage:  pngquant [options] [ncolors] -- pngfile [pngfile ...]\n\
        pngquant [options] [ncolors] - >stdout <stdin\n\n\
options:\n\
  --force           overwrite existing output files (synonym: -f)\n\
  --skip-if-larger  only save converted files if they're smaller than original\n\
  --output file     destination file path to use instead of --ext (synonym: -o)\n\
  --ext new.png     set custom suffix/extension for output filenames\n\
  --quality min-max don't save below min, use fewer colors below max (0-100)\n\
  --speed N         speed/quality trade-off. 1=slow, 4=default, 11=fast & rough\n\
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
use --force to overwrite. See man page for full list of options.\n";


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

char *PNGQUANT_VERSION = LIQ_VERSION_STRING " (July 2019)";

static pngquant_error prepare_output_image(liq_result *result, liq_image *input_image, rwpng_color_transform tag, png8_image *output_image);
static void set_palette(liq_result *result, png8_image *output_image);
static pngquant_error read_image(liq_attr *options, const char *filename, int using_stdin, png24_image *input_image_p, liq_image **liq_image_p, bool keep_input_pixels, bool strip, bool verbose);
static pngquant_error write_image(png8_image *output_image, png24_image *output_image24, const char *outname, struct pngquant_options *options, liq_attr *liq);
static char *add_filename_extension(const char *filename, const char *newext);
static bool file_exists(const char *outname);

static void verbose_printf(liq_attr *liq, struct pngquant_options *context, const char *fmt, ...)
{
    if (context->log_callback) {
        va_list va;
        va_start(va, fmt);
        int required_space = vsnprintf(NULL, 0, fmt, va)+1; // +\0
        va_end(va);

#if defined(_MSC_VER)
        char *buf = malloc(required_space);
#else
        char buf[required_space];
#endif
        va_start(va, fmt);
        vsnprintf(buf, required_space, fmt, va);
        va_end(va);

        context->log_callback(liq, buf, context->log_callback_user_info);
#if defined(_MSC_VER)
        free(buf);
#endif
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

void pngquant_internal_print_config(FILE *fd) {
    fprintf(fd,
        ""
        #ifndef NDEBUG
                    "   WARNING: this is a DEBUG (slow) version.\n" /* NDEBUG disables assert() */
        #endif
        #if !USE_SSE && (defined(__SSE__) || defined(__amd64__) || defined(__X86_64__) || defined(__i386__))
                    "   SSE acceleration disabled.\n"
        #endif
        #if _OPENMP
                    "   Compiled with OpenMP (multicore support).\n"
        #endif
    );
    fflush(fd);
}

FILE *pngquant_c_stderr() {
    return stderr;
}
FILE *pngquant_c_stdout() {
    return stdout;
}

static void print_full_version(FILE *fd)
{
    fprintf(fd, "pngquant, %s, by Kornel Lesinski, Greg Roelofs.\n", PNGQUANT_VERSION);
    pngquant_internal_print_config(fd);
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

pngquant_error pngquant_main_internal(struct pngquant_options *options, liq_attr *liq);
static pngquant_error pngquant_file_internal(const char *filename, const char *outname, struct pngquant_options *options, liq_attr *liq);

#ifndef PNGQUANT_NO_MAIN
int main(int argc, char *argv[])
{
    struct pngquant_options options = {
        .floyd = 1.f, // floyd-steinberg dithering
        .strip = false,
    };

    pngquant_error retval = pngquant_parse_options(argc, argv, &options);
    if (retval != SUCCESS) {
        return retval;
    }

    if (options.print_version) {
        puts(PNGQUANT_VERSION);
        return SUCCESS;
    }

    if (options.missing_arguments) {
        print_full_version(stderr);
        print_usage(stderr);
        return MISSING_ARGUMENT;
    }

    if (options.print_help) {
        print_full_version(stdout);
        print_usage(stdout);
        return SUCCESS;
    }

    liq_attr *liq = liq_attr_create();

    if (!liq) {
        fputs("SSE-capable CPU is required for this build.\n", stderr);
        return WRONG_ARCHITECTURE;
    }

    if (options.quality && !parse_quality(options.quality, liq, &options.min_quality_limit)) {
        fputs("Quality should be in format min-max where min and max are numbers in range 0-100.\n", stderr);
        return INVALID_ARGUMENT;
    }

    if (options.iebug) {
        // opacities above 238 will be rounded up to 255, because IE6 truncates <255 to 0.
        liq_set_min_opacity(liq, 238);
        fputs("  warning: the workaround for IE6 is deprecated\n", stderr);
    }

    if (options.verbose) {
        liq_set_log_callback(liq, log_callback, NULL);
        options.log_callback = log_callback;
    }

    if (options.last_index_transparent) {
        liq_set_last_index_transparent(liq, true);
    }

    if (options.speed >= 10) {
        options.fast_compression = true;
        if (options.speed == 11) {
            options.floyd = 0;
            options.speed = 10;
        }
    }

    if (options.speed && LIQ_OK != liq_set_speed(liq, options.speed)) {
        fputs("Speed should be between 1 (slow) and 11 (fast).\n", stderr);
        return INVALID_ARGUMENT;
    }

    if (options.colors && LIQ_OK != liq_set_max_colors(liq, options.colors)) {
        fputs("Number of colors must be between 2 and 256.\n", stderr);
        return INVALID_ARGUMENT;
    }

    if (options.posterize && LIQ_OK != liq_set_min_posterization(liq, options.posterize)) {
        fputs("Posterization should be number of bits in range 0-4.\n", stderr);
        return INVALID_ARGUMENT;
    }

    if (options.extension && options.output_file_path) {
        fputs("--ext and --output options can't be used at the same time\n", stderr);
        return INVALID_ARGUMENT;
    }

    // new filename extension depends on options used. Typically basename-fs8.png
    if (options.extension == NULL) {
        options.extension = options.floyd > 0 ? "-fs8.png" : "-or8.png";
    }

    if (options.output_file_path && options.num_files != 1) {
        fputs("  error: Only one input file is allowed when --output is used. This error also happens when filenames with spaces are not in quotes.\n", stderr);
        return INVALID_ARGUMENT;
    }

    if (options.using_stdout && !options.using_stdin && options.num_files != 1) {
        fputs("  error: Only one input file is allowed when using the special output path \"-\" to write to stdout. This error also happens when filenames with spaces are not in quotes.\n", stderr);
        return INVALID_ARGUMENT;
    }

    if (!options.num_files && !options.using_stdin) {
        fputs("No input files specified.\n", stderr);
        if (options.verbose) {
            print_full_version(stderr);
        }
        print_usage(stderr);
        return MISSING_ARGUMENT;
    }

    retval = pngquant_main_internal(&options, liq);
    liq_attr_destroy(liq);
    return retval;
}
#endif

// Don't use this. This is not a public API.
pngquant_error pngquant_main_internal(struct pngquant_options *options, liq_attr *liq)
{
    if (options->map_file) {
        png24_image tmp = {.width=0};
        if (SUCCESS != read_image(liq, options->map_file, false, &tmp, &options->fixed_palette_image, true, true, false)) {
            fprintf(stderr, "  error: unable to load %s", options->map_file);
            return INVALID_ARGUMENT;
        }
        liq_result *tmp_quantize = liq_quantize_image(liq, options->fixed_palette_image);
        const liq_palette *pal = liq_get_palette(tmp_quantize);
        if (!pal) {
            fprintf(stderr, "  error: unable to read colors from %s", options->map_file);
            return INVALID_ARGUMENT;
        }
        for(unsigned int i=0; i < pal->count; i++) {
            liq_image_add_fixed_color(options->fixed_palette_image, pal->entries[i]);
        }
        liq_result_destroy(tmp_quantize);
    }

#ifdef _OPENMP
    // if there's a lot of files, coarse parallelism can be used
    if (options->num_files > 2*omp_get_max_threads()) {
        omp_set_nested(0);
        omp_set_dynamic(1);
    } else {
        omp_set_nested(1);
    }
#endif

    unsigned int error_count=0, skipped_count=0, file_count=0;
    pngquant_error latest_error=SUCCESS;

    #pragma omp parallel for \
        schedule(static, 1) reduction(+:skipped_count) reduction(+:error_count) reduction(+:file_count) shared(latest_error)
    for(int i=0; i < options->num_files; i++) {
        const char *filename = options->using_stdin ? "stdin" : options->files[i];
        struct pngquant_options opts = *options;
        liq_attr *local_liq = liq_attr_copy(liq);


        #ifdef _OPENMP
        struct buffered_log buf = {0};
        if (opts.log_callback && omp_get_num_threads() > 1 && opts.num_files > 1) {
            liq_set_log_callback(local_liq, log_callback_buferred, &buf);
            liq_set_log_flush_callback(local_liq, log_callback_buferred_flush, &buf);
            opts.log_callback = log_callback_buferred;
            opts.log_callback_user_info = &buf;
        }
        #endif


        pngquant_error retval = SUCCESS;

        const char *outname = opts.output_file_path;
        char *outname_free = NULL;
        if (!opts.using_stdout) {
            if (!outname) {
                outname = outname_free = add_filename_extension(filename, opts.extension);
            }
            if (!opts.force && file_exists(outname)) {
                fprintf(stderr, "  error: '%s' exists; not overwriting\n", outname);
                retval = NOT_OVERWRITING_ERROR;
            }
        }

        if (SUCCESS == retval) {
            retval = pngquant_file_internal(filename, outname, &opts, local_liq);
        }

        free(outname_free);

        liq_attr_destroy(local_liq);

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
        verbose_printf(liq, options, "There were errors quantizing %d file%s out of a total of %d file%s.",
                       error_count, (error_count == 1)? "" : "s", file_count, (file_count == 1)? "" : "s");
    }
    if (skipped_count) {
        verbose_printf(liq, options, "Skipped %d file%s out of a total of %d file%s.",
                       skipped_count, (skipped_count == 1)? "" : "s", file_count, (file_count == 1)? "" : "s");
    }
    if (!skipped_count && !error_count) {
        verbose_printf(liq, options, "Quantized %d image%s.",
                       file_count, (file_count == 1)? "" : "s");
    }

    if (options->fixed_palette_image) liq_image_destroy(options->fixed_palette_image);

    return latest_error;
}

/// Don't hack this. Instead use https://github.com/ImageOptim/libimagequant/blob/f54d2f1a3e1cf728e17326f4db0d45811c63f063/example.c
static pngquant_error pngquant_file_internal(const char *filename, const char *outname, struct pngquant_options *options, liq_attr *liq)
{
    pngquant_error retval = SUCCESS;

    verbose_printf(liq, options, "%s:", filename);

    liq_image *input_image = NULL;
    png24_image input_image_rwpng = {.width=0};
    bool keep_input_pixels = options->skip_if_larger || (options->using_stdout && options->min_quality_limit); // original may need to be output to stdout
    if (SUCCESS == retval) {
        retval = read_image(liq, filename, options->using_stdin, &input_image_rwpng, &input_image, keep_input_pixels, options->strip, options->verbose);
    }

    int quality_percent = 90; // quality on 0-100 scale, updated upon successful remap
    png8_image output_image = {.width=0};
    if (SUCCESS == retval) {
        verbose_printf(liq, options, "  read %luKB file", (input_image_rwpng.file_size+1023UL)/1024UL);

        if (RWPNG_ICCP == input_image_rwpng.input_color) {
            verbose_printf(liq, options, "  used embedded ICC profile to transform image to sRGB colorspace");
        } else if (RWPNG_GAMA_CHRM == input_image_rwpng.input_color) {
            verbose_printf(liq, options, "  used gAMA and cHRM chunks to transform image to sRGB colorspace");
        } else if (RWPNG_ICCP_WARN_GRAY == input_image_rwpng.input_color) {
            verbose_printf(liq, options, "  warning: ignored ICC profile in GRAY colorspace");
        } else if (RWPNG_COCOA == input_image_rwpng.input_color) {
            // No comment
        } else if (RWPNG_SRGB == input_image_rwpng.input_color) {
            verbose_printf(liq, options, "  passing sRGB tag from the input");
        } else if (input_image_rwpng.gamma != 0.45455) {
            verbose_printf(liq, options, "  converted image from gamma %2.1f to gamma 2.2",
                           1.0/input_image_rwpng.gamma);
        }

        // when using image as source of a fixed palette the palette is extracted using regular quantization
        liq_result *remap;
        liq_error remap_error = liq_image_quantize(options->fixed_palette_image ? options->fixed_palette_image : input_image, liq, &remap);

        if (LIQ_OK == remap_error) {

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
                    verbose_printf(liq, options, "  mapped image to new colors...MSE=%.3f (Q=%d)", palette_error, quality_percent);
                }
            }
            liq_result_destroy(remap);
        } else if (LIQ_QUALITY_TOO_LOW == remap_error) {
            retval = TOO_LOW_QUALITY;
        } else {
            retval = INVALID_ARGUMENT; // dunno
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
        retval = write_image(&output_image, NULL, outname, options, liq);

        if (TOO_LARGE_FILE == retval) {
            verbose_printf(liq, options, "  file exceeded expected size of %luKB", (unsigned long)output_image.maximum_file_size/1024UL);
        }
        if (SUCCESS == retval && output_image.metadata_size > 0) {
            verbose_printf(liq, options, "  copied %dKB of additional PNG metadata", (int)(output_image.metadata_size+999)/1000);
        }
    }

    if (options->using_stdout && keep_input_pixels && (TOO_LARGE_FILE == retval || TOO_LOW_QUALITY == retval)) {
        // when outputting to stdout it'd be nasty to create 0-byte file
        // so if quality is too low, output 24-bit original
        pngquant_error write_retval = write_image(NULL, &input_image_rwpng, outname, options, liq);
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
        const liq_color px = palette->entries[i];
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

    strcpy(outname, filename);
    if (x > 4 && (strncmp(outname+x-4, ".png", 4) == 0 || strncmp(outname+x-4, ".PNG", 4) == 0)) {
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
#if defined(_WIN32) || defined(WIN32) || defined(__WIN32__)
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
#if defined(_WIN32) || defined(WIN32) || defined(__WIN32__)
    if (force) {
        // On Windows rename doesn't replace
        unlink(to);
    }
#endif
    return (0 == rename(from, to));
}

static pngquant_error write_image(png8_image *output_image, png24_image *output_image24, const char *outname, struct pngquant_options *options, liq_attr *liq)
{
    FILE *outfile;
    char *tempname = NULL;

    if (options->using_stdout) {
        set_binary_mode(stdout);
        outfile = stdout;

        if (output_image) {
            verbose_printf(liq, options, "  writing %d-color image to stdout", output_image->num_palette);
        } else {
            verbose_printf(liq, options, "  writing truecolor image to stdout");
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
            verbose_printf(liq, options, "  writing %d-color image as %s", output_image->num_palette, filename_part(outname));
        } else {
            verbose_printf(liq, options, "  writing truecolor image as %s", filename_part(outname));
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
        fprintf(stderr, "  error: failed writing image to %s (%d)\n", options->using_stdout ? "stdout" : outname, retval);
    }

    return retval;
}

static pngquant_error read_image(liq_attr *options, const char *filename, int using_stdin, png24_image *input_image_p, liq_image **liq_image_p, bool keep_input_pixels, bool strip, bool verbose)
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
        retval = rwpng_read_image24(infile, input_image_p, strip, verbose);
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
