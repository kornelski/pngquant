
#include "pam.h"
#include "viter.h"
#include "nearest.h"
#include <stdlib.h>
#include <string.h>
/*
 * Voronoi iteration: new palette color is computed from weighted average of colors that map to that palette entry.
 */
void viter_init(const colormap *map, viter_state average_color[])
{
    memset(average_color, 0, sizeof(average_color[0])*map->colors);
}

void viter_update_color(f_pixel acolor, float value, colormap *map, int match, viter_state average_color[])
{
    average_color[match].a += acolor.a * value;
    average_color[match].r += acolor.r * value;
    average_color[match].g += acolor.g * value;
    average_color[match].b += acolor.b * value;
    average_color[match].total += value;
}

void viter_finalize(colormap *map, viter_state average_color[])
{
    for (int i=0; i < map->colors; i++) {
        if (average_color[i].total) {
            map->palette[i].acolor = (f_pixel){
                .a = (average_color[i].a) / average_color[i].total,
                .r = (average_color[i].r) / average_color[i].total,
                .g = (average_color[i].g) / average_color[i].total,
                .b = (average_color[i].b) / average_color[i].total,
            };
        }
        map->palette[i].popularity = average_color[i].total;
    }
}

double viter_do_iteration(const hist *hist, colormap *map, float min_opaque_val)
{
    viter_state average_color[map->colors];

    hist_item *achv = hist->achv;
    viter_init(map, average_color);
    struct nearest_map *n = nearest_init(map);

    double total_diff=0;
    for(int j=0; j < hist->size; j++) {
        float diff;
        int match = nearest_search(n, achv[j].acolor, min_opaque_val, &diff);
        total_diff += diff * achv[j].perceptual_weight;

        viter_update_color(achv[j].acolor, achv[j].perceptual_weight, map, match, average_color);
    }

    nearest_free(n);
    viter_finalize(map, average_color);

    return total_diff / hist->total_perceptual_weight;
}
