
#include "pam.h"
#include "viter.h"
#include "nearest.h"
#include <stdlib.h>
#include <string.h>
/*
 * Voronoi iteration: new palette color is computed from weighted average of colors that map to that palette entry.
 */
void viter_init(const colormap *map, const int max_threads, viter_state average_color[])
{
    memset(average_color, 0, sizeof(average_color[0])*map->colors*max_threads);
}

void viter_update_color(const f_pixel acolor, const float value, const colormap *map, int match, const int thread, viter_state average_color[])
{
    match += thread * map->colors;
    average_color[match].a += acolor.a * value;
    average_color[match].r += acolor.r * value;
    average_color[match].g += acolor.g * value;
    average_color[match].b += acolor.b * value;
    average_color[match].total += value;
}

void viter_finalize(colormap *map, const int max_threads, const viter_state average_color[])
{
    for (int i=0; i < map->colors; i++) {
        double a=0, r=0, g=0, b=0, total=0;

        // Aggregate results from all threads
        for(int t=0; t < max_threads; t++) {
            const int offset = map->colors * t + i;

            a += average_color[offset].a;
            r += average_color[offset].r;
            g += average_color[offset].g;
            b += average_color[offset].b;
            total += average_color[offset].total;
        }

        if (total) {
            map->palette[i].acolor = (f_pixel){
                .a = a / total,
                .r = r / total,
                .g = g / total,
                .b = b / total,
            };
        }
        map->palette[i].popularity = total;
    }
}

double viter_do_iteration(hist *hist, colormap *map, const float min_opaque_val, viter_callback callback)
{
    viter_state average_color[map->colors];
    viter_init(map, 1, average_color);
    struct nearest_map *const n = nearest_init(map);
    hist_item *achv = hist->achv;

    double total_diff=0;
    for(int j=0; j < hist->size; j++) {
        float diff;
        int match = nearest_search(n, achv[j].acolor, min_opaque_val, &diff);
        total_diff += diff * achv[j].perceptual_weight;

        viter_update_color(achv[j].acolor, achv[j].perceptual_weight, map, match, 0, average_color);

        if (callback) callback(&achv[j], diff);
    }

    nearest_free(n);
    viter_finalize(map, 1, average_color);

    return total_diff / hist->total_perceptual_weight;
}
