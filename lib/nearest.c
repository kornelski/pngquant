
#include "libimagequant.h"
#include "pam.h"
#include "nearest.h"
#include "mempool.h"
#include <stdlib.h>

struct sorttmp {
    float radius;
    unsigned int index;
};

struct head {
    // colors less than radius away from vantage_point color will have best match in candidates
    f_pixel vantage_point;
    float radius;
    unsigned int num_candidates;
    f_pixel *candidates_color;
    unsigned short *candidates_index;
};

struct nearest_map {
    const colormap *map;
    float nearest_other_color_dist[256];
    mempool mempool;
    struct head heads[];
};

static float distance_from_nearest_other_color(const colormap *map, const unsigned int i)
{
    float second_best=MAX_DIFF;
    for(unsigned int j=0; j < map->colors; j++) {
        if (i == j) continue;
        float diff = colordifference(map->palette[i].acolor, map->palette[j].acolor);
        if (diff <= second_best) {
            second_best = diff;
        }
    }
    return second_best;
}

static int compareradius(const void *ap, const void *bp)
{
    float a = ((const struct sorttmp*)ap)->radius;
    float b = ((const struct sorttmp*)bp)->radius;
    return a > b ? 1 : (a < b ? -1 : 0);
}

static struct head build_head(f_pixel px, const colormap *map, unsigned int num_candidates, mempool *m, float error_margin, bool skip_index[], unsigned int *skipped)
{
    struct sorttmp colors[map->colors];
    unsigned int colorsused=0;

    for(unsigned int i=0; i < map->colors; i++) {
        if (skip_index[i]) continue; // colors in skip_index have been eliminated already in previous heads
        colors[colorsused].index = i;
        colors[colorsused].radius = colordifference(px, map->palette[i].acolor);
        colorsused++;
    }

    qsort(&colors, colorsused, sizeof(colors[0]), compareradius);
    assert(colorsused < 2 || colors[0].radius <= colors[1].radius); // closest first

    num_candidates = MIN(colorsused, num_candidates);

    struct head h = {
        .candidates_color = mempool_alloc(m, num_candidates * sizeof(h.candidates_color[0]), 0),
        .candidates_index = mempool_alloc(m, num_candidates * sizeof(h.candidates_index[0]), 0),
        .vantage_point = px,
        .num_candidates = num_candidates,
    };
    for(unsigned int i=0; i < num_candidates; i++) {
        h.candidates_color[i] = map->palette[colors[i].index].acolor;
        h.candidates_index[i] = colors[i].index;
    }
    // if all colors within this radius are included in candidates, then there cannot be any other better match
    // farther away from the vantage point than half of the radius. Due to alpha channel must assume pessimistic radius.
    h.radius = min_colordifference(px, h.candidates_color[num_candidates-1])/4.0f; // /4 = half of radius, but radius is squared

    for(unsigned int i=0; i < num_candidates; i++) {
        // divide again as that's matching certain subset within radius-limited subset
        // - 1/256 is a tolerance for miscalculation (seems like colordifference isn't exact)
        if (colors[i].radius < h.radius/4.f - error_margin) {
            skip_index[colors[i].index]=true;
            (*skipped)++;
        }
    }
    return h;
}

static colormap *get_subset_palette(const colormap *map)
{
    if (map->subset_palette) {
        return map->subset_palette;
    }

    unsigned int subset_size = (map->colors+3)/4;
    colormap *subset_palette = pam_colormap(subset_size, map->malloc, map->free);

    for(unsigned int i=0; i < subset_size; i++) {
        subset_palette->palette[i] = map->palette[i];
    }

    return subset_palette;
}

LIQ_PRIVATE struct nearest_map *nearest_init(const colormap *map, bool fast)
{
    colormap *subset_palette = get_subset_palette(map);
    const unsigned int num_vantage_points = map->colors > 16 ? MIN(map->colors/(fast ? 4 : 3), subset_palette->colors) : 0;
    const unsigned long heads_size = sizeof(struct head) * (num_vantage_points+1); // +1 is fallback head

    const unsigned long mempool_size = (sizeof(f_pixel) + sizeof(unsigned int)) * subset_palette->colors * map->colors/5 + (1<<14);
    mempool m = NULL;
    struct nearest_map *centroids = mempool_create(&m, sizeof(*centroids) + heads_size /* heads array is appended to it */, mempool_size, map->malloc, map->free);
    centroids->mempool = m;

    for(unsigned int i=0; i < map->colors; i++) {
        const float dist = distance_from_nearest_other_color(map,i);
        centroids->nearest_other_color_dist[i] = dist / 4.f; // half of squared distance
    }

    centroids->map = map;

    unsigned int skipped=0;
    assert(map->colors > 0);
    bool skip_index[map->colors]; for(unsigned int j=0; j < map->colors; j++) skip_index[j]=false;

    // floats and colordifference calculations are not perfect
    const float error_margin = fast ? 0 : 8.f/256.f/256.f;
    unsigned int h=0;
    for(; h < num_vantage_points; h++) {
        unsigned int num_candiadtes = 1+(map->colors - skipped)/((1+num_vantage_points-h)/2);

        centroids->heads[h] = build_head(subset_palette->palette[h].acolor, map, num_candiadtes, &centroids->mempool, error_margin, skip_index, &skipped);
        if (centroids->heads[h].num_candidates == 0) {
            break;
        }
    }

    // assumption that there is no better color within radius of vantage point color
    // holds true only for colors within convex hull formed by palette colors.
    // The fallback must contain all colors, since there are too many edge cases to cover.
    if (!fast) for(unsigned int j=0; j < map->colors; j++) {
        skip_index[j] = false;
    }

    centroids->heads[h] = build_head((f_pixel){0,0,0,0}, map, map->colors, &centroids->mempool, error_margin, skip_index, &skipped);
    centroids->heads[h].radius = MAX_DIFF;

    // get_subset_palette could have created a copy
    if (subset_palette != map->subset_palette) {
        pam_freecolormap(subset_palette);
    }

    return centroids;
}

LIQ_PRIVATE unsigned int nearest_search(const struct nearest_map *centroids, const f_pixel px, int likely_colormap_index, const float min_opaque_val, float *diff)
{
    const bool iebug = px.a > min_opaque_val;

    const struct head *const heads = centroids->heads;

    assert(likely_colormap_index < centroids->map->colors);
    const float guess_diff = colordifference(centroids->map->palette[likely_colormap_index].acolor, px);
    if (guess_diff < centroids->nearest_other_color_dist[likely_colormap_index]) {
        if (diff) *diff = guess_diff;
        return likely_colormap_index;
    }

    for(unsigned int i=0; /* last head will always be selected */ ; i++) {
        float vantage_point_dist = colordifference(px, heads[i].vantage_point);

        if (vantage_point_dist <= heads[i].radius) {
            assert(heads[i].num_candidates);
            unsigned int ind=0;
            float dist = colordifference(px, heads[i].candidates_color[0]);

            /* penalty for making holes in IE */
            if (iebug && heads[i].candidates_color[0].a < 1) {
                dist += 1.f/1024.f;
            }

            for(unsigned int j=1; j < heads[i].num_candidates; j++) {
                float newdist = colordifference(px, heads[i].candidates_color[j]);

                /* penalty for making holes in IE */
                if (iebug && heads[i].candidates_color[j].a < 1) {
                    newdist += 1.f/1024.f;
                }

                if (newdist < dist) {
                    dist = newdist;
                    ind = j;
                }
            }
            if (diff) *diff = dist;
            return heads[i].candidates_index[ind];
        }
    }
}

LIQ_PRIVATE void nearest_free(struct nearest_map *centroids)
{
    mempool_destroy(centroids->mempool);
}
