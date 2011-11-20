
#include "pam.h"
#include "viter.h"

/*
 * Voronoi iteration: new palette color is computed from weighted average of colors that map to that palette entry.
 */
void viter_init(const colormap *map,
                       f_pixel *average_color, float *average_color_count,
                       f_pixel *base_color, float *base_color_count)
{
    colormap_item *newmap = map->palette;
    int newcolors = map->colors;
    for (int i=0; i < newcolors; i++) {
        average_color_count[i] = 0;
        average_color[i] = (f_pixel){0,0,0,0};
    }

    // Rather than only using separate mapping and averaging steps
    // new palette colors are computed at the same time as mapping is done
    // but to avoid first few matches moving the entry too much
    // some base color and weight is added
    if (base_color) {
        for (int i=0; i < newcolors; i++) {
            float value = 1.0+newmap[i].popularity/2.0;
            base_color_count[i] = value;
            base_color[i] = (f_pixel){
                .a = newmap[i].acolor.a * value,
                .r = newmap[i].acolor.r * value,
                .g = newmap[i].acolor.g * value,
                .b = newmap[i].acolor.b * value,
            };
        }
    }
}

void viter_update_color(f_pixel acolor, float value, colormap *map, int match,
                               f_pixel *average_color, float *average_color_count,
                               const f_pixel *base_color, const float *base_color_count)
{
    average_color[match].a += acolor.a * value;
    average_color[match].r += acolor.r * value;
    average_color[match].g += acolor.g * value;
    average_color[match].b += acolor.b * value;
    average_color_count[match] += value;

    if (base_color) {
        map->palette[match].acolor = (f_pixel){
            .a = (average_color[match].a + base_color[match].a) / (average_color_count[match] + base_color_count[match]),
            .r = (average_color[match].r + base_color[match].r) / (average_color_count[match] + base_color_count[match]),
            .g = (average_color[match].g + base_color[match].g) / (average_color_count[match] + base_color_count[match]),
            .b = (average_color[match].b + base_color[match].b) / (average_color_count[match] + base_color_count[match]),
        };
    }
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

float viter_do_interation(const hist *hist, colormap *map, float min_opaque_val)
{
    f_pixel average_color[map->colors];
    float average_color_count[map->colors];

    hist_item *achv = hist->achv;
    viter_init(map, average_color,average_color_count, NULL,NULL);

    float total_diff=0, total_weight=0;
    for(int j=0; j < hist->size; j++) {
        float diff;
        int match = best_color_index(achv[j].acolor, map, min_opaque_val, &diff);
        total_diff += diff * achv[j].perceptual_weight;
        total_weight += achv[j].perceptual_weight;

        viter_update_color(achv[j].acolor, achv[j].perceptual_weight, map, match, average_color,average_color_count, NULL,NULL);
    }

    viter_finalize(map, average_color,average_color_count);

    return total_diff / total_weight;
}
