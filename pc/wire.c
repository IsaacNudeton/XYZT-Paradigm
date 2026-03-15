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
    /* Content-dependent spatial seeding: spread node's identity fingerprint
     * across its local cube as a spatial pattern, not a single-point blast.
     *
     * Each of the 64 positions in the cube gets a substrate value derived
     * from the node's identity bitstream. Different content → different
     * spatial pattern → different retina views for children.
     *
     * Capped at SEED_CAP to prevent saturation. SET, not additive —
     * only raises positions that are below the seeded value. */

    const Graph *g0 = &eng->shells[0].g;
    const int SEED_CAP = 96;  /* above SUB_ALIVE (64) so seeded positions cross threshold,
                                  well below 255 saturation — leaves room for spatial gradients */

    for (int i = 0; i < g0->n_nodes; i++) {
        const Node *n = &g0->nodes[i];
        if (!n->alive || n->layer_zero) continue;
        if (n->identity.len < 64) continue;  /* need fingerprint for spatial pattern */

        int x = coord_x(n->coord) % (VOL_X * CUBE_DIM);
        int y = coord_y(n->coord) % (VOL_Y * CUBE_DIM);
        int z = coord_z(n->coord) % (VOL_Z * CUBE_DIM);

        int cx = x / CUBE_DIM, cy = y / CUBE_DIM, cz = z / CUBE_DIM;
        int cube_id = cube_id_from(cx, cy, cz);
        if (cube_id < 0 || cube_id >= n_cubes) continue;

        CubeState *cube = &cubes[cube_id];

        /* Seed each position by hashing identity with position index.
         * Raw bit sampling fails because ONETWO fingerprints are sparse
         * (~20% density). Instead, hash(identity_word XOR position) gives
         * full-range values that are deterministic but content-dependent.
         * Different fingerprints → different spatial patterns. */
        for (int p = 0; p < CUBE_SIZE; p++) {
            /* Mix identity words with position to get a content-dependent seed */
            int word_idx = p % (n->identity.len / 64 + 1);
            uint64_t id_word = (word_idx < BS_WORDS) ? n->identity.w[word_idx] : 0;
            uint32_t mix = (uint32_t)(id_word ^ (id_word >> 32)) ^ (uint32_t)(p * 2654435761u);
            /* FNV-1a hash of the mix for good distribution */
            uint32_t h = 2166136261u;
            h ^= mix & 0xFF; h *= 16777619u;
            h ^= (mix >> 8) & 0xFF; h *= 16777619u;
            h ^= (mix >> 16) & 0xFF; h *= 16777619u;
            h ^= (mix >> 24) & 0xFF; h *= 16777619u;
            /* Map hash to seed range. Use top bits for better uniformity. */
            int seed = (int)((h >> 24) & 0xFF) * SEED_CAP / 255;

            /* REPLACE semantics: re-stamp the content pattern each sync.
             * This counteracts co-presence saturation — positions that
             * the fingerprint says should be cold get pulled back down. */
            cube->substrate[p] = (uint8_t)seed;

            /* Mark based on whether this position is above threshold */
            if (cube->substrate[p] >= SUB_ALIVE)
                cube->marked[p] = 1;
        }
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
     * Read at each node's voxel position. */
    uint8_t *yee_sub = (uint8_t *)calloc(YEE_N, 1);
    if (!yee_sub) return -1;

    if (yee_download_acc(yee_sub, YEE_N) != 0) {
        free(yee_sub);
        return -1;
    }

    Graph *g0 = &eng->shells[0].g;
    for (int i = 0; i < g0->n_nodes; i++) {
        Node *n = &g0->nodes[i];
        if (!n->alive || n->layer_zero) continue;

        int gx = coord_x(n->coord) % YEE_GX;
        int gy = coord_y(n->coord) % YEE_GY;
        int gz = coord_z(n->coord) % YEE_GZ;
        int vid = yee_voxel(gx, gy, gz);

        /* Substrate value at this node's position → accumulation signal.
         * Yee acc values are already calibrated (0-255). Use directly. */
        int32_t gpu_signal = (int32_t)yee_sub[vid];
        n->accum += gpu_signal;
        if (gpu_signal > 0) n->n_incoming++;

        /* Hebbian feedback: replaces wire_hebbian_from_gpu.
         * Wave activity at this position → strengthen incoming edges + boost valence.
         * Old substrate used co_present popcnt > 0 (any neighbor activity).
         * Yee equivalent: any wave energy at this position. */
        if (gpu_signal > 0) {
            if (n->valence < 255) n->valence++;
            /* Strengthen edges targeting this node */
            for (int e = 0; e < g0->n_edges; e++) {
                if (g0->edges[e].dst == i && g0->edges[e].weight < 255)
                    g0->edges[e].weight++;
            }
        }

    }

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
