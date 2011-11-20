//
//  nearest.h
//  pngquant
//
struct nearest_map;
struct nearest_map *nearest_init(const colormap *palette);
int nearest_search(struct nearest_map *map, f_pixel px, float min_opaque, float *diff);
void nearest_free(struct nearest_map *map);