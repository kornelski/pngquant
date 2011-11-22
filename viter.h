
void viter_init(const colormap *map, f_pixel* average_color, float* average_color_count);
void viter_update_color(f_pixel acolor, float value, colormap *map, int match,
                               f_pixel *average_color, float *average_color_count);
void viter_finalize(colormap *map, f_pixel *average_color, float *average_color_count);
double viter_do_interation(const hist *hist, colormap *map, float min_opaque_val);
