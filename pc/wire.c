/*
 * wire.c — Auto-wiring implementation
 *
 * v3 proximity + v6 gateway management + Hebbian correlation.
 *
 * Isaac Oravec & Claude, February 2026
 */

#include "wire.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* Yee API — declared in yee.cuh but we can't include it from C.
 * These are extern "C" functions in yee.cu. */
typedef struct { int voxel_id; float amplitude; float strength; } YeeSource;
#define YEE_GX 64
#define YEE_GY 64
#define YEE_GZ 64
#define YEE_N  (YEE_GX * YEE_GY * YEE_GZ)
#define YEE_MAX_SOURCES 512
int yee_inject(const YeeSource *sources, int n_sources);
int yee_download_acc(uint8_t *h_substrate, int n);

void wire_local_3d(CubeState *cubes, int n_cubes) {
    /* Wire each position to its 6 face-neighbors within same cube.
     * This is the v3 spatial proximity principle in 3D. */
    int dx[] = {1, -1, 0, 0, 0, 0};
    int dy[] = {0, 0, 1, -1, 0, 0};
    int dz[] = {0, 0, 0, 0, 1, -1};

    for (int c = 0; c < n_cubes; c++) {
        CubeState *cube = &cubes[c];
        for (int pos = 0; pos < CUBE_SIZE; pos++) {
            int lx, ly, lz;
            local_coords(pos, &lx, &ly, &lz);

            uint64_t rd = 0;
            for (int d = 0; d < 6; d++) {
                int nx = lx + dx[d], ny = ly + dy[d], nz = lz + dz[d];
                if (nx >= 0 && nx < CUBE_DIM && ny >= 0 && ny < CUBE_DIM && nz >= 0 && nz < CUBE_DIM) {
                    int nidx = local_idx(nx, ny, nz);
                    rd |= (1ULL << nidx);
                }
            }
            cube->reads[pos] = rd;
            cube->active[pos] = (rd != 0) ? 1 : 0;
        }
    }
}

/* wire_gateways, wire_hebbian_from_gpu, wire_engine_to_gpu, wire_gpu_to_engine
 * removed in v0.14-yee-persist — dead code after Yee substrate swap. */

/* ══════════════════════════════════════════════════════════════
 * YEE SUBSTRATE WIRING — Wave physics replaces CA
 * ══════════════════════════════════════════════════════════════ */

static inline int yee_voxel(int gx, int gy, int gz) {
    return gx + gy * YEE_GX + gz * YEE_GX * YEE_GY;
}

int wire_engine_to_yee(const Engine *eng) {
    /* Create YeeSource for each active node.
     * Identity presence → base injection (0.15). Val modulates on top.
     * Valence floors at 0.1 so cold nodes still participate.
     * Old substrate used identity fingerprint to seed — this matches
     * by ensuring every node with identity injects at a base level. */
    const Graph *g0 = &eng->shells[0].g;
    YeeSource sources[YEE_MAX_SOURCES];
    int n_src = 0;

    for (int i = 0; i < g0->n_nodes && n_src < YEE_MAX_SOURCES; i++) {
        const Node *n = &g0->nodes[i];
        if (!n->alive || n->layer_zero) continue;
        if (n->identity.len < 1) continue;

        int gx = coord_x(n->coord) % YEE_GX;
        int gy = coord_y(n->coord) % YEE_GY;
        int gz = coord_z(n->coord) % YEE_GZ;

        /* Identity-based injection: matches old substrate behavior.
         * Old substrate stamped identity hash (50-200) at full strength
         * regardless of valence. Yee equivalent: base 0.5 always present
         * for nodes with identity, val modulates on top. Strength 1.0
         * (spatial presence doesn't depend on valence — that deadlocked). */
        float base = (n->identity.len >= 64) ? 0.5f : 0.0f;
        float amp = base + fabsf((float)n->val / (float)VAL_CEILING) * 0.5f;
        float strength = 1.0f;

        sources[n_src].voxel_id = yee_voxel(gx, gy, gz);
        sources[n_src].amplitude = amp;
        sources[n_src].strength = strength;
        n_src++;
    }

    if (n_src > 0)
        return yee_inject(sources, n_src);
    return 0;
}

int wire_yee_to_engine(Engine *eng) {
    /* Download Yee accumulator as uint8_t (0-255), same as old substrate[].
     * Read at each node's voxel position. Includes Hebbian feedback
     * (replaces wire_hebbian_from_gpu): wave activity → valence++ and
     * strengthen incoming edges. Uses dst-indexed lookup: O(V+E). */
    uint8_t *yee_sub = (uint8_t *)calloc(YEE_N, 1);
    if (!yee_sub) return -1;

    if (yee_download_acc(yee_sub, YEE_N) != 0) {
        free(yee_sub);
        return -1;
    }

    Graph *g0 = &eng->shells[0].g;

    /* Build dst adjacency index: O(V+E) Hebbian edge strengthening.
     * Same pattern as wire_hebbian_from_gpu. */
    int *dst_off = (int *)calloc(g0->n_nodes + 1, sizeof(int));
    int *dst_idx = (int *)malloc(g0->n_edges * sizeof(int));

    for (int e = 0; e < g0->n_edges; e++) {
        int d = g0->edges[e].dst;
        if (d < g0->n_nodes) dst_off[d + 1]++;
    }
    for (int i = 1; i <= g0->n_nodes; i++)
        dst_off[i] += dst_off[i - 1];
    int *cursor = (int *)calloc(g0->n_nodes, sizeof(int));
    for (int e = 0; e < g0->n_edges; e++) {
        int d = g0->edges[e].dst;
        if (d < g0->n_nodes) {
            dst_idx[dst_off[d] + cursor[d]] = e;
            cursor[d]++;
        }
    }
    free(cursor);

    for (int i = 0; i < g0->n_nodes; i++) {
        Node *n = &g0->nodes[i];
        if (!n->alive || n->layer_zero) continue;

        int gx = coord_x(n->coord) % YEE_GX;
        int gy = coord_y(n->coord) % YEE_GY;
        int gz = coord_z(n->coord) % YEE_GZ;
        int vid = yee_voxel(gx, gy, gz);

        int32_t gpu_signal = (int32_t)yee_sub[vid];
        n->accum += gpu_signal;
        if (gpu_signal > 0) n->n_incoming++;

        /* Hebbian: wave activity → strengthen incoming edges + valence++ */
        if (gpu_signal > 0) {
            if (n->valence < 255) n->valence++;
            for (int j = dst_off[i]; j < dst_off[i + 1]; j++) {
                int e = dst_idx[j];
                if (g0->edges[e].weight < 255)
                    g0->edges[e].weight++;
            }
        }
    }

    free(dst_off);
    free(dst_idx);
    free(yee_sub);
    return 0;
}

int wire_yee_retinas(Engine *eng, uint8_t *yee_substrate) {
    /* Gather each child's parent cube voxels into a contiguous buffer.
     * The Yee flat array is 64x64x64 row-major — a 4x4x4 cube's voxels
     * are scattered (stride 64 in Y, 4096 in Z). We gather into
     * retina_bufs[c][0..63] so child retina sees 64 contiguous bytes. */
    static uint8_t retina_bufs[MAX_CHILDREN][CUBE_SIZE];

    for (int c = 0; c < MAX_CHILDREN; c++) {
        if (eng->child_owner[c] < 0) continue;
        int owner_id = eng->child_owner[c];
        if (owner_id >= eng->shells[0].g.n_nodes) continue;
        Node *owner = &eng->shells[0].g.nodes[owner_id];

        int gx = coord_x(owner->coord) % YEE_GX;
        int gy = coord_y(owner->coord) % YEE_GY;
        int gz = coord_z(owner->coord) % YEE_GZ;

        int cx = gx / CUBE_DIM, cy = gy / CUBE_DIM, cz = gz / CUBE_DIM;
        int bx = cx * CUBE_DIM, by = cy * CUBE_DIM, bz = cz * CUBE_DIM;

        /* Gather 4x4x4 voxels from flat Yee array into contiguous buffer */
        int lp = 0;
        for (int lz = 0; lz < CUBE_DIM; lz++)
            for (int ly = 0; ly < CUBE_DIM; ly++)
                for (int lx = 0; lx < CUBE_DIM; lx++) {
                    int vid = (bx+lx) + (by+ly)*YEE_GX + (bz+lz)*YEE_GX*YEE_GY;
                    retina_bufs[c][lp++] = yee_substrate[vid];
                }

        eng->child_pool[c].retina = retina_bufs[c];
        eng->child_pool[c].retina_len = CUBE_SIZE;
    }
    return 0;
}
