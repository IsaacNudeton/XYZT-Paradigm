/*
 * reporter.c — Local output reporter
 *
 * Reads engine state, produces human-readable text.
 * No AI, no API. Just printf from engine state.
 *
 * Isaac Oravec & Claude, February 2026
 */

#include "engine.h"
#include "substrate.cuh"
#include <stdio.h>

void report_shells(const Engine *eng) {
    printf("=== SHELL STATUS ===\n");
    for (int s = 0; s < eng->n_shells; s++) {
        const Graph *g = &eng->shells[s].g;
        int active = 0, crystallized = 0;
        for (int i = 0; i < g->n_nodes; i++) {
            if (g->nodes[i].alive && !g->nodes[i].layer_zero) active++;
            if (g->nodes[i].valence >= 200) crystallized++;
        }
        int live_edges = 0;
        for (int e = 0; e < g->n_edges; e++)
            if (g->edges[e].weight > PRUNE_FLOOR) live_edges++;

        printf("  [%d] %s: %d/%d nodes active, %d crystallized, %d/%d edges live\n",
               s, eng->shells[s].name, active, g->n_nodes,
               crystallized, live_edges, g->n_edges);
    }
}

void report_onetwo(const Engine *eng) {
    printf("=== ONETWO FEEDBACK ===\n");
    const OneTwoSystem *ot = &eng->onetwo;
    printf("  density=%d  symmetry=%d  period=%d\n",
           ot->analysis.density, ot->analysis.symmetry, ot->analysis.period);
    printf("  match=%d  diverge=%d  consensus=%d  mismatch=%d\n",
           ot->feedback[0], ot->feedback[1], ot->feedback[2], ot->feedback[3]);
    printf("  stable=%d  energy=%d  learning=%d  error=%d\n",
           ot->feedback[4], ot->feedback[5], ot->feedback[6], ot->feedback[7]);
    printf("  ticks=%d  inputs=%d  low_error_run=%d\n",
           ot->tick_count, ot->total_inputs, eng->low_error_run);
}

void report_nesting(const Engine *eng) {
    if (eng->n_children == 0) return;
    printf("=== NESTED SYSTEMS ===\n");
    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (eng->child_owner[i] < 0) continue;
        const Graph *child = &eng->child_pool[i];
        const Node *owner = &eng->shells[0].g.nodes[eng->child_owner[i]];
        int out_idx = child->n_nodes - 1;
        int32_t out_val = (out_idx >= 0) ? child->nodes[out_idx].val : 0;

        printf("  child[%d] owned by '%s': %d nodes, %d edges, %llu ticks, out=%d",
               i, owner->name, child->n_nodes, child->n_edges,
               (unsigned long long)child->total_ticks, out_val);

        /* Show retina octant values if connected */
        if (child->retina && child->retina_len >= 64) {
            printf(" retina=[");
            for (int r = 0; r < 8 && r < child->n_nodes; r++) {
                printf("%d", child->nodes[r].val);
                if (r < 7) printf(",");
            }
            printf("]");
        } else {
            printf(" (no retina)");
        }
        printf("\n");
    }
}

void report_top_nodes(const Engine *eng, int shell, int top_n) {
    const Graph *g = &eng->shells[shell].g;
    printf("=== TOP %d NODES (shell %d: %s) ===\n", top_n, shell, eng->shells[shell].name);

    /* Find top-N by resonance (val + valence) */
    typedef struct { int id; int score; } Entry;
    Entry *top = (Entry *)calloc(top_n, sizeof(Entry));
    int n_top = 0;

    for (int i = 0; i < g->n_nodes; i++) {
        const Node *n = &g->nodes[i];
        if (!n->alive || n->layer_zero) continue;
        int score = abs(n->val) + n->valence * 10;
        if (n_top < top_n) {
            top[n_top].id = i; top[n_top].score = score; n_top++;
        } else {
            int min_k = 0;
            for (int k = 1; k < top_n; k++)
                if (top[k].score < top[min_k].score) min_k = k;
            if (score > top[min_k].score) {
                top[min_k].id = i; top[min_k].score = score;
            }
        }
    }

    for (int k = 0; k < n_top; k++) {
        const Node *n = &g->nodes[top[k].id];
        printf("  [%d] %-40s val=%d val=%d crystal=%d coord=(%d,%d,%d)\n",
               top[k].id, n->name, n->val, n->valence,
               crystal_strength(n),
               coord_x(n->coord), coord_y(n->coord), coord_z(n->coord));
    }
    free(top);
}

void report_gpu_substrate(const CubeState *cubes, int n_cubes) {
    printf("=== GPU SUBSTRATE ===\n");
    int total_alive = 0, total_active = 0;
    uint64_t total_co = 0;
    for (int c = 0; c < n_cubes; c++) {
        for (int p = 0; p < CUBE_SIZE; p++) {
            if (cubes[c].substrate[p] >= SUB_ALIVE) total_alive++;
            if (cubes[c].active[p]) total_active++;
            total_co += xyzt_popcnt64(cubes[c].co_present[p]);
        }
    }
    int total_voxels = n_cubes * CUBE_SIZE;
    printf("  %d cubes, %d voxels\n", n_cubes, total_voxels);
    printf("  alive: %d/%d (%.1f%%)\n", total_alive, total_voxels,
           total_voxels > 0 ? 100.0 * total_alive / total_voxels : 0);
    printf("  active: %d  co-present bits: %llu\n", total_active, (unsigned long long)total_co);
}

void report_full(const Engine *eng, const CubeState *cubes, int n_cubes) {
    printf("\n");
    printf("ENGINE: %llu ticks, %d boundary edges\n",
           (unsigned long long)eng->total_ticks, eng->n_boundary_edges);
    report_shells(eng);
    report_onetwo(eng);
    report_nesting(eng);
    report_top_nodes(eng, 0, 5);
    if (cubes && n_cubes > 0)
        report_gpu_substrate(cubes, n_cubes);
    printf("\n");
}
