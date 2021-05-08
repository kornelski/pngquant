/*
** © 2017 by Kornel Lesiński.
**
** See COPYRIGHT file for license.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>

#include "rwpng.h"
#include "libimagequant.h" /* if you get compile error, add -Ilib to compiler flags */
#include "pngquant_opts.h"

extern char *optarg;
extern int optind, opterr;

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
    arg_transbug, arg_map, arg_posterize, arg_skip_larger, arg_strip};

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
    {"strip", no_argument, NULL, arg_strip},
    {"map", required_argument, NULL, arg_map},
    {"version", no_argument, NULL, 'V'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0},
};

pngquant_error pngquant_parse_options(int argc, char *argv[], struct pngquant_options *options)
{
    fix_obsolete_options(argc, argv);

    int opt;
    do {
        opt = getopt_long(argc, argv, "Vvqfhs:Q:o:", long_options, NULL);
        switch (opt) {
            case 'v':
                options->verbose = true;
                break;
            case 'q':
                options->verbose = false;
                break;

            case arg_floyd:
                options->floyd = optarg ? atof(optarg) : 1.f;
                if (options->floyd < 0 || options->floyd > 1.f) {
                    fputs("--floyd argument must be in 0..1 range\n", stderr);
                    return INVALID_ARGUMENT;
                }
                break;
            case arg_ordered: options->floyd = 0; break;

            case 'f': options->force = true; break;
            case arg_no_force: options->force = false; break;

            case arg_ext: options->extension = optarg; break;
            case 'o':
                if (options->output_file_path) {
                    fputs("--output option can be used only once\n", stderr);
                    return INVALID_ARGUMENT;
                }
                if (strcmp(optarg, "-") == 0) {
                    options->using_stdout = true;
                    break;
                }
                options->output_file_path = optarg; break;

            case arg_iebug:
                options->iebug = true;
                break;

            case arg_transbug:
                options->last_index_transparent = true;
                break;

            case arg_skip_larger:
                options->skip_if_larger = true;
                break;

            case 's':
                options->speed = optarg[0] == '0' ? -1 : atoi(optarg);
                break;

            case 'Q':
                options->quality = optarg;
                break;

            case arg_posterize:
                options->posterize = atoi(optarg);
                break;

            case arg_strip:
                options->strip = true;
                break;

            case arg_map:
                options->map_file = optarg;
                break;

            case 'h':
                options->print_help = true;
                break;

            case 'V':
                options->print_version = true;
                break;

            case -1: break;

            default:
                return INVALID_ARGUMENT;
        }
    } while (opt != -1);

    int argn = optind;

    if (argn < argc) {
        char *colors_end;
        unsigned long colors = strtoul(argv[argn], &colors_end, 10);
        if (colors_end != argv[argn] && '\0' == colors_end[0]) {
            options->colors = colors;
            argn++;
        }

        if (argn == argc || (argn == argc-1 && 0==strcmp(argv[argn],"-"))) {
            options->using_stdin = true;
            options->using_stdout = !options->output_file_path;
            argn = argc-1;
        }

        options->num_files = argc-argn;
        options->files = argv+argn;
    } else if (argn <= 1) {
        options->missing_arguments = true;
    }

    return SUCCESS;
}
