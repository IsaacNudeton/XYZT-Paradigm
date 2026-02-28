/*
 * main.cu — Unified PC AI Engine entry point
 *
 * Combines v9 CPU engine + v6/v3 3D GPU substrate.
 * Self-contained. No external services. Everything local.
 *
 * Run loop: transduce -> ingest -> GPU tick -> CPU tick -> report
 *
 * Compile:
 *   nvcc -O2 -arch=sm_75 -o xyzt_pc main.cu engine.c wire.c transducer.c onetwo.c substrate.cu reporter.c -lm
 *
 * Usage:
 *   xyzt_pc run              — interactive mode (stdin)
 *   xyzt_pc test             — self-test
 *   xyzt_pc ingest <file>    — ingest a file
 *   xyzt_pc bench            — GPU benchmark
 *
 * Isaac Oravec & Claude, February 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "engine.h"
#include "onetwo.h"
#include "wire.h"
#include "transducer.h"
}
#include "substrate.cuh"

/* Forward declarations from reporter.c */
extern "C" {
void report_shells(const Engine *eng);
void report_onetwo(const Engine *eng);
void report_nesting(const Engine *eng);
void report_top_nodes(const Engine *eng, int shell, int top_n);
void report_gpu_substrate(const CubeState *cubes, int n_cubes);
void report_full(const Engine *eng, const CubeState *cubes, int n_cubes);
}

/* ══════════════════════════════════════════════════════════════
 * GPU BENCHMARK
 * ══════════════════════════════════════════════════════════════ */

static void cmd_bench(void) {
    printf("=== GPU BENCHMARK ===\n\n");

    char info[512];
    substrate_device_info(info, sizeof(info));
    printf("%s\n", info);

    int cube_counts[] = {64, 256, 1024, 4096};

    for (int tc = 0; tc < 4; tc++) {
        int n_cubes = cube_counts[tc];
        int total_voxels = n_cubes * CUBE_SIZE;

        if (substrate_init(n_cubes) != 0) {
            printf("  Failed to init %d cubes\n", n_cubes);
            continue;
        }

        /* Set up some marks for a meaningful test */
        CubeState *h_cubes = (CubeState *)calloc(n_cubes, sizeof(CubeState));
        for (int c = 0; c < n_cubes; c++) {
            /* Wire each position to its neighbors (done by kernel_auto_wire_local) */
            h_cubes[c].substrate[0] = 128;   /* Alive */
            h_cubes[c].substrate[1] = (c % 2) ? 128 : 0;
        }
        substrate_upload(h_cubes, n_cubes);

        /* Warm up */
        int *results = (int *)calloc(n_cubes * CUBE_SIZE, sizeof(int));
        substrate_tick_and_observe(n_cubes, OBS_PARITY, -1, results);

        /* Benchmark */
        cudaEvent_t start, stop;
        cudaEventCreate(&start);
        cudaEventCreate(&stop);

        int iterations = 1000;
        cudaEventRecord(start);
        for (int i = 0; i < iterations; i++) {
            substrate_tick(n_cubes);
        }
        cudaEventRecord(stop);
        cudaDeviceSynchronize();

        float ms = 0;
        cudaEventElapsedTime(&ms, start, stop);

        double cubes_per_sec = (double)n_cubes * iterations / (ms / 1000.0);
        double voxels_per_sec = cubes_per_sec * CUBE_SIZE;

        printf("  %4d cubes (%6d voxels): %.1f ms for %dk ticks = %.1f M cube-ticks/s = %.1f M voxel-ticks/s\n",
               n_cubes, total_voxels, ms, iterations,
               cubes_per_sec / 1e6, voxels_per_sec / 1e6);

        cudaEventDestroy(start);
        cudaEventDestroy(stop);
        free(h_cubes);
        free(results);
        substrate_destroy();
    }
    printf("\n");
}

/* ══════════════════════════════════════════════════════════════
 * SELF-TEST
 * ══════════════════════════════════════════════════════════════ */

extern "C" int g_pass = 0;
extern "C" int g_fail = 0;

extern "C" {
void run_core_tests(void);
void run_gpu_tests(void);
void run_lifecycle_tests(void);
void run_observer_tests(void);
void run_stress_tests(void);
}

static void cmd_test(void) {
    printf("=== XYZT PC Self-Test ===\n\n");

    run_core_tests();
    run_gpu_tests();
    run_lifecycle_tests();
    run_observer_tests();
    run_stress_tests();

    printf("\n=== RESULTS: %d passed, %d failed, %d total ===\n",
           g_pass, g_fail, g_pass + g_fail);
    if (g_fail == 0) printf("ALL PASS.\n");
}


/* ══════════════════════════════════════════════════════════════
 * T3 EXPERIMENT — Falsifiable child divergence test
 *
 * Hypothesis: Children spawned from parents that ingested
 * structurally different data develop distinct internal
 * topologies (different edge weights, output values,
 * retina patterns).
 *
 * Falsification: If all children converge to identical
 * topology regardless of parent input type, the retina
 * mechanism is not transmitting spatial information.
 *
 * Protocol:
 *   1. Ingest 3 structurally different inputs (C source,
 *      binary exe, prose text)
 *   2. Tick with GPU sync until crystallization + child spawn
 *   3. Tick N more with GPU sync to let children evolve
 *   4. Compare child topology fingerprints
 *   5. Report divergence metric and pass/fail
 *
 * Isaac Oravec & Claude, February 2026
 * ══════════════════════════════════════════════════════════════ */

/* Forward declaration — defined below */
static void hookup_retinas(Engine *eng, CubeState *h_cubes, int n_cubes);

typedef struct {
    int   slot;             /* child_pool index */
    int   owner_id;         /* parent node id in shell 0 */
    char  owner_name[128];  /* parent node name */
    /* Child topology snapshot */
    int32_t retina_vals[8]; /* 8 octant node values */
    int32_t output_val;     /* output node (index 8) value */
    uint8_t edge_weights[4];/* 4 edges: (r0,r1)->out, (r2,r3)->out, etc. */
    uint8_t retina_valence[8];
    uint8_t output_valence;
    int     output_crystal;
    uint64_t child_ticks;
    /* Parent context */
    uint8_t parent_valence;
    int     parent_crystal;
    int32_t parent_val;
    Coord   parent_coord;
} ChildSnapshot;

static int snapshot_children(const Engine *eng, ChildSnapshot *snaps, int max_snaps) {
    int n = 0;
    for (int c = 0; c < MAX_CHILDREN && n < max_snaps; c++) {
        if (eng->child_owner[c] < 0) continue;
        const Graph *child = &eng->child_pool[c];
        int owner_id = eng->child_owner[c];
        if (owner_id >= eng->shells[0].g.n_nodes) continue;
        const Node *owner = &eng->shells[0].g.nodes[owner_id];

        ChildSnapshot *s = &snaps[n];
        memset(s, 0, sizeof(*s));
        s->slot = c;
        s->owner_id = owner_id;
        strncpy(s->owner_name, owner->name, 127);

        /* Retina node values and valence */
        for (int r = 0; r < 8 && r < child->n_nodes; r++) {
            s->retina_vals[r] = child->nodes[r].val;
            s->retina_valence[r] = child->nodes[r].valence;
        }

        /* Output node */
        int out_idx = child->n_nodes - 1;
        if (out_idx >= 0 && child->nodes[out_idx].alive) {
            s->output_val = child->nodes[out_idx].val;
            s->output_valence = child->nodes[out_idx].valence;
            s->output_crystal = crystal_strength(&child->nodes[out_idx]);
        }

        /* Edge weights */
        for (int e = 0; e < 4 && e < child->n_edges; e++)
            s->edge_weights[e] = child->edges[e].weight;

        s->child_ticks = child->total_ticks;

        /* Parent context */
        s->parent_valence = owner->valence;
        s->parent_crystal = crystal_strength(owner);
        s->parent_val = owner->val;
        s->parent_coord = owner->coord;

        n++;
    }
    return n;
}

static int topology_distance(const ChildSnapshot *a, const ChildSnapshot *b) {
    /* Manhattan distance across all topology dimensions.
     * Retina vals + output val + edge weights + valence.
     * High = divergent topologies. Low = convergent. */
    int dist = 0;

    /* Retina values — the main signal. If retina works, these differ. */
    for (int r = 0; r < 8; r++)
        dist += abs(a->retina_vals[r] - b->retina_vals[r]);

    /* Output value */
    dist += abs(a->output_val - b->output_val);

    /* Edge weights */
    for (int e = 0; e < 4; e++)
        dist += abs((int)a->edge_weights[e] - (int)b->edge_weights[e]);

    /* Valence pattern */
    for (int r = 0; r < 8; r++)
        dist += abs((int)a->retina_valence[r] - (int)b->retina_valence[r]);
    dist += abs((int)a->output_valence - (int)b->output_valence);

    return dist;
}

static void report_child_detail(const ChildSnapshot *s) {
    printf("  child[%d] owned by '%s' (node %d)\n", s->slot, s->owner_name, s->owner_id);
    printf("    parent: val=%d valence=%d crystal=%d coord=(%d,%d,%d)\n",
           s->parent_val, s->parent_valence, s->parent_crystal,
           coord_x(s->parent_coord), coord_y(s->parent_coord), coord_z(s->parent_coord));
    printf("    retina: [%d, %d, %d, %d, %d, %d, %d, %d]\n",
           s->retina_vals[0], s->retina_vals[1], s->retina_vals[2], s->retina_vals[3],
           s->retina_vals[4], s->retina_vals[5], s->retina_vals[6], s->retina_vals[7]);
    printf("    retina_valence: [%d, %d, %d, %d, %d, %d, %d, %d]\n",
           s->retina_valence[0], s->retina_valence[1], s->retina_valence[2], s->retina_valence[3],
           s->retina_valence[4], s->retina_valence[5], s->retina_valence[6], s->retina_valence[7]);
    printf("    output: val=%d valence=%d crystal=%d\n",
           s->output_val, s->output_valence, s->output_crystal);
    printf("    edges: [%d, %d, %d, %d]\n",
           s->edge_weights[0], s->edge_weights[1], s->edge_weights[2], s->edge_weights[3]);
    printf("    ticks: %llu\n", (unsigned long long)s->child_ticks);
}

static void cmd_t3(int argc, char *argv[]) {
    printf("=== T3 EXPERIMENT: Child Divergence Falsification ===\n\n");
    printf("HYPOTHESIS: Children from structurally different parents develop\n");
    printf("  distinct internal topologies via retina spatial reading.\n");
    printf("FALSIFICATION: If all children converge to identical topology\n");
    printf("  regardless of parent data, retina is broken.\n\n");

    /* Parse file arguments or use defaults */
    const char *file_c    = (argc >= 3) ? argv[2] : NULL;
    const char *file_bin  = (argc >= 4) ? argv[3] : NULL;
    const char *file_text = (argc >= 5) ? argv[4] : NULL;

    Engine eng;
    engine_init(&eng);

    /* GPU setup */
    int n_cubes = 4096;
    CubeState *h_cubes = (CubeState *)calloc(n_cubes, sizeof(CubeState));
    int gpu_ok = (substrate_init(n_cubes) == 0);
    if (gpu_ok) {
        substrate_download(h_cubes, n_cubes);
        hookup_retinas(&eng, h_cubes, n_cubes);
        printf("GPU: %d cubes (%d voxels) initialized.\n\n", n_cubes, n_cubes * CUBE_SIZE);
    } else {
        printf("WARNING: GPU init failed. Running CPU-only (retina will use fallback).\n\n");
    }

    /* ── PHASE 1: Ingest structurally different inputs ─────────── */
    printf("─── PHASE 1: INGEST ───\n");

    int ids[3] = {-1, -1, -1};
    const char *labels[3] = {"C_SOURCE", "BINARY", "PROSE"};

    if (file_c) {
        ids[0] = engine_ingest_file(&eng, file_c);
        printf("  [%s] ingested '%s' -> node %d\n", labels[0], file_c, ids[0]);
    } else {
        /* Embed C source snippet */
        const char *c_src =
            "#include <stdio.h>\n"
            "int main(void) {\n"
            "    uint64_t rd = cube->reads[pos];\n"
            "    uint64_t present = 0;\n"
            "    while (bits) {\n"
            "        int b = __ffsll(bits) - 1;\n"
            "        if (b < CUBE_SIZE && snap[b])\n"
            "            present |= (1ULL << b);\n"
            "        bits &= bits - 1;\n"
            "    }\n"
            "    cube->co_present[pos] = present;\n"
            "    uint32_t sr = cube->shift_reg[pos];\n"
            "    sr = (sr << 1) | (present != 0 ? 1 : 0);\n"
            "    for (int i = 0; i < g->n_nodes; i++)\n"
            "        if (g->nodes[i].alive && !g->nodes[i].layer_zero)\n"
            "            crystal_update(&g->nodes[i], g->edges, g->n_edges, i);\n"
            "    return 0;\n"
            "}\n";
        ids[0] = engine_ingest_text(&eng, "t3_c_source", c_src);
        printf("  [%s] ingested embedded C source -> node %d\n", labels[0], ids[0]);
    }

    if (file_bin) {
        ids[1] = engine_ingest_file(&eng, file_bin);
        printf("  [%s] ingested '%s' -> node %d\n", labels[1], file_bin, ids[1]);
    } else {
        /* Embed binary-like data (high entropy, non-text) */
        uint8_t bin_data[256];
        uint32_t seed = 0xDEADBEEF;
        for (int i = 0; i < 256; i++) {
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            bin_data[i] = (uint8_t)(seed & 0xFF);
        }
        BitStream bs;
        onetwo_parse(bin_data, 256, &bs);
        ids[1] = engine_ingest(&eng, "t3_binary", &bs);
        printf("  [%s] ingested embedded pseudo-binary -> node %d\n", labels[1], ids[1]);
    }

    if (file_text) {
        ids[2] = engine_ingest_file(&eng, file_text);
        printf("  [%s] ingested '%s' -> node %d\n", labels[2], file_text, ids[2]);
    } else {
        /* Embed prose text */
        const char *prose =
            "The fundamental insight is that position itself carries value. "
            "When two patterns collide in the same spatial location, the collision "
            "is not a problem to be solved but the computation itself. Traditional "
            "architectures route data to ALUs for processing, but in wave computing "
            "the medium is the message. A wavefront propagating through a substrate "
            "encounters obstacles, reflects, interferes, and the resulting pattern "
            "at any observation point encodes the answer. The observer creates the "
            "operation by choosing where and how to look. This is not metaphor. "
            "Junction impedance provides dynamic range control over both collision "
            "magnitude and timing delays measured in hardware.";
        ids[2] = engine_ingest_text(&eng, "t3_prose", prose);
        printf("  [%s] ingested embedded prose -> node %d\n", labels[2], ids[2]);
    }

    /* Sync to GPU */
    if (gpu_ok) {
        wire_engine_to_gpu(&eng, h_cubes, n_cubes);
        substrate_upload(h_cubes, n_cubes);
    }

    /* Report spatial separation */
    printf("\n  Spatial coordinates:\n");
    for (int i = 0; i < 3; i++) {
        if (ids[i] < 0) continue;
        Node *n = &eng.shells[0].g.nodes[ids[i]];
        printf("    [%s] node %d: coord=(%d,%d,%d) -> cube=%d\n",
               labels[i], ids[i],
               coord_x(n->coord), coord_y(n->coord), coord_z(n->coord),
               cube_id_from(coord_x(n->coord) / CUBE_DIM,
                            coord_y(n->coord) / CUBE_DIM,
                            coord_z(n->coord) / CUBE_DIM));
    }

    /* ── PHASE 2: Tick to crystallization ──────────────────────── */
    printf("\n─── PHASE 2: CRYSTALLIZE ───\n");

    int max_phase2 = 50000;
    int phase2_report = 5000;
    int spawned = 0;
    int tick = 0;

    for (tick = 0; tick < max_phase2; tick++) {
        if (gpu_ok) {
            substrate_route_step(n_cubes);
            substrate_inject_gateways(n_cubes);
            substrate_tick(n_cubes);
        }
        engine_tick(&eng);

        if (gpu_ok && eng.total_ticks % SUBSTRATE_INT == 0) {
            substrate_download(h_cubes, n_cubes);
            hookup_retinas(&eng, h_cubes, n_cubes);
            wire_gpu_to_engine(&eng, h_cubes, n_cubes);
            wire_hebbian_from_gpu(&eng, h_cubes, n_cubes);
            wire_engine_to_gpu(&eng, h_cubes, n_cubes);
            substrate_upload(h_cubes, n_cubes);
        }

        /* Check spawn progress */
        if (eng.n_children > spawned) {
            printf("  tick %d: child spawned (%d/%d)\n", tick, eng.n_children, MAX_CHILDREN);
            spawned = eng.n_children;
        }

        /* Report progress */
        if (tick > 0 && tick % phase2_report == 0) {
            int max_val = 0;
            for (int i = 0; i < 3; i++) {
                if (ids[i] >= 0) {
                    int v = eng.shells[0].g.nodes[ids[i]].valence;
                    if (v > max_val) max_val = v;
                }
            }
            printf("  tick %d: max_valence=%d children=%d\n", tick, max_val, eng.n_children);
        }

        /* Stop if we have at least 2 children */
        if (eng.n_children >= 2) break;
    }

    if (eng.n_children < 2) {
        printf("\n  ABORT: Only %d children after %d ticks. Need >= 2 for comparison.\n",
               eng.n_children, tick);
        printf("  Valence status:\n");
        for (int i = 0; i < 3; i++) {
            if (ids[i] < 0) continue;
            Node *n = &eng.shells[0].g.nodes[ids[i]];
            printf("    [%s] node %d: valence=%d crystal=%d\n",
                   labels[i], ids[i], n->valence, crystal_strength(n));
        }
        free(h_cubes);
        if (gpu_ok) substrate_destroy();
        engine_destroy(&eng);
        return;
    }

    printf("  Phase 2 complete: %d children spawned in %d ticks.\n", eng.n_children, tick);

    /* ── PHASE 3: Evolve children ──────────────────────────────── */
    printf("\n─── PHASE 3: EVOLVE CHILDREN ───\n");

    /* Snapshot children before evolution */
    ChildSnapshot before[MAX_CHILDREN];
    int n_before = snapshot_children(&eng, before, MAX_CHILDREN);
    printf("  Pre-evolution snapshot: %d children\n", n_before);

    int evolve_ticks = 10000;
    for (int t = 0; t < evolve_ticks; t++) {
        if (gpu_ok) {
            substrate_route_step(n_cubes);
            substrate_inject_gateways(n_cubes);
            substrate_tick(n_cubes);
        }
        engine_tick(&eng);

        if (gpu_ok && eng.total_ticks % SUBSTRATE_INT == 0) {
            substrate_download(h_cubes, n_cubes);
            hookup_retinas(&eng, h_cubes, n_cubes);
            wire_gpu_to_engine(&eng, h_cubes, n_cubes);
            wire_hebbian_from_gpu(&eng, h_cubes, n_cubes);
            wire_engine_to_gpu(&eng, h_cubes, n_cubes);
            substrate_upload(h_cubes, n_cubes);
        }

        if (eng.n_children > spawned) {
            printf("  evolve tick %d: new child (%d total)\n", t, eng.n_children);
            spawned = eng.n_children;
        }
    }

    /* Final GPU sync */
    if (gpu_ok) {
        substrate_download(h_cubes, n_cubes);
        hookup_retinas(&eng, h_cubes, n_cubes);
        wire_gpu_to_engine(&eng, h_cubes, n_cubes);
    }

    /* Snapshot children after evolution */
    ChildSnapshot after[MAX_CHILDREN];
    int n_after = snapshot_children(&eng, after, MAX_CHILDREN);

    /* ── PHASE 4: Compare & Falsify ───────────────────────────── */
    printf("\n─── PHASE 4: TOPOLOGY COMPARISON ───\n\n");

    printf("Children after %d evolution ticks:\n\n", evolve_ticks);
    for (int i = 0; i < n_after; i++)
        report_child_detail(&after[i]);

    printf("\n─── DIVERGENCE MATRIX ───\n\n");

    /* Pairwise distance matrix */
    int total_dist = 0, n_pairs = 0;
    int max_dist = 0, min_dist = 999999;

    for (int i = 0; i < n_after; i++) {
        for (int j = i + 1; j < n_after; j++) {
            int dist = topology_distance(&after[i], &after[j]);
            printf("  child[%d] '%s' vs child[%d] '%s': distance = %d\n",
                   after[i].slot, after[i].owner_name,
                   after[j].slot, after[j].owner_name, dist);
            total_dist += dist;
            n_pairs++;
            if (dist > max_dist) max_dist = dist;
            if (dist < min_dist) min_dist = dist;
        }
    }

    int mean_dist = n_pairs > 0 ? total_dist / n_pairs : 0;

    /* Self-evolution: how much did each child change? */
    printf("\n─── SELF-EVOLUTION (before vs after) ───\n\n");
    int total_self_change = 0;
    for (int i = 0; i < n_before && i < n_after; i++) {
        if (before[i].slot != after[i].slot) continue;
        int self_dist = topology_distance(&before[i], &after[i]);
        printf("  child[%d] '%s': self-change = %d\n",
               after[i].slot, after[i].owner_name, self_dist);
        total_self_change += self_dist;
    }

    /* Retina alive check: are retina nodes actually getting nonzero values? */
    printf("\n─── RETINA LIVENESS CHECK ───\n\n");
    int retina_alive = 0, retina_zero = 0;
    for (int i = 0; i < n_after; i++) {
        int nonzero = 0;
        for (int r = 0; r < 8; r++)
            if (after[i].retina_vals[r] != 0) nonzero++;
        printf("  child[%d]: %d/8 retina nodes nonzero\n", after[i].slot, nonzero);
        retina_alive += nonzero;
        retina_zero += (8 - nonzero);
    }

    /* ── VERDICT ──────────────────────────────────────────────── */
    printf("\n═══════════════════════════════════════════════════════\n");
    printf("  T3 RESULTS\n");
    printf("═══════════════════════════════════════════════════════\n\n");

    printf("  Total ticks:        %llu\n", (unsigned long long)eng.total_ticks);
    printf("  Children:           %d\n", n_after);
    printf("  Retina nodes alive: %d/%d\n", retina_alive, n_after * 8);
    printf("  Mean pairwise dist: %d\n", mean_dist);
    printf("  Min pairwise dist:  %d\n", n_pairs > 0 ? min_dist : 0);
    printf("  Max pairwise dist:  %d\n", max_dist);
    printf("  Total self-change:  %d\n", total_self_change);

    /* Falsification criteria:
     *   1. Retina must be alive (>50% nodes nonzero)
     *   2. Children must have evolved (self-change > 0)
     *   3. Pairwise distance must be nonzero (topology diverged)
     *
     * If all three pass: T3 → T2 (retina transmits, children diverge)
     * If any fail: identify which mechanism is broken */

    int retina_ok = retina_alive > n_after * 4;  /* >50% alive */
    int evolved_ok = total_self_change > 0;
    int diverged_ok = mean_dist > 0;

    printf("\n  CRITERIA:\n");
    printf("    [%s] Retina alive (>50%% nodes nonzero)\n", retina_ok ? "PASS" : "FAIL");
    printf("    [%s] Children evolved (self-change > 0)\n", evolved_ok ? "PASS" : "FAIL");
    printf("    [%s] Children diverged (mean dist > 0)\n", diverged_ok ? "PASS" : "FAIL");

    if (retina_ok && evolved_ok && diverged_ok) {
        printf("\n  ▓▓ T3 → T2: Children diverge based on parent spatial context. ▓▓\n");
        printf("  Retina mechanism transmits spatial information to children.\n");
    } else if (!retina_ok) {
        printf("\n  ▓▓ T3 BLOCKED: Retina nodes not receiving substrate data. ▓▓\n");
        printf("  Check: hookup_retinas(), substrate values at parent coords,\n");
        printf("  SUB_ALIVE threshold, and GPU sync timing.\n");
    } else if (!evolved_ok) {
        printf("\n  ▓▓ T3 BLOCKED: Children not evolving (child_tick_once dead?). ▓▓\n");
        printf("  Check: layer_zero clearing, edge weights, identity.len.\n");
    } else {
        printf("\n  ▓▓ T3 FALSIFIED: Children converge regardless of parent input. ▓▓\n");
        printf("  Retina is alive but not discriminative. The spatial signal\n");
        printf("  is either too uniform or too noisy for children to differentiate.\n");
    }
    printf("\n═══════════════════════════════════════════════════════\n\n");

    /* Full engine report */
    if (gpu_ok)
        report_full(&eng, h_cubes, n_cubes);
    else
        report_full(&eng, NULL, 0);

    free(h_cubes);
    if (gpu_ok) substrate_destroy();
    engine_destroy(&eng);
}

/* ══════════════════════════════════════════════════════════════
 * RETINA HOOKUP — zero-copy pointer entanglement
 * ══════════════════════════════════════════════════════════════ */

static void hookup_retinas(Engine *eng, CubeState *h_cubes, int n_cubes) {
    /* For each child graph, point its retina at the parent's substrate slice.
     * The child reads 64 substrate bytes directly — no copy, no attenuation.
     * Different parents at different spatial locations → different retina
     * patterns → distinct child topologies. */
    for (int c = 0; c < MAX_CHILDREN; c++) {
        if (eng->child_owner[c] < 0) continue;
        int owner_id = eng->child_owner[c];
        if (owner_id >= eng->shells[0].g.n_nodes) continue;
        Node *owner = &eng->shells[0].g.nodes[owner_id];
        int x = coord_x(owner->coord) % (VOL_X * CUBE_DIM);
        int y = coord_y(owner->coord) % (VOL_Y * CUBE_DIM);
        int z = coord_z(owner->coord) % (VOL_Z * CUBE_DIM);
        int cx = x / CUBE_DIM, cy = y / CUBE_DIM, cz = z / CUBE_DIM;
        int cube_id = cube_id_from(cx, cy, cz);
        if (cube_id >= 0 && cube_id < n_cubes) {
            eng->child_pool[c].retina = h_cubes[cube_id].substrate;
            eng->child_pool[c].retina_len = CUBE_SIZE;
        }
    }
}

/* ══════════════════════════════════════════════════════════════
 * INTERACTIVE MODE
 * ══════════════════════════════════════════════════════════════ */

static void cmd_run(void) {
    printf("=== XYZT PC — Interactive Mode ===\n");
    printf("Commands: ingest <text>, tick [N], status, query <text>, save <path>, load <path>, quit\n\n");

    Engine eng;
    engine_init(&eng);

    int n_cubes = 4096; /* Full 16^3 volume = 262144 voxels.
                           GPU proven at this scale (9.5B voxel-ticks/s, ~8MB VRAM) */
    CubeState *h_cubes = (CubeState *)calloc(n_cubes, sizeof(CubeState));
    int gpu_ok = (substrate_init(n_cubes) == 0);
    if (gpu_ok) {
        /* Download auto-wired state so h_cubes has reads[]/active[] from kernel_auto_wire_local */
        substrate_download(h_cubes, n_cubes);
        hookup_retinas(&eng, h_cubes, n_cubes);
        printf("GPU substrate initialized: %d cubes (%d voxels)\n\n", n_cubes, n_cubes * CUBE_SIZE);
    } else {
        printf("GPU init failed, running CPU-only\n\n");
    }

    char line[4096];
    while (1) {
        printf("xyzt> ");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin)) break;

        /* Strip newline */
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = 0;
        if (len == 0) continue;

        if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) break;

        if (strcmp(line, "status") == 0) {
            if (gpu_ok) {
                substrate_download(h_cubes, n_cubes);
                hookup_retinas(&eng, h_cubes, n_cubes);
                report_full(&eng, h_cubes, n_cubes);
            } else {
                report_full(&eng, NULL, 0);
            }
        }
        else if (strncmp(line, "ingest ", 7) == 0) {
            const char *text = line + 7;
            /* Check if it's a file path */
            FILE *f = fopen(text, "rb");
            if (f) {
                fclose(f);
                int id = engine_ingest_file(&eng, text);
                printf("Ingested file '%s' -> node %d\n", text, id);
            } else {
                /* Treat as text */
                char name[64];
                snprintf(name, 64, "input_%d", eng.shells[0].g.n_nodes);
                int id = engine_ingest_text(&eng, name, text);
                printf("Ingested text -> node %d '%s'\n", id, name);
            }

            /* Sync to GPU */
            if (gpu_ok) {
                wire_engine_to_gpu(&eng, h_cubes, n_cubes);
                substrate_upload(h_cubes, n_cubes);
            }
        }
        else if (strncmp(line, "tick", 4) == 0) {
            int n = 1;
            if (len > 5) n = atoi(line + 5);
            if (n < 1) n = 1;
            if (n > 100000) n = 100000;

            for (int i = 0; i < n; i++) {
                /* GPU tick: route → inject gateways → threshold tap → co-presence */
                if (gpu_ok) {
                    substrate_route_step(n_cubes);
                    substrate_inject_gateways(n_cubes);
                    substrate_tick(n_cubes);
                }
                /* CPU tick */
                engine_tick(&eng);

                /* Periodic GPU↔CPU sync every SUBSTRATE_INT ticks.
                 * Without this, the bridge is open-loop during batches —
                 * GPU evolves without CPU feedback and vice versa.
                 * Cost: ~1ms per sync (6MB PCIe download). */
                if (gpu_ok && eng.total_ticks % SUBSTRATE_INT == 0) {
                    substrate_download(h_cubes, n_cubes);
                    hookup_retinas(&eng, h_cubes, n_cubes);
                    wire_gpu_to_engine(&eng, h_cubes, n_cubes);
                    wire_hebbian_from_gpu(&eng, h_cubes, n_cubes);
                    wire_engine_to_gpu(&eng, h_cubes, n_cubes);
                    substrate_upload(h_cubes, n_cubes);
                }
            }

            /* Final sync at end of batch */
            if (gpu_ok) {
                substrate_download(h_cubes, n_cubes);
                hookup_retinas(&eng, h_cubes, n_cubes);
                wire_gpu_to_engine(&eng, h_cubes, n_cubes);
                wire_hebbian_from_gpu(&eng, h_cubes, n_cubes);
            }

            printf("Ticked %d times (total: %llu)\n", n, (unsigned long long)eng.total_ticks);
        }
        else if (strncmp(line, "query ", 6) == 0) {
            const char *qtext = line + 6;
            int qlen = len - 6;
            QueryResult results[10];
            int n = 0;

            if (qlen < 64) {
                /* Short query: name substring search (ONETWO can't discriminate <64 bytes) */
                n = engine_query_name(&eng, qtext, results, 10);
            } else {
                /* Long query: structural ONETWO containment */
                BitStream bs;
                onetwo_parse((const uint8_t *)qtext, qlen, &bs);
                n = engine_query(&eng, &bs, results, 10);
            }

            if (n == 0) {
                printf("No matches.\n");
            } else {
                printf("Found %d matches:\n", n);
                for (int i = 0; i < n; i++) {
                    printf("  [%d] shell=%d corr=%d crystal=%d res=%d '%s'\n",
                           results[i].id, results[i].shell, results[i].raw_corr,
                           results[i].crystal, results[i].resonance, results[i].name);
                }
            }
        }
        else if (strncmp(line, "save ", 5) == 0) {
            if (engine_save(&eng, line + 5) == 0)
                printf("Saved to '%s'\n", line + 5);
            else
                printf("Save failed.\n");
        }
        else if (strncmp(line, "load ", 5) == 0) {
            int lr = engine_load(&eng, line + 5);
            if (lr == 0)
                printf("Loaded from '%s'\n", line + 5);
            else
                printf("Load failed (error %d). Supports v3-v10. Check version with hex dump.\n", lr);
        }
        else {
            printf("Unknown command. Try: ingest, tick, status, query, save, load, quit\n");
        }
    }

    free(h_cubes);
    if (gpu_ok) substrate_destroy();
    engine_destroy(&eng);
}

/* ══════════════════════════════════════════════════════════════
 * FILE INGEST MODE
 * ══════════════════════════════════════════════════════════════ */

static void cmd_ingest(const char *path) {
    printf("=== XYZT PC — Ingest '%s' ===\n\n", path);

    Engine eng;
    engine_init(&eng);

    int id = engine_ingest_file(&eng, path);
    if (id < 0) {
        printf("Error: could not ingest '%s'\n", path);
        engine_destroy(&eng);
        return;
    }
    printf("Ingested -> node %d\n", id);

    /* Run ticks to let the engine process */
    printf("Running 500 ticks...\n");
    for (int i = 0; i < 500; i++) engine_tick(&eng);

    report_full(&eng, NULL, 0);

    engine_destroy(&eng);
}

/* ══════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
    printf("==========================================================\n");
    printf("  XYZT Unified PC Engine\n");
    printf("  v9 shells + 3D GPU substrate + ONETWO feedback\n");
    printf("  Isaac Oravec & Claude, February 2026\n");
    printf("==========================================================\n\n");

    if (argc < 2 || strcmp(argv[1], "run") == 0) {
        cmd_run();
    } else if (strcmp(argv[1], "test") == 0) {
        cmd_test();
        return g_fail;
    } else if (strcmp(argv[1], "bench") == 0) {
        cmd_bench();
    } else if (strcmp(argv[1], "ingest") == 0 && argc >= 3) {
        cmd_ingest(argv[2]);
    } else if (strcmp(argv[1], "t3") == 0) {
        cmd_t3(argc, argv);
    } else if (strcmp(argv[1], "exec") == 0 && argc >= 3) {
        Engine eng;
        engine_init(&eng);
        engine_exec(&eng, argv[2]);
        engine_destroy(&eng);
    } else if (strcmp(argv[1], "wire_import") == 0 && argc >= 3) {
        Engine eng;
        engine_init(&eng);
        int n = engine_wire_import(&eng, argv[2]);
        if (n > 0) engine_status(&eng);
        engine_destroy(&eng);
    } else if (strcmp(argv[1], "wire_export") == 0 && argc >= 3) {
        Engine eng;
        engine_init(&eng);
        engine_wire_export(&eng, argv[2]);
        engine_destroy(&eng);
    } else if (strcmp(argv[1], "bridge") == 0) {
        const char *wpath = argc >= 3 ? argv[2] : NULL;
        if (!wpath) {
            /* Default: ~/.xyzt/wire.bin */
            static char default_path[512];
            const char *home = getenv("USERPROFILE");
            if (!home) home = getenv("HOME");
            if (home) {
                snprintf(default_path, sizeof(default_path), "%s/.xyzt/wire.bin", home);
                wpath = default_path;
            }
        }
        if (!wpath) {
            printf("No wire.bin path (set USERPROFILE or pass as argument)\n");
            return 1;
        }
        printf("=== BRIDGE: %s ===\n", wpath);
        Engine eng;
        engine_init(&eng);

        int n_cubes = 4096;
        CubeState *h_cubes = (CubeState *)calloc(n_cubes, sizeof(CubeState));
        int gpu_ok = (substrate_init(n_cubes) == 0);
        if (gpu_ok) substrate_download(h_cubes, n_cubes);

        int imp = engine_wire_import(&eng, wpath);
        if (imp <= 0) {
            printf("Import failed (%d)\n", imp);
            free(h_cubes);
            if (gpu_ok) substrate_destroy();
            engine_destroy(&eng);
            return 1;
        }

        /* Sync to GPU and run SUBSTRATE_INT ticks */
        if (gpu_ok) {
            wire_engine_to_gpu(&eng, h_cubes, n_cubes);
            substrate_upload(h_cubes, n_cubes);
        }

        printf("Running %u ticks...\n", SUBSTRATE_INT);
        for (uint32_t i = 0; i < SUBSTRATE_INT; i++) {
            if (gpu_ok) {
                substrate_route_step(n_cubes);
                substrate_inject_gateways(n_cubes);
                substrate_tick(n_cubes);
            }
            engine_tick(&eng);
        }

        /* Final sync */
        if (gpu_ok) {
            substrate_download(h_cubes, n_cubes);
            wire_gpu_to_engine(&eng, h_cubes, n_cubes);
            wire_hebbian_from_gpu(&eng, h_cubes, n_cubes);
        }

        engine_status(&eng);

        /* Export enriched graph */
        char out_path[512];
        snprintf(out_path, sizeof(out_path), "%s.enriched", wpath);
        engine_wire_export(&eng, out_path);
        printf("Enriched graph written to: %s\n", out_path);

        free(h_cubes);
        if (gpu_ok) substrate_destroy();
        engine_destroy(&eng);
    } else {
        printf("Usage: xyzt_pc [run|test|bench|ingest <file>|t3|exec <file.xyzt>|wire_import <path>|wire_export <path>|bridge [wire.bin]]\n");
        return 1;
    }

    return 0;
}
