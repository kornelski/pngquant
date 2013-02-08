//
//  nearest.h
//  pngquant
//
struct nearest_map;
struct nearest_map *nearest_init(const colormap *palette);
unsigned int nearest_search(const struct nearest_map *map, const f_pixel px, const float min_opaque, float *diff);
void nearest_free(struct nearest_map *map);
