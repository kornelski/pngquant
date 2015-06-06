
#ifndef VITER_H
#define VITER_H

// Spread memory touched by different threads at least 64B apart which I assume is the cache line size. This should avoid memory write contention.
#define VITER_CACHE_LINE_GAP ((64+sizeof(viter_state)-1)/sizeof(viter_state))

typedef struct {
    double a, r, g, b, total;
} viter_state;

typedef void (*viter_callback)(hist_item *item, float diff);

LIQ_PRIVATE void viter_init(const colormap *map, const unsigned int max_threads, viter_state state[]);
LIQ_PRIVATE void viter_update_color(const f_pixel acolor, const float value, const colormap *map, unsigned int match, const unsigned int thread, viter_state average_color[]);
LIQ_PRIVATE void viter_finalize(colormap *map, const unsigned int max_threads, const viter_state state[]);
LIQ_PRIVATE double viter_do_iteration(histogram *hist, colormap *const map, viter_callback callback, const bool fast_palette);

#endif
