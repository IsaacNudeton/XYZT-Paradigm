/*
 * wire.c — Auto-wiring implementation
 *
 * v3 proximity + v6 gateway management + Hebbian correlation.
 *
 * Isaac Oravec & Claude, February 2026
 */

#include "wire.h"
#include <stdio.h>

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

void wire_gateways(CubeState *cubes, int n_cubes) {
    /* Gateway positions: face positions at cube boundaries.
     * Each face of a cube has CUBE_DIM x CUBE_DIM = 16 gateway positions.
     *
     * Wiring convention:
     * - Face +X: positions where lx = CUBE_DIM-1 -> read from neighbor's lx=0
     * - Face -X: positions where lx = 0 -> read from neighbor's lx=CUBE_DIM-1
     * Similarly for Y and Z.
     *
     * Gateway signals propagate via the route kernel (substrate.cu). */

    /* For now, gateways are handled by the route kernel.
     * This function sets up the gateway positions' reads masks to include
     * a "gateway bit" (bit 63) that the route kernel uses as a signal
     * that this position participates in inter-cube communication. */

    for (int c = 0; c < n_cubes; c++) {
        CubeState *cube = &cubes[c];
        for (int pos = 0; pos < CUBE_SIZE; pos++) {
            int lx, ly, lz;
            local_coords(pos, &lx, &ly, &lz);

            /* Mark face positions with gateway bit */
            int is_face = (lx == 0 || lx == CUBE_DIM - 1 ||
                           ly == 0 || ly == CUBE_DIM - 1 ||
                           lz == 0 || lz == CUBE_DIM - 1);
            if (is_face) {
                /* Gateway positions also read from themselves (echo) */
                cube->reads[pos] |= (1ULL << pos);
            }
        }
    }
}

void wire_hebbian_from_gpu(Engine *eng, const CubeState *cubes, int n_cubes) {
    /* Correlate GPU expression patterns with engine graph topology.
     *
     * For each engine node that maps to a voxel:
     *   1. Read the voxel's co-presence result
     *   2. Count active neighbors -> strength signal
     *   3. Feed back into node's val accumulation
     *
     * This is the CPU-side feedback path from GPU substrate to learning graph.
     *
     * Uses dst-indexed edge lookup: O(nodes + edges) total
     * instead of O(nodes × edges). */

    Graph *g0 = &eng->shells[0].g;
    if (g0->n_nodes == 0 || g0->n_edges == 0) return;

    /* Build dst adjacency index: for each node, the edges targeting it.
     * dst_off[i] = start index in dst_idx for node i.
     * dst_off[i+1] - dst_off[i] = number of edges with dst==i. */
    int *dst_off = (int *)calloc(g0->n_nodes + 1, sizeof(int));
    int *dst_idx = (int *)malloc(g0->n_edges * sizeof(int));

    /* Count edges per dst node */
    for (int e = 0; e < g0->n_edges; e++) {
        int d = g0->edges[e].dst;
        if (d < g0->n_nodes) dst_off[d + 1]++;
    }
    /* Prefix sum */
    for (int i = 1; i <= g0->n_nodes; i++)
        dst_off[i] += dst_off[i - 1];
    /* Scatter edge indices into dst-ordered array */
    int *cursor = (int *)calloc(g0->n_nodes, sizeof(int));
    for (int e = 0; e < g0->n_edges; e++) {
        int d = g0->edges[e].dst;
        if (d < g0->n_nodes) {
            dst_idx[dst_off[d] + cursor[d]] = e;
            cursor[d]++;
        }
    }
    free(cursor);

    /* Main loop: map each node to its voxel, read GPU activity, strengthen edges */
    for (int i = 0; i < g0->n_nodes; i++) {
        Node *n = &g0->nodes[i];
        if (!n->alive || n->layer_zero) continue;

        /* Map node to voxel via coord */
        int x = coord_x(n->coord) % (VOL_X * CUBE_DIM);
        int y = coord_y(n->coord) % (VOL_Y * CUBE_DIM);
        int z = coord_z(n->coord) % (VOL_Z * CUBE_DIM);

        /* Cube and local position */
        int cx = x / CUBE_DIM, cy = y / CUBE_DIM, cz = z / CUBE_DIM;
        int cube_id = cube_id_from(cx, cy, cz);
        if (cube_id < 0 || cube_id >= n_cubes) continue;

        int lx = x % CUBE_DIM, ly = y % CUBE_DIM, lz = z % CUBE_DIM;
        int lpos = local_idx(lx, ly, lz);

        const CubeState *cube = &cubes[cube_id];
        uint64_t cp = cube->co_present[lpos];

        /* Count active co-present neighbors as a substrate signal */
        int activity = xyzt_popcnt64(cp);

        /* Feed GPU activity into node's learning */
        if (activity > 0) {
            /* Strengthen edges targeting this node — O(degree) via index */
            for (int j = dst_off[i]; j < dst_off[i + 1]; j++) {
                int e = dst_idx[j];
                if (g0->edges[e].weight < 255)
                    g0->edges[e].weight++;
            }
            /* Boost valence from GPU confirmation */
            if (n->valence < 255) n->valence++;
        }
    }

    free(dst_off);
    free(dst_idx);
}

void wire_engine_to_gpu(const Engine *eng, CubeState *cubes, int n_cubes) {
    /* Map engine node integer values to GPU substrate weights.
     * Each node injects its val as a substrate boost at its coord position. */

    const Graph *g0 = &eng->shells[0].g;

    for (int i = 0; i < g0->n_nodes; i++) {
        const Node *n = &g0->nodes[i];
        if (!n->alive || n->layer_zero) continue;
        if (n->val == 0) continue;

        int x = coord_x(n->coord) % (VOL_X * CUBE_DIM);
        int y = coord_y(n->coord) % (VOL_Y * CUBE_DIM);
        int z = coord_z(n->coord) % (VOL_Z * CUBE_DIM);

        int cx = x / CUBE_DIM, cy = y / CUBE_DIM, cz = z / CUBE_DIM;
        int cube_id = cube_id_from(cx, cy, cz);
        if (cube_id < 0 || cube_id >= n_cubes) continue;

        int lx = x % CUBE_DIM, ly = y % CUBE_DIM, lz = z % CUBE_DIM;
        int lpos = local_idx(lx, ly, lz);

        CubeState *cube = &cubes[cube_id];

        /* Inject: boost substrate by abs(val), mark if positive */
        int boost = abs(n->val);
        if (boost > 255) boost = 255;
        int nw = (int)cube->substrate[lpos] + boost;
        cube->substrate[lpos] = nw > 255 ? 255 : (uint8_t)nw;
        cube->marked[lpos] = (n->val > 0) ? 1 : 0;
    }
}

void wire_gpu_to_engine(Engine *eng, const CubeState *cubes, int n_cubes) {
    /* Read GPU co-presence results and accumulate into engine nodes.
     * The GPU processed spatial correlations; now feed back to learning. */

    Graph *g0 = &eng->shells[0].g;

    for (int i = 0; i < g0->n_nodes; i++) {
        Node *n = &g0->nodes[i];
        if (!n->alive || n->layer_zero) continue;

        int x = coord_x(n->coord) % (VOL_X * CUBE_DIM);
        int y = coord_y(n->coord) % (VOL_Y * CUBE_DIM);
        int z = coord_z(n->coord) % (VOL_Z * CUBE_DIM);

        int cx = x / CUBE_DIM, cy = y / CUBE_DIM, cz = z / CUBE_DIM;
        int cube_id = cube_id_from(cx, cy, cz);
        if (cube_id < 0 || cube_id >= n_cubes) continue;

        int lx = x % CUBE_DIM, ly = y % CUBE_DIM, lz = z % CUBE_DIM;
        int lpos = local_idx(lx, ly, lz);

        const CubeState *cube = &cubes[cube_id];
        uint64_t cp = cube->co_present[lpos];

        /* GPU co-presence count becomes accumulation signal */
        int32_t gpu_signal = xyzt_popcnt64(cp);
        n->accum += gpu_signal;
        if (gpu_signal > 0) n->n_incoming++;
    }
}
