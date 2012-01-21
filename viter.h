
typedef struct {
    double a, r, g, b, total;
} viter_state;

void viter_init(const colormap *map, viter_state state[]);
void viter_update_color(f_pixel acolor, float value, colormap *map, int match, viter_state state[]);
void viter_finalize(colormap *map, viter_state state[]);
double viter_do_iteration(const hist *hist, colormap *map, float min_opaque_val);
