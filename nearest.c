
#include "pam.h"
#include "nearest.h"

struct color_entry {
    f_pixel color;
    float radius;
    int index;
};

struct sorttmp {
    float radius;
    int index;
};

struct head {
    f_pixel center;
    float radius;
    int num_candidates;
    struct color_entry candidates[256];
};

struct nearest_map {
    struct head heads[256];
    int num_heads;
};

static int compareradius(const void *ap, const void *bp)
{
    float a = ((struct sorttmp*)ap)->radius;
    float b = ((struct sorttmp*)bp)->radius;
    return a > b ? 1 : (a < b ? -1 : 0);
}

static struct head build_head(f_pixel px, const colormap *map, int num_candidates, int skip_index[], int *skipped)
{
    struct sorttmp colors[map->colors];
    int colorsused=0;

    for(int i=0; i < map->colors; i++) {
        if (skip_index[i]) continue;
        colors[colorsused].index = i;
        colors[colorsused].radius = colordifference(px, map->palette[i].acolor);
        colorsused++;
    }

    qsort(&colors, colorsused, sizeof(colors[0]), compareradius);
    assert(colorsused < 2 || colors[0].radius <= colors[1].radius);

    struct head h;
    h.center = px;
    num_candidates = MIN(colorsused, num_candidates);
    h.num_candidates = num_candidates;
    for(int i=0; i < num_candidates; i++) {
        h.candidates[i] = (struct color_entry) {
            .color = map->palette[colors[i].index].acolor,
            .index = colors[i].index,
            .radius = colors[i].radius,
        };
    }
    h.radius = colors[num_candidates-1].radius/4.0f; // /2 squared

    for(int i=0; i < num_candidates; i++) {

        assert(colors[i].radius <= h.radius*4.0f);
        // divide again as that's matching certain subset within radius-limited subset
        // - 1/256 is a tolerance for miscalculation (seems like colordifference isn't exact)
        if (colors[i].radius < h.radius/4.f - 1.f/256.f) {
            skip_index[colors[i].index]=1;
            (*skipped)++;
        }
    }
    return h;
}


struct nearest_map *nearest_init(const colormap *map)
{
    struct nearest_map *centroids = calloc(1, sizeof(struct nearest_map));
    int skipped=0;
    int skip_index[map->colors]; for(int j=0; j<map->colors;j++) skip_index[j]=0;

    int max_heads = map->subset_palette->colors;
    int h=0;
    for(; h < max_heads; h++)
    {
        int num_candiadtes = 1+(map->colors - skipped)/((1+max_heads-h)/2);

        int idx = best_color_index(map->subset_palette->palette[h].acolor, map, 1.0, NULL);

        centroids->heads[h] = build_head(map->palette[idx].acolor, map, num_candiadtes, skip_index, &skipped);
        if (centroids->heads[h].num_candidates == 0) {
            break;
        }
    }

    centroids->heads[h].radius = 9999999;
    centroids->heads[h].center = (f_pixel){0,0,0,0};
    centroids->heads[h].num_candidates = 0;
    for (int i=0; i < map->colors; i++) {
        if (skip_index[i]) continue;

        centroids->heads[h].candidates[centroids->heads[h].num_candidates++] = (struct color_entry) {
            .color = map->palette[i].acolor,
            .index = i,
            .radius = 999,
        };
    }
    centroids->num_heads = ++h;

    return centroids;
}

int nearest_search(struct nearest_map *centroids, f_pixel px, float *diff)
{
    const struct head *const heads = centroids->heads;
    for(int i=0; i < centroids->num_heads; i++) {
        float headdist = colordifference(px, heads[i].center);

        if (headdist <= heads[i].radius) {
            assert(heads[i].num_candidates);
            int ind=heads[i].candidates[0].index;
            float dist = colordifference(px, heads[i].candidates[0].color);
            for (int j=1; j < heads[i].num_candidates; j++) {
                float newdist = colordifference(px, heads[i].candidates[j].color);
                if (newdist < dist) {
                    dist = newdist;
                    ind = heads[i].candidates[j].index;
                }
            }
            if (diff) *diff = dist;
            return ind;
        }
    }
    assert(0);
    return 0;
}
