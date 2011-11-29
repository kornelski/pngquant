
#include "pam.h"
#include "viter.h"
#include "nearest.h"
#include <stdlib.h>

/*
 * Voronoi iteration: new palette color is computed from weighted average of colors that map to that palette entry.
 */
void viter_init(const colormap *map, f_pixel *average_color, float *average_color_count)
{
    for (int i=0; i < map->colors; i++) {
        average_color_count[i] = 0;
        average_color[i] = (f_pixel){0,0,0,0};
    }
}

void viter_update_color(f_pixel acolor, float value, colormap *map, int match, f_pixel *average_color, float *average_color_count)
{
    average_color[match].a += acolor.a * value;
    average_color[match].r += acolor.r * value;
    average_color[match].g += acolor.g * value;
    average_color[match].b += acolor.b * value;
    average_color_count[match] += value;
}

void viter_finalize(colormap *map, f_pixel *average_color, float *average_color_count)
{
    for (int i=0; i < map->colors; i++) {
        if (average_color_count[i]) {
            map->palette[i].acolor = (f_pixel){
                .a = (average_color[i].a) / average_color_count[i],
                .r = (average_color[i].r) / average_color_count[i],
                .g = (average_color[i].g) / average_color_count[i],
                .b = (average_color[i].b) / average_color_count[i],
            };
        }
        map->palette[i].popularity = average_color_count[i];
    }
}

double viter_do_interation(const hist *hist, colormap *map, float min_opaque_val)
{
    f_pixel average_color[map->colors];
    float average_color_count[map->colors];

    hist_item *achv = hist->achv;
    viter_init(map, average_color,average_color_count);
    struct nearest_map *n = nearest_init(map);

    double total_diff=0;
    for(int j=0; j < hist->size; j++) {
        float diff;
        int match = nearest_search(n, achv[j].acolor, min_opaque_val, &diff);
        total_diff += diff * achv[j].perceptual_weight;

        viter_update_color(achv[j].acolor, achv[j].perceptual_weight, map, match, average_color,average_color_count);
    }

    nearest_free(n);
    viter_finalize(map, average_color,average_color_count);

    return total_diff / hist->total_perceptual_weight;
}
