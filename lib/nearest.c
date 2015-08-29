/*
© 2011-2015 by Kornel Lesiński.

This file is part of libimagequant.

libimagequant is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

libimagequant is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with libimagequant. If not, see <http://www.gnu.org/licenses/>.
*/

#include "libimagequant.h"
#include "pam.h"
#include "nearest.h"
#include "mempool.h"
#include <stdlib.h>

typedef struct vp_sort_tmp {
    float distance_squared;
    unsigned int idx;
} vp_sort_tmp;

typedef struct vp_search_tmp {
    float distance;
    unsigned int idx;
    int exclude;
} vp_search_tmp;

typedef struct vp_node {
    struct vp_node *near, *far;
    f_pixel vantage_point;
    float radius;
    unsigned int idx;
} vp_node;

struct nearest_map {
    vp_node *root;
    const colormap_item *palette;
    float nearest_other_color_dist[256];
    mempool mempool;
};

static void vp_search_node(const vp_node *node, const f_pixel *const needle, vp_search_tmp *const best_candidate);

static int vp_compare_distance(const void *ap, const void *bp) {
    float a = ((const vp_sort_tmp*)ap)->distance_squared;
    float b = ((const vp_sort_tmp*)bp)->distance_squared;
    return a > b ? 1 : -1;
}

static void vp_sort_indexes_by_distance(const f_pixel vantage_point, vp_sort_tmp *indexes, int num_indexes, const colormap_item items[]) {
    for(int i=0; i < num_indexes; i++) {
        indexes[i].distance_squared = colordifference(vantage_point, items[indexes[i].idx].acolor);
    }
    qsort(indexes, num_indexes, sizeof(indexes[0]), vp_compare_distance);
}

/*
 * Usually it should pick farthest point, but picking most popular point seems to make search quicker anyway
 */
static int vp_find_best_vantage_point_index(vp_sort_tmp *indexes, int num_indexes, const colormap_item items[]) {
    int best = 0;
    float best_popularity = items[indexes[0].idx].popularity;
    for(int i = 1; i < num_indexes; i++) {
        if (items[indexes[i].idx].popularity > best_popularity) {
            best_popularity = items[indexes[i].idx].popularity;
            best = i;
        }
    }
    return best;
}

static vp_node *vp_create_node(mempool *m, vp_sort_tmp *indexes, int num_indexes, const colormap_item items[]) {
    if (num_indexes <= 0) {
        return NULL;
    }

    vp_node *node = mempool_alloc(m, sizeof(node[0]), 0);

    if (num_indexes == 1) {
        *node = (vp_node){
            .vantage_point = items[indexes[0].idx].acolor,
            .idx = indexes[0].idx,
            .radius = MAX_DIFF,
        };
        return node;
    }

    const int ref = vp_find_best_vantage_point_index(indexes, num_indexes, items);
    const int ref_idx = indexes[ref].idx;

    // Removes the `ref_idx` item from remaining items, because it's included in the current node
    num_indexes -= 1;
    indexes[ref] = indexes[num_indexes];

    vp_sort_indexes_by_distance(items[ref_idx].acolor, indexes, num_indexes, items);

    // Remaining items are split by the median distance
    const int half_idx = num_indexes/2;

    *node = (vp_node){
        .vantage_point = items[ref_idx].acolor,
        .idx = ref_idx,
        .radius = sqrtf(indexes[half_idx].distance_squared),
    };
    node->near = vp_create_node(m, indexes, half_idx, items);
    node->far = vp_create_node(m, &indexes[half_idx], num_indexes - half_idx, items);

    return node;
}

LIQ_PRIVATE struct nearest_map *nearest_init(const colormap *map, const bool fast) {
    mempool m = NULL;
    struct nearest_map *handle = mempool_create(&m, sizeof(handle[0]), sizeof(handle[0]) + sizeof(vp_node)*map->colors+16, map->malloc, map->free);

    vp_sort_tmp indexes[map->colors];

    for(unsigned int i=0; i < map->colors; i++) {
        indexes[i].idx = i;
    }

    vp_node *root = vp_create_node(&m, indexes, map->colors, map->palette);
    *handle = (struct nearest_map){
        .root = root,
        .palette = map->palette,
        .mempool = m,
    };

    for(unsigned int i=0; i < map->colors; i++) {
        vp_search_tmp best = {
            .distance = MAX_DIFF,
            .exclude = i,
        };
        vp_search_node(root, &map->palette[i].acolor, &best);
        handle->nearest_other_color_dist[i] = best.distance * best.distance / 4.0; // half of squared distance
    }

    return handle;
}

static void vp_search_node(const vp_node *node, const f_pixel *const needle, vp_search_tmp *const best_candidate) {
    do {
        const float distance = sqrtf(colordifference(node->vantage_point, *needle));

        if (distance < best_candidate->distance && best_candidate->exclude != node->idx) {
            best_candidate->distance = distance;
            best_candidate->idx = node->idx;
        }

        // Recurse towards most likely candidate first to narrow best candidate's distance as soon as possible
        if (distance < node->radius) {
            if (node->near) {
                vp_search_node(node->near, needle, best_candidate);
            }
            // The best node (final answer) may be just ouside the radius, but not farther than
            // the best distance we know so far. The vp_search_node above should have narrowed
            // best_candidate->distance, so this path is rarely taken.
            if (node->far && distance >= node->radius - best_candidate->distance) {
                node = node->far; // Fast tail recursion
            } else {
                break;
            }
        } else {
            if (node->far) {
                vp_search_node(node->far, needle, best_candidate);
            }
            if (node->near && distance <= node->radius + best_candidate->distance) {
                node = node->near; // Fast tail recursion
            } else {
                break;
            }
        }
    } while(true);
}

LIQ_PRIVATE unsigned int nearest_search(const struct nearest_map *handle, const f_pixel *px, const int likely_colormap_index, float *diff) {
    const float guess_diff = colordifference(handle->palette[likely_colormap_index].acolor, *px);
    if (guess_diff < handle->nearest_other_color_dist[likely_colormap_index]) {
        if (diff) *diff = guess_diff;
        return likely_colormap_index;
    }

    vp_search_tmp best_candidate = {
        .distance = sqrtf(guess_diff),
        .idx = likely_colormap_index,
        .exclude = -1,
    };
    vp_search_node(handle->root, px, &best_candidate);
    if (diff) {
        *diff = best_candidate.distance * best_candidate.distance;
    }
    return best_candidate.idx;
}

LIQ_PRIVATE void nearest_free(struct nearest_map *centroids)
{
    mempool_destroy(centroids->mempool);
}
