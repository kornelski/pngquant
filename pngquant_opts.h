
struct pngquant_options {
    liq_image *fixed_palette_image;
    liq_log_callback_function *log_callback;
    void *log_callback_user_info;
    const char *quality;
    const char *extension;
    const char *output_file_path;
    const char *map_file;
    char *const *files;
    unsigned int num_files;
    unsigned int colors;
    unsigned int speed;
    unsigned int posterize;
    float floyd;
    bool using_stdin, using_stdout, force, fast_compression,
        min_quality_limit, skip_if_larger,
        strip, iebug, last_index_transparent,
        print_help, print_version, missing_arguments,
        verbose;
};

pngquant_error pngquant_parse_options(int argc, char *argv[], struct pngquant_options *options);
