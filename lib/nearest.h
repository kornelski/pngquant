//
//  nearest.h
//  pngquant
//
struct nearest_map;
LIQ_PRIVATE struct nearest_map *nearest_init(const colormap *palette);
LIQ_PRIVATE unsigned int nearest_search(const struct nearest_map *map, const f_pixel px, const float min_opaque, float *diff);
LIQ_PRIVATE void nearest_free(struct nearest_map *map);
