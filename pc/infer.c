/*
 * infer.c — XYZT Inference Layer (Forward Pass)
 *
 * The forward pass is NOT matrix multiplication.
 * It's a voltage spike propagating through a carved impedance field.
 *
 * 1. LOAD: L-field is already in GPU memory (loaded from .xyzt)
 * 2. EXCITE: Convert query to raw bytes, hash to 3D coordinates,
 *    inject voltage spike at those coordinates
 * 3. RING: Run yee_tick() for N heartbeats. Waves propagate through
 *    low-L corridors, reflect off high-L walls, interfere at junctions.
 * 4. READ: Download accumulator, find which nodes have highest energy.
 *    Those nodes are the answer — the query resonated through the
 *    carved waveguides to reach structurally similar knowledge.
 *
 * Isaac Oravec & Claude, March 2026
 */

#include "infer.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

/* YeeSource — matches yee.cuh (can't include .cuh from pure C) */
typedef struct { int voxel_id; float amplitude; float strength; } YeeSource;

/* Yee GPU functions — pure C externs */
extern int yee_tick(void);
extern int yee_inject(const YeeSource *sources, int n_sources);
extern int yee_download_V(float *h_V, int n);
extern int yee_download_acc_raw(float *h_acc, int n);
extern int yee_clear_fields(void);
extern int yee_apply_sponge(int width, float rate);

#define YEE_GX 64
#define YEE_GY 64
#define YEE_GZ 64
#define YEE_TOTAL (YEE_GX * YEE_GY * YEE_GZ)

static int yee_vid(int gx, int gy, int gz) {
    return gx + gy * YEE_GX + gz * YEE_GX * YEE_GY;
}

/* ══════════════════════════════════════════════════════════════
 * FORWARD PASS
 * ══════════════════════════════════════════════════════════════ */

static int infer_forward(Engine *eng, const uint8_t *data, int len,
                          InferResult *results, int max_results) {
    Graph *g0 = &eng->shells[0].g;
    if (g0->n_nodes == 0) return 0;
    if (max_results > INFER_MAX_RESULTS) max_results = INFER_MAX_RESULTS;

    /* ── Step 1: Clear dynamic fields (V, I, acc) but keep L-field ── */
    yee_clear_fields();

    /* ── Step 2: EXCITE — inject query as voltage spike ──
     * 3-tier semantic coords: X=type(bytes 0-3), Y=sub-type(4-7), Z=instance(8-11).
     * Same hash as engine_ingest. Query lands where same-type nodes live. */
    BitStream qbs;
    bs_init(&qbs);
    {
        int max = BS_MAXBITS / 8;
        int elen = len < max ? len : max;
        for (int i = 0; i < elen; i++)
            for (int b = 0; b < 8; b++)
                bs_push(&qbs, (data[i] >> b) & 1);
    }
    uint8_t b0[4] = {0}, b1[4] = {0}, b2[4] = {0};
    for (int i = 0; i < 4; i++) {
        for (int b = 0; b < 8; b++) {
            if (i * 8 + b < qbs.len && bs_get(&qbs, i * 8 + b))
                b0[i] |= (1 << b);
            if ((i + 4) * 8 + b < qbs.len && bs_get(&qbs, (i + 4) * 8 + b))
                b1[i] |= (1 << b);
            if ((i + 8) * 8 + b < qbs.len && bs_get(&qbs, (i + 8) * 8 + b))
                b2[i] |= (1 << b);
        }
    }
    uint32_t hx = hash32(b0, 4);
    uint32_t hy = hash32(b1, 4);
    uint32_t hz = hash32(b2, 4);
    int qx = hx % YEE_GX;
    int qy = hy % YEE_GY;
    int qz = hz % YEE_GZ;

    YeeSource src;
    src.voxel_id = yee_vid(qx, qy, qz);
    src.amplitude = 1.0f;   /* full-strength pulse */
    src.strength = 1.0f;

    /* ── Step 3: RING — propagate with absorbing boundaries ──
     * Sponge layer (8 cells deep, 0.3 damping rate) absorbs outgoing waves.
     * Without it, the grid is a torus — energy fills uniformly.
     * With it, only energy trapped in carved waveguides persists.
     *
     * Drive the query for 15 ticks (short pulse), then let it ring.
     * Read the EARLY accumulator (tick 25) for sharp spatial locality,
     * then continue to full ring for resonant cavity detection. */
    #define SPONGE_WIDTH  4
    #define SPONGE_RATE   0.15f
    #define DRIVE_TICKS   30
    #define EARLY_READ    40

    float *h_acc_early = (float *)malloc(YEE_TOTAL * sizeof(float));

    for (int t = 0; t < INFER_RING_TICKS; t++) {
        if (t < DRIVE_TICKS)
            yee_inject(&src, 1);
        yee_tick();
        yee_apply_sponge(SPONGE_WIDTH, SPONGE_RATE);

        /* Early snapshot — sharpest spatial locality */
        if (t == EARLY_READ && h_acc_early)
            yee_download_acc_raw(h_acc_early, YEE_TOTAL);
    }

    /* ── Step 4: READ — use early accumulator for scoring ──
     * Early read (tick 25) captures spatial locality.
     * Energy near the query position and along waveguides is high.
     * Energy in distant/unconnected regions hasn't arrived yet. */
    float *h_acc = h_acc_early;  /* use early snapshot */
    float *h_V = (float *)malloc(YEE_TOTAL * sizeof(float));
    if (!h_acc || !h_V) { free(h_acc_early); free(h_V); return 0; }

    yee_download_acc_raw(h_acc, YEE_TOTAL);
    yee_download_V(h_V, YEE_TOTAL);

    /* Phase 4a: Compute spatial energy per node from wave accumulator */
    float *node_energy = (float *)calloc(g0->n_nodes, sizeof(float));
    for (int i = 0; i < g0->n_nodes; i++) {
        Node *n = &g0->nodes[i];
        if (!n->alive || n->layer_zero) continue;
        int gx = coord_x(n->coord) % YEE_GX;
        int gy = coord_y(n->coord) % YEE_GY;
        int gz = coord_z(n->coord) % YEE_GZ;
        node_energy[i] = h_acc[yee_vid(gx, gy, gz)];
    }

    /* Phase 4b: Topological propagation — spread energy along graph edges.
     * If node A resonated spatially and has a strong edge to node B,
     * B gets energy even if B is at a different spatial position.
     * This is the fuzzy matching layer. The graph learned which nodes
     * are structurally related regardless of their 4-byte prefix. */
    float *topo_energy = (float *)calloc(g0->n_nodes, sizeof(float));
    for (int e = 0; e < g0->n_edges; e++) {
        Edge *ed = &g0->edges[e];
        if (ed->weight == 0) continue;
        float w = (float)ed->weight / 255.0f;  /* normalize to [0,1] */
        int src = ed->src_a, dst = ed->dst;
        if (src < g0->n_nodes && dst < g0->n_nodes) {
            topo_energy[dst] += node_energy[src] * w * 0.5f;
            topo_energy[src] += node_energy[dst] * w * 0.5f;
        }
    }

    /* Combine: spatial + topological. Topo fills in what spatial misses. */
    for (int i = 0; i < g0->n_nodes; i++)
        node_energy[i] += topo_energy[i];
    free(topo_energy);

    /* Score every alive node by combined energy */
    int n_results = 0;
    for (int i = 0; i < g0->n_nodes; i++) {
        Node *n = &g0->nodes[i];
        if (!n->alive || n->layer_zero) continue;

        int gx = coord_x(n->coord) % YEE_GX;
        int gy = coord_y(n->coord) % YEE_GY;
        int gz = coord_z(n->coord) % YEE_GZ;

        float energy = node_energy[i];
        float voltage = h_V[yee_vid(gx, gy, gz)];

        /* Skip nodes with negligible energy */
        if (energy < 0.0005f) continue;

        /* Z3 observer: estimate zero-crossing frequency from V sign changes.
         * High frequency = resonant node. We approximate from final V and acc. */
        int z3 = (energy > 0.01f && fabsf(voltage) < energy * 0.3f) ? 1 : 0;

        /* Z4 observer: correlated trend — is this node's energy increasing?
         * We check if energy is above background (mean). */
        int z4 = (energy > 0.1f) ? 1 : 0;

        /* Insert into top-K results */
        InferResult r;
        r.node_id = i;
        strncpy(r.name, n->name, NAME_LEN - 1);
        r.name[NAME_LEN - 1] = '\0';
        r.energy = energy;
        r.val = voltage;
        r.z3_freq = z3;
        r.z4_corr = z4;
        r.crystal = crystal_strength(n);

        if (n_results < max_results) {
            results[n_results++] = r;
        } else {
            /* Replace lowest-energy result */
            int min_k = 0;
            for (int k = 1; k < max_results; k++)
                if (results[k].energy < results[min_k].energy) min_k = k;
            if (r.energy > results[min_k].energy)
                results[min_k] = r;
        }
    }

    /* Sort results by energy descending */
    for (int i = 0; i < n_results - 1; i++)
        for (int j = i + 1; j < n_results; j++)
            if (results[j].energy > results[i].energy) {
                InferResult tmp = results[i];
                results[i] = results[j];
                results[j] = tmp;
            }

    free(node_energy);
    free(h_acc);
    free(h_V);
    return n_results;
}

/* ══════════════════════════════════════════════════════════════
 * PUBLIC API
 * ══════════════════════════════════════════════════════════════ */

int infer_query(Engine *eng, const char *query_text,
                InferResult *results, int max_results) {
    return infer_forward(eng, (const uint8_t *)query_text,
                         (int)strlen(query_text), results, max_results);
}

int infer_query_raw(Engine *eng, const uint8_t *data, int len,
                    InferResult *results, int max_results) {
    return infer_forward(eng, data, len, results, max_results);
}
