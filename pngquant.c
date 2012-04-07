/* pngquant.c - quantize the colors in an alphamap down to a specified number
**
** Copyright (C) 1989, 1991 by Jef Poskanzer.
** Copyright (C) 1997, 2000, 2002 by Greg Roelofs; based on an idea by
**                                Stefan Schneider.
** © 2009-2012 by Kornel Lesinski.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#define PNGQUANT_VERSION "1.7.3 (April 2012)"

#define PNGQUANT_USAGE "\
   usage:  pngquant [options] [ncolors] [pngfile [pngfile ...]]\n\n\
   options:\n\
      --ext new.png  set custom suffix/extension for output filename\n\
      --force        overwrite existing output files (synonym: -f)\n\
      --iebug        increase opacity to work around Internet Explorer 6 bug\n\
      --transbug     transparent color will be placed at the end of the palette\n\
      --nofs         disable Floyd-Steinberg dithering\n\
      --speed N      speed/quality trade-off. 1=slow, 3=default, 10=fast & rough\n\
      --verbose      print status messages (synonym: -v)\n\
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
#include <stdarg.h>
#include <stdbool.h>

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
#include "pam.h"
#include "mediancut.h"
#include "nearest.h"
#include "blur.h"
#include "viter.h"

struct pngquant_options {
    int reqcolors;
    int speed_tradeoff;
    float min_opaque_val;
    bool floyd, last_index_transparent;
};

static pngquant_error pngquant(read_info *input_image, write_info *output_image, const struct pngquant_options *options);
static pngquant_error read_image(const char *filename, int using_stdin, read_info *input_image_p);
static pngquant_error write_image(write_info *output_image,const char *filename,int force,int using_stdin);
static char *add_filename_extension(const char *filename, const char *newext);
static bool file_exists(const char *outname);

static bool verbose=0;
/* prints only when verbose flag is set */
void verbose_printf(const char *fmt, ...)
{
    va_list va;
    va_start(va, fmt);
    if (verbose) vfprintf(stderr, fmt, va);
    va_end(va);
}

static void print_full_version(FILE *fd)
{
    fprintf(fd, "pngquant, version %s, by Greg Roelofs, Kornel Lesinski.\n"
        #ifndef NDEBUG
                    "   DEBUG (slow) version.\n"
        #endif
        #if USE_SSE
                    "   Compiled with SSE2 instructions.\n"
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

#if USE_SSE
inline static bool is_sse2_available()
{
#if (defined(__x86_64__) || defined(__amd64))
    return true;
#endif
    int a,b,c,d;
        cpuid(1, a, b, c, d);
    return d & (1<<26); // edx bit 26 is set when SSE2 is present
        }
#endif

static const struct {const char *old; char *new;} obsolete_options[] = {
    {"-fs","--floyd"},
    {"-nofs", "--ordered"},
    {"-floyd", "--floyd"},
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

static void fix_obsolete_options(const int argc, char *argv[])
{
    for(int argn=1; argn < argc; argn++) {
        if ('-' != argv[argn][0]) continue;

        if ('-' == argv[argn][1]) break; // stop on first --option or --

        for(int i=0; i < sizeof(obsolete_options)/sizeof(obsolete_options[0]); i++) {
            if (0 == strcmp(obsolete_options[i].old, argv[argn])) {
                fprintf(stderr, "  warning: option '%s' has been replaced with '%s'.\n", obsolete_options[i].old, obsolete_options[i].new);
                argv[argn] = obsolete_options[i].new;
            }
        }
    }
}

int main(int argc, char *argv[])
{
    struct pngquant_options options = {
        .reqcolors = 256,
        .floyd = true, // floyd-steinberg dithering
        .min_opaque_val = 1, // whether preserve opaque colors for IE (1.0=no, does not affect alpha)
        .speed_tradeoff = 3, // 1 max quality, 10 rough & fast. 3 is optimum.
        .last_index_transparent = false, // puts transparent color at last index. This is workaround for blu-ray subtitles.
    };

    bool force = false, // force overwrite
         using_stdin = false;
    int latest_error=0, error_count=0, file_count=0;
    const char *filename, *newext = NULL;

    fix_obsolete_options(argc, argv);

    int argn = 1;
    while (argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0') {
        if (0 == strcmp(argv[argn], "--")) { ++argn;break; }

        if ( 0 == strcmp(argv[argn], "--fs") ||
             0 == strcmp(argv[argn], "--floyd") )
            options.floyd = true;
        else if ( 0 == strcmp(argv[argn], "--nofs") ||
                  0 == strcmp(argv[argn], "--ordered") )
            options.floyd = false;
        else if (0 == strcmp(argv[argn], "--iebug"))
            options.min_opaque_val = 238.0/256.0; // opacities above 238 will be rounded up to 255, because IE6 truncates <255 to 0.
        else if (0 == strcmp(argv[argn], "-f") ||
		 0 == strcmp(argv[argn], "--force"))
            force = true;
        else if (0 == strcmp(argv[argn], "--no-force"))
            force = false;
        else if ( 0 == strcmp(argv[argn], "--verbose") ||
                  0 == strcmp(argv[argn], "-v"))
            verbose = true;
        else if ( 0 == strcmp(argv[argn], "--quiet") )
            verbose = false;

        else if ( 0 == strcmp(argv[argn], "--transbug"))
            options.last_index_transparent = true;

        else if ( 0 == strcmp(argv[argn], "-V") ||
		  0 == strcmp(argv[argn], "--version")) {
            puts(PNGQUANT_VERSION);
            return SUCCESS;
        } else if ( 0 == strcmp(argv[argn], "-h") ||
		    0 == strcmp(argv[argn], "--help")) {
            print_full_version(stdout);
            print_usage(stdout);
            return SUCCESS;
        } else if (0 == strcmp(argv[argn], "--ext")) {
            ++argn;
            if (argn == argc) {
                print_usage(stderr);
                return MISSING_ARGUMENT;
            }
            newext = argv[argn];
        }
        else if (0 == strcmp(argv[argn], "-s") ||
                 0 == strcmp(argv[argn], "--speed")) {
            ++argn;
            if (argn == argc) {
                print_usage(stderr);
                return MISSING_ARGUMENT;
            }
            options.speed_tradeoff = atoi(argv[argn]);
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

    if (sscanf(argv[argn], "%d", &options.reqcolors) == 1) {
        argn++;
    }

    if (options.reqcolors < 2 || options.reqcolors > 256) {
        fputs("number of colors must be between 2 and 256\n", stderr);
        return INVALID_ARGUMENT;
    }

    if (options.speed_tradeoff < 1 || options.speed_tradeoff > 10) {
        fputs("speed should be between 1 (slow) and 10 (fast)\n", stderr);
        return INVALID_ARGUMENT;
    }

    // new filename extension depends on options used. Typically basename-fs8.png
    if (newext == NULL) {
        newext = options.floyd ? "-ie-fs8.png" : "-ie-or8.png";
        if (options.min_opaque_val == 1.f) newext += 3; /* skip "-ie" */
    }

    if (argn == argc || (argn == argc-1 && 0==strcmp(argv[argn],"-"))) {
        using_stdin = true;
        filename = "stdin";
        argn = argc;
    } else {
        filename = argv[argn];
        ++argn;
    }

#if USE_SSE
    if (!is_sse2_available()) {
        print_full_version(stderr);
        fputs("SSE2-capable CPU is required for this build.\n", stderr);
        return WRONG_ARCHITECTURE;
    }
#endif

    /*=============================  MAIN LOOP  =============================*/

    while (argn <= argc) {
        int retval = 0;

        verbose_printf("%s:\n", filename);

        char *outname = NULL;
        if (!using_stdin) {
            outname = add_filename_extension(filename,newext);
            if (!force && file_exists(outname)) {
                fprintf(stderr, "  error:  %s exists; not overwriting\n", outname);
                retval = NOT_OVERWRITING_ERROR;
            }
        }

        read_info input_image = {}; // initializes all fields to 0
        write_info output_image = {};

        if (!retval) {
            retval = read_image(filename,using_stdin,&input_image);
        }

        if (!retval) {
            verbose_printf("  read %uKB file corrected for gamma %2.1f\n", (input_image.file_size+1023)/1024,
                           1.0/input_image.gamma);

            retval = pngquant(&input_image, &output_image, &options);
        }

        /* now we're done with the INPUT data and row_pointers, so free 'em */
        if (input_image.rgba_data) {
            free(input_image.rgba_data);
        }
        if (input_image.row_pointers) {
            free(input_image.row_pointers);
        }

        if (!retval) {
            retval = write_image(&output_image,outname,force,using_stdin);
        }

        if (outname) free(outname);

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

static int compare_popularity(const void *ch1, const void *ch2)
{
    const float v1 = ((const colormap_item*)ch1)->popularity;
    const float v2 = ((const colormap_item*)ch2)->popularity;
    return v1 > v2 ? 1 : -1;
}

static void sort_palette(write_info *output_image, colormap *map, int last_index_transparent)
{
    assert(map); assert(output_image);

    /*
    ** Step 3.5 [GRR]: remap the palette colors so that all entries with
    ** the maximal alpha value (i.e., fully opaque) are at the end and can
    ** therefore be omitted from the tRNS chunk.
    */

    verbose_printf("  eliminating opaque tRNS-chunk entries...");
    output_image->num_palette = map->colors;

    if (last_index_transparent) for(int i=0; i < map->colors; i++) {
        if (map->palette[i].acolor.a < 1.0/256.0) {
            const int old = i;
            const int transparent_dest = map->colors-1;
            const colormap_item tmp = map->palette[transparent_dest];
            map->palette[transparent_dest] = map->palette[old];
            map->palette[old] = tmp;

            /* colors sorted by popularity make pngs slightly more compressible */
            qsort(map->palette, map->colors-1, sizeof(map->palette[0]), compare_popularity);

            output_image->num_trans = map->colors;
            return;
        }
    }

    /* move transparent colors to the beginning to shrink trns chunk */
    int num_transparent=0;
    for(int i=0; i < map->colors; i++) {
        rgb_pixel px = to_rgb(output_image->gamma, map->palette[i].acolor);
        if (px.a != 255) {
            // current transparent color is swapped with earlier opaque one
            if (i != num_transparent) {
                const colormap_item tmp = map->palette[num_transparent];
                map->palette[num_transparent] = map->palette[i];
                map->palette[i] = tmp;
                i--;
            }
            num_transparent++;
        }
    }

    verbose_printf("%d entr%s transparent\n", num_transparent, (num_transparent == 1)? "y" : "ies");

    /* colors sorted by popularity make pngs slightly more compressible
     * opaque and transparent are sorted separately
     */
    qsort(map->palette, num_transparent, sizeof(map->palette[0]), compare_popularity);
    qsort(map->palette+num_transparent, map->colors-num_transparent, sizeof(map->palette[0]), compare_popularity);

    output_image->num_trans = num_transparent;
}

static void set_palette(write_info *output_image, const colormap *map)
{
    for (int x = 0; x < map->colors; ++x) {
        rgb_pixel px = to_rgb(output_image->gamma, map->palette[x].acolor);
        map->palette[x].acolor = to_f(output_image->gamma, px); /* saves rounding error introduced by to_rgb, which makes remapping & dithering more accurate */

        output_image->palette[x].red   = px.r;
        output_image->palette[x].green = px.g;
        output_image->palette[x].blue  = px.b;
        output_image->trans[x]         = px.a;
    }
}

static float remap_to_palette(read_info *input_image, write_info *output_image, colormap *const map, const float min_opaque_val)
{
    const rgb_pixel *const *const input_pixels = (const rgb_pixel **)input_image->row_pointers;
    unsigned char *const *const row_pointers = output_image->row_pointers;
    const int rows = input_image->height, cols = input_image->width;
    const float gamma = input_image->gamma;

    int remapped_pixels=0;
    float remapping_error=0;

    struct nearest_map *const n = nearest_init(map);
    const int transparent_ind = nearest_search(n, (f_pixel){0,0,0,0}, min_opaque_val, NULL);

    const int max_threads = omp_get_max_threads();
    viter_state average_color[map->colors * max_threads];
    viter_init(map, max_threads, average_color);

    #pragma omp parallel for if (rows*cols > 3000) \
        default(none) shared(average_color) reduction(+:remapping_error) reduction(+:remapped_pixels)
    for (int row = 0; row < rows; ++row) {
        for(int col = 0; col < cols; ++col) {

            f_pixel px = to_f(gamma, input_pixels[row][col]);
            int match;

            if (px.a < 1.0/256.0) {
                match = transparent_ind;
            } else {
                float diff;
                match = nearest_search(n, px, min_opaque_val, &diff);

                remapped_pixels++;
                remapping_error += diff;
            }

            row_pointers[row][col] = match;

            viter_update_color(px, 1.0, map, match, omp_get_thread_num(), average_color);
        }
    }

    viter_finalize(map, max_threads, average_color);

    nearest_free(n);

    return remapping_error / MAX(1,remapped_pixels);
}

static float distance_from_closest_other_color(const colormap *map, const int i)
{
    float second_best=999999;
    for(int j=0; j < map->colors; j++) {
        if (i == j) continue;
        float diff = colordifference(map->palette[i].acolor, map->palette[j].acolor);
        if (diff <= second_best) {
            second_best = diff;
        }
    }
    return second_best;
}

/**
  Uses edge/noise map to apply dithering only to flat areas. Dithering on edges creates jagged lines, and noisy areas are "naturally" dithered.

  If output_image_is_remapped is true, only pixels noticeably changed by error diffusion will be written to output image.
 */
static void remap_to_palette_floyd(read_info *input_image, write_info *output_image, const colormap *map, const float min_opaque_val, const float *edge_map, const int output_image_is_remapped)
{
    const rgb_pixel *const *const input_pixels = (const rgb_pixel *const *const)input_image->row_pointers;
    unsigned char *const *const row_pointers = output_image->row_pointers;
    const int rows = input_image->height, cols = input_image->width;
    const float gamma = input_image->gamma;

    const colormap_item *acolormap = map->palette;

    struct nearest_map *const n = nearest_init(map);
    const int transparent_ind = nearest_search(n, (f_pixel){0,0,0,0}, min_opaque_val, NULL);

    float difference_tolerance[map->colors];
    if (output_image_is_remapped) for(int i=0; i < map->colors; i++) {
        difference_tolerance[i] = distance_from_closest_other_color(map,i) / 4.f; // half of squared distance
    }

    f_pixel *restrict thiserr = NULL;
    f_pixel *restrict nexterr = NULL;
    float sr=0, sg=0, sb=0, sa=0;
    int fs_direction = 1;

    /* Initialize Floyd-Steinberg error vectors. */
    thiserr = malloc((cols + 2) * sizeof(*thiserr));
    nexterr = malloc((cols + 2) * sizeof(*thiserr));
    srand(12345); /* deterministic dithering is better for comparing results */

    for (int col = 0; col < cols + 2; ++col) {
        const double rand_max = RAND_MAX;
        thiserr[col].r = ((double)rand() - rand_max/2.0)/rand_max/255.0;
        thiserr[col].g = ((double)rand() - rand_max/2.0)/rand_max/255.0;
        thiserr[col].b = ((double)rand() - rand_max/2.0)/rand_max/255.0;
        thiserr[col].a = ((double)rand() - rand_max/2.0)/rand_max/255.0;
    }

    for (int row = 0; row < rows; ++row) {
        memset(nexterr, 0, (cols + 2) * sizeof(*nexterr));

        int col = (fs_direction) ? 0 : (cols - 1);

        do {
            const f_pixel px = to_f(gamma, input_pixels[row][col]);

            float dither_level = edge_map ? edge_map[row*cols + col] : 0.9f;

            /* Use Floyd-Steinberg errors to adjust actual color. */
            sr = px.r + thiserr[col + 1].r * dither_level;
            sg = px.g + thiserr[col + 1].g * dither_level;
            sb = px.b + thiserr[col + 1].b * dither_level;
            sa = px.a + thiserr[col + 1].a * dither_level;

            // Error must be clamped, otherwise it can accumulate so much that it will be
            // impossible to compensate it, causing color streaks
            if (sr < 0) sr = 0;
            else if (sr > 1) sr = 1;
            if (sg < 0) sg = 0;
            else if (sg > 1) sg = 1;
            if (sb < 0) sb = 0;
            else if (sb > 1) sb = 1;
            if (sa < 0) sa = 0;
            else if (sa > 1) sa = 1;

            int ind;
            if (sa < 1.0/256.0) {
                ind = transparent_ind;
            } else {
                f_pixel spx = (f_pixel){.r=sr, .g=sg, .b=sb, .a=sa};
                int curr_ind = row_pointers[row][col];
                if (output_image_is_remapped && colordifference(map->palette[curr_ind].acolor, spx) < difference_tolerance[curr_ind]) {
                    ind = curr_ind;
                } else {
                    ind = nearest_search(n, spx, min_opaque_val, NULL);
            }
            }

            row_pointers[row][col] = ind;

            const f_pixel xp = acolormap[ind].acolor;
            f_pixel err = {
                .r = (sr - xp.r),
                .g = (sg - xp.g),
                .b = (sb - xp.b),
                .a = (sa - xp.a),
            };

            // If dithering error is crazy high, don't propagate it that much
            // This prevents crazy geen pixels popping out of the blue (or red or black! ;)
            if (err.r*err.r + err.g*err.g + err.b*err.b + err.a*err.a > 16.f/256.f/256.f) {
                dither_level *= 0.75;
            }

            const float colorimp = (3.0f + acolormap[ind].acolor.a)/4.0f * dither_level;
            err.r *= colorimp;
            err.g *= colorimp;
            err.b *= colorimp;
            err.a *= dither_level;

            /* Propagate Floyd-Steinberg error terms. */
            if (fs_direction) {
                thiserr[col + 2].a += (err.a * 7.0f) / 16.0f;
                thiserr[col + 2].r += (err.r * 7.0f) / 16.0f;
                thiserr[col + 2].g += (err.g * 7.0f) / 16.0f;
                thiserr[col + 2].b += (err.b * 7.0f) / 16.0f;

                nexterr[col    ].a += (err.a * 3.0f) / 16.0f;
                nexterr[col    ].r += (err.r * 3.0f) / 16.0f;
                nexterr[col    ].g += (err.g * 3.0f) / 16.0f;
                nexterr[col    ].b += (err.b * 3.0f) / 16.0f;

                nexterr[col + 1].a += (err.a * 5.0f) / 16.0f;
                nexterr[col + 1].r += (err.r * 5.0f) / 16.0f;
                nexterr[col + 1].g += (err.g * 5.0f) / 16.0f;
                nexterr[col + 1].b += (err.b * 5.0f) / 16.0f;

                nexterr[col + 2].a += (err.a       ) / 16.0f;
                nexterr[col + 2].r += (err.r       ) / 16.0f;
                nexterr[col + 2].g += (err.g       ) / 16.0f;
                nexterr[col + 2].b += (err.b       ) / 16.0f;
            } else {
                thiserr[col    ].a += (err.a * 7.0f) / 16.0f;
                thiserr[col    ].r += (err.r * 7.0f) / 16.0f;
                thiserr[col    ].g += (err.g * 7.0f) / 16.0f;
                thiserr[col    ].b += (err.b * 7.0f) / 16.0f;

                nexterr[col    ].a += (err.a       ) / 16.0f;
                nexterr[col    ].r += (err.r       ) / 16.0f;
                nexterr[col    ].g += (err.g       ) / 16.0f;
                nexterr[col    ].b += (err.b       ) / 16.0f;

                nexterr[col + 1].a += (err.a * 5.0f) / 16.0f;
                nexterr[col + 1].r += (err.r * 5.0f) / 16.0f;
                nexterr[col + 1].g += (err.g * 5.0f) / 16.0f;
                nexterr[col + 1].b += (err.b * 5.0f) / 16.0f;

                nexterr[col + 2].a += (err.a * 3.0f) / 16.0f;
                nexterr[col + 2].r += (err.r * 3.0f) / 16.0f;
                nexterr[col + 2].g += (err.g * 3.0f) / 16.0f;
                nexterr[col + 2].b += (err.b * 3.0f) / 16.0f;
            }

            // remapping is done in zig-zag
            if (fs_direction) {
                ++col;
                if (col >= cols) break;
            } else {
                --col;
                if (col < 0) break;
            }
        }
        while(1);

        f_pixel *temperr;
        temperr = thiserr;
        thiserr = nexterr;
        nexterr = temperr;
        fs_direction = !fs_direction;
    }

    free(thiserr);
    free(nexterr);
    nearest_free(n);
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

    strncpy(outname, filename, x);
    if (strncmp(outname+x-4, ".png", 4) == 0)
        strcpy(outname+x-4, newext);
    else
        strcpy(outname+x, newext);

    return outname;
}

static void set_binary_mode(FILE *fp)
{
#if defined(WIN32) || defined(__WIN32__)
    setmode(fp == stdout ? 1 : 0, O_BINARY);
#endif
}

static pngquant_error write_image(write_info *output_image,const char *outname,int force,int using_stdin)
{
    FILE *outfile;
    if (using_stdin) {
        set_binary_mode(stdout);
        outfile = stdout;

        verbose_printf("  writing %d-color image to stdout\n", output_image->num_palette);
    } else {

        if ((outfile = fopen(outname, "wb")) == NULL) {
            fprintf(stderr, "  error:  cannot open %s for writing\n", outname);
            return CANT_WRITE_ERROR;
        }

        const char *outfilename = strrchr(outname, '/');
        if (outfilename) outfilename++; else outfilename = outname;
        verbose_printf("  writing %d-color image as %s\n", output_image->num_palette, outfilename);
    }

    pngquant_error retval = rwpng_write_image(outfile, output_image);
    if (retval) {
        fprintf(stderr, "  Error writing image to %s\n", outname);
    }

    if (!using_stdin)
        fclose(outfile);

    /* now we're done with the OUTPUT data and row_pointers, too */
    return retval;
}

/* histogram contains information how many times each color is present in the image, weighted by importance_map */
static histogram *get_histogram(const read_info *input_image, const int reqcolors, const int speed_tradeoff, const float *importance_map)
{
    histogram *hist;
    int ignorebits=0;
    const rgb_pixel **input_pixels = (const rgb_pixel **)input_image->row_pointers;
    const int cols = input_image->width, rows = input_image->height;
    const float gamma = input_image->gamma;
    assert(gamma > 0);

   /*
    ** Step 2: attempt to make a histogram of the colors, unclustered.
    ** If at first we don't succeed, increase ignorebits to increase color
    ** coherence and try again.
    */

    if (speed_tradeoff > 7) ignorebits++;
    int maxcolors = (1<<17) + (1<<18)*(10-speed_tradeoff);

    verbose_printf("  making histogram...");
    for (; ;) {

        hist = pam_computeacolorhist(input_pixels, cols, rows, gamma, maxcolors, ignorebits, importance_map);
        if (hist) break;

        ignorebits++;
        verbose_printf("too many colors!\n  scaling colors to improve clustering...");
    }

    verbose_printf("%d colors found\n", hist->size);
    return hist;
}

static void modify_alpha(read_info *input_image, const float min_opaque_val)
{
    /* IE6 makes colors with even slightest transparency completely transparent,
       thus to improve situation in IE, make colors that are less than ~10% transparent
       completely opaque */

    rgb_pixel *const *const input_pixels = (rgb_pixel **)input_image->row_pointers;
    const int rows= input_image->height, cols = input_image->width;
    const float gamma = input_image->gamma;

    if (min_opaque_val > 254.f/255.f) return;

    const float almost_opaque_val = min_opaque_val * 169.f/256.f;
    const int almost_opaque_val_int = almost_opaque_val*255.f;

    verbose_printf("  Working around IE6 bug by making image less transparent...\n");

    for(int row = 0; row < rows; ++row) {
        for(int col = 0; col < cols; col++) {
            const rgb_pixel srcpx = input_pixels[row][col];

            /* ie bug: to avoid visible step caused by forced opaqueness, linearily raise opaqueness of almost-opaque colors */
            if (srcpx.a >= almost_opaque_val_int) {
                f_pixel px = to_f(gamma, srcpx);

                float al = almost_opaque_val + (px.a-almost_opaque_val) * (1-almost_opaque_val) / (min_opaque_val-almost_opaque_val);
                if (al > 1) al = 1;
                px.a = al;
                input_pixels[row][col] = to_rgb(gamma, px);
            }
        }
    }
}

static pngquant_error read_image(const char *filename, int using_stdin, read_info *input_image_p)
{
    FILE *infile;

    if (using_stdin) {
        set_binary_mode(stdin);
        infile = stdin;
    } else if ((infile = fopen(filename, "rb")) == NULL) {
        fprintf(stderr, "  error:  cannot open %s for reading\n", filename);
        return READ_ERROR;
    }

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

/**
 Builds two maps:
    noise - approximation of areas with high-frequency noise, except straight edges. 1=flat, 0=noisy.
    edges - noise map including all edges
 */
static void contrast_maps(const rgb_pixel*const apixels[], const int cols, const int rows, const float gamma, float **noiseP, float **edgesP)
{
    float *restrict noise = malloc(sizeof(float)*cols*rows);
    float *restrict tmp = malloc(sizeof(float)*cols*rows);
    float *restrict edges = malloc(sizeof(float)*cols*rows);

    for (int j=0; j < rows; j++) {
        f_pixel prev, curr = to_f(gamma, apixels[j][0]), next=curr;
        for (int i=0; i < cols; i++) {
            prev=curr;
            curr=next;
            next = to_f(gamma, apixels[j][MIN(cols-1,i+1)]);

            // contrast is difference between pixels neighbouring horizontally and vertically
            float a = fabsf(prev.a+next.a - curr.a*2.f),
            r = fabsf(prev.r+next.r - curr.r*2.f),
            g = fabsf(prev.g+next.g - curr.g*2.f),
            b = fabsf(prev.b+next.b - curr.b*2.f);

            f_pixel nextl = to_f(gamma, apixels[MAX(0,j-1)][i]);
            f_pixel prevl = to_f(gamma, apixels[MIN(rows-1,j+1)][i]);

            float a1 = fabsf(prevl.a+nextl.a - curr.a*2.f),
            r1 = fabsf(prevl.r+nextl.r - curr.r*2.f),
            g1 = fabsf(prevl.g+nextl.g - curr.g*2.f),
            b1 = fabsf(prevl.b+nextl.b - curr.b*2.f);

            float horiz = MAX(MAX(a,r),MAX(g,b));
            float vert = MAX(MAX(a1,r1),MAX(g1,b1));
            float edge = MAX(horiz,vert);
            float z = edge - fabsf(horiz-vert)*.5f;
            z = 1.f - MAX(z,MIN(horiz,vert));
            z *= z; // noise is amplified
            z *= z;

            noise[j*cols+i] = z;
            edges[j*cols+i] = 1.f-edge;
        }
    }

    // noise areas are shrunk and then expanded to remove thin edges from the map
    max3(noise, tmp, cols, rows);
    max3(tmp, noise, cols, rows);

    blur(noise, tmp, noise, cols, rows, 3);

    max3(noise, tmp, cols, rows);

    min3(tmp, noise, cols, rows);
    min3(noise, tmp, cols, rows);
    min3(tmp, noise, cols, rows);

    min3(edges, tmp, cols, rows);
    max3(tmp, edges, cols, rows);
    for(int i=0; i < cols*rows; i++) edges[i] = MIN(noise[i], edges[i]);

    free(tmp);

    *noiseP = noise;
    *edgesP = edges;
}

/**
 * Builds map of neighbor pixels mapped to the same palette entry
 *
 * For efficiency/simplicity it mainly looks for same consecutive pixels horizontally
 * and peeks 1 pixel above/below. Full 2d algorithm doesn't improve it significantly.
 * Correct flood fill doesn't have visually good properties.
 */
static void update_dither_map(const write_info *output_image, float *edges)
{
    const int width = output_image->width;
    const int height = output_image->height;
    const unsigned char *restrict pixels = output_image->indexed_data;

    for(int row=0; row < height; row++)
    {
        unsigned char lastpixel = pixels[row*width];
        int lastcol=0;
        for(int col=1; col < width; col++)
        {
            const unsigned char px = pixels[row*width + col];

            if (px != lastpixel || col == width-1) {
                float neighbor_count = 2.5f + col-lastcol;

                int i=lastcol;
                while(i < col) {
                    if (row > 0) {
                        unsigned char pixelabove = pixels[(row-1)*width + i];
                        if (pixelabove == lastpixel) neighbor_count += 1.f;
                    }
                    if (row < height-1) {
                        unsigned char pixelbelow = pixels[(row+1)*width + i];
                        if (pixelbelow == lastpixel) neighbor_count += 1.f;
                    }
                    i++;
                }

                while(lastcol <= col) {
                    edges[row*width + lastcol++] *= 1.f - 2.5f/neighbor_count;
                }
                lastpixel = px;
            }
        }
    }
}

static void adjust_histogram_callback(hist_item *item, float diff)
{
    item->adjusted_weight = (item->perceptual_weight+item->adjusted_weight) * (sqrtf(1.f+diff));
}

/**
 Repeats mediancut with different histogram weights to find palette with minimum error.

 feedback_loop_trials controls how long the search will take. < 0 skips the iteration.
 */
static colormap *find_best_palette(histogram *hist, const int reqcolors, const float min_opaque_val, int feedback_loop_trials, double *palette_error_p)
{
    colormap *acolormap = NULL;
    double least_error = 0;
    const double percent = (double)(feedback_loop_trials>0?feedback_loop_trials:1)/100.0;

    do
    {
        verbose_printf("  selecting colors");

        colormap *newmap = mediancut(hist, min_opaque_val, reqcolors);
        if (newmap->subset_palette) {
            // nearest_search() needs subset palette to accelerate the search, I presume that
            // palette starting with most popular colors will improve search speed
            qsort(newmap->subset_palette->palette, newmap->subset_palette->colors, sizeof(newmap->subset_palette->palette[0]), compare_popularity);
        }

        if (feedback_loop_trials <= 0) {
            verbose_printf("\n");
            return newmap;
        }

        verbose_printf("...");

        // after palette has been created, total error (MSE) is calculated to keep the best palette
        // at the same time Voronoi iteration is done to improve the palette
        // and histogram weights are adjusted based on remapping error to give more weight to poorly matched colors

        double total_error = viter_do_iteration(hist, newmap, min_opaque_val, adjust_histogram_callback);

        if (!acolormap || total_error < least_error) {
            if (acolormap) pam_freecolormap(acolormap);
            acolormap = newmap;

            least_error = total_error;
            feedback_loop_trials -= 1; // asymptotic improvement could make it go on forever
        } else {
            feedback_loop_trials -= 6;
            // if error is really bad, it's unlikely to improve, so end sooner
            if (total_error > least_error*4) feedback_loop_trials -= 3;
            pam_freecolormap(newmap);
        }

        verbose_printf("%d%%\n",100-MAX(0,(int)(feedback_loop_trials/percent)));
    }
    while(feedback_loop_trials > 0);

    *palette_error_p = least_error;
    return acolormap;
}

static pngquant_error pngquant(read_info *input_image, write_info *output_image, const struct pngquant_options *options)
{
    const int speed_tradeoff = options->speed_tradeoff, reqcolors = options->reqcolors;
    const float min_opaque_val = options->min_opaque_val;
    assert(min_opaque_val>0);
    modify_alpha(input_image, min_opaque_val);


    float *noise = NULL, *edges = NULL;
    if (speed_tradeoff < 8 && input_image->width >= 4 && input_image->height >= 4) {
        contrast_maps((const rgb_pixel**)input_image->row_pointers, input_image->width, input_image->height, input_image->gamma,
                   &noise, &edges);
    }

    // histogram uses noise contrast map for importance. Color accuracy in noisy areas is not very important.
    // noise map does not include edges to avoid ruining anti-aliasing
    histogram *hist = get_histogram(input_image, reqcolors, speed_tradeoff, noise); if (noise) free(noise);

    double palette_error = -1;
    colormap *acolormap = find_best_palette(hist, reqcolors, min_opaque_val, 56-9*speed_tradeoff, &palette_error);

        verbose_printf("  moving colormap towards local minimum\n");

    // Voronoi iteration approaches local minimum for the palette
    int iterations = MAX(8-speed_tradeoff,0); iterations += iterations * iterations/2;
        const double iteration_limit = 1.0/(double)(1<<(23-speed_tradeoff));
        double previous_palette_error = 9999999;
        for(int i=0; i < iterations; i++) {
            palette_error = viter_do_iteration(hist, acolormap, min_opaque_val, NULL);

            if (fabs(previous_palette_error-palette_error) < iteration_limit) {
                break;
            }
            previous_palette_error = palette_error;
        }

    pam_freeacolorhist(hist);

    output_image->width = input_image->width;
    output_image->height = input_image->height;
    output_image->gamma = 0.45455f; // fixed gamma ~2.2 for the web. PNG can't store exact 1/2.2

    /*
    ** Step 3.7 [GRR]: allocate memory for the entire indexed image
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

    // tRNS, etc.
    sort_palette(output_image, acolormap, options->last_index_transparent);

    /*
     ** Step 4: map the colors in the image to their closest match in the
     ** new colormap, and write 'em out.
     */
    verbose_printf("  mapping image to new colors...");

    const int floyd = options->floyd,
              use_dither_map = floyd && edges && speed_tradeoff < 6;

    if (!floyd || use_dither_map) {
        // If no dithering is required, that's the final remapping.
        // If dithering (with dither map) is required, this image is used to find areas that require dithering
        float remapping_error = remap_to_palette(input_image, output_image, acolormap, min_opaque_val);

        // remapping error from dithered image is absurd, so always non-dithered value is used
        // palette_error includes some perceptual weighting from histogram which is closer correlated with dssim
        // so that should be used when possible.
        if (palette_error < 0) {
            palette_error = remapping_error;
        }

        if (use_dither_map) {
            update_dither_map(output_image, edges);
        }
    }

    if (palette_error >= 0) {
        verbose_printf("MSE=%.3f", palette_error*256.0f*256.0f);
    }

    // remapping above was the last chance to do voronoi iteration, hence the final palette is set after remapping
    set_palette(output_image, acolormap);

    if (floyd) {
        remap_to_palette_floyd(input_image, output_image, acolormap, min_opaque_val, edges, use_dither_map);
    }

    verbose_printf("\n");

    if (edges) free(edges);
    pam_freecolormap(acolormap);

    return SUCCESS;
}


