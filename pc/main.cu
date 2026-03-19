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
#include "sense.h"
#include "io.h"
#include "infer.h"
#include "cortex.h"
}
#include "substrate.cuh"
#include "yee.cuh"

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
void run_sense_tests(void);
void run_collision_tests(void);
void run_t3_stage1_tests(void);
void run_t3_full_tests(void);
void run_save_load_tests(void);
void run_yee_save_load_tests(void);
void run_tline_tests(void);
void run_child_conflict_tests(void);
void run_external_tests(void);
void run_lfield_test(void);
void run_stress_system_tests(void);
void run_tracking_sweep(void);
void run_sense_diagnostic(void);
}

static void cmd_sweep(void) {
    /* Metric collection: 25-sentence corpus with overlapping themes, 40 cycles */
    Engine eng; engine_init(&eng);

    const char *corpus[] = {
        "the quick brown fox jumps over the lazy dog near the river bank",
        "hydrogen helium lithium beryllium boron carbon nitrogen oxygen fluorine neon",
        "struct node val accum identity valence crystal relay collision threshold",
        "alpha bravo charlie delta echo foxtrot golf hotel india juliet kilo lima",
        "zero one two three four five six seven eight nine ten eleven twelve thirteen",
        "the brown fox sleeps under the oak tree by the quiet river bank at dawn",
        "sodium magnesium aluminum silicon phosphorus sulfur chlorine argon potassium",
        "edge weight source destination propagate impedance fresnel junction boundary",
        "mike november oscar papa quebec romeo sierra tango uniform victor whiskey",
        "fourteen fifteen sixteen seventeen eighteen nineteen twenty thirty forty fifty",
        "a lazy dog rests near the brown fox while the river flows toward the sea",
        "calcium scandium titanium vanadium chromium manganese iron cobalt nickel copper",
        "graph shell observer substrate lattice topology coherent valence crystal lysis",
        "xray yankee zulu alpha bravo charlie one two three four five six seven eight",
        "hundred thousand million billion trillion quadrillion quintillion sextillion",
        "the river bank where the fox and dog meet is shaded by ancient oak trees",
        "zinc gallium germanium arsenic selenium bromine krypton rubidium strontium",
        "accumulate propagate crystallize observe prune grow wire resolve identity bloom",
        "nine eight seven six five four three two one zero ignition liftoff altitude",
        "the dog chases the fox across the river bank and through the oak forest",
        "yttrium zirconium niobium molybdenum technetium ruthenium rhodium palladium",
        "impedance matching fresnel conservation energy bounded lysis trigger nesting",
        "delta echo foxtrot golf hotel india juliet kilo lima mike november oscar papa",
        "fibonacci prime composite perfect abundant deficient amicable sociable happy",
        "both the fox and the dog drink from the same river under the same oak tree",
    };
    int n_corpus = 25;
    for (int k = 0; k < n_corpus; k++)
        engine_ingest_text(&eng, corpus[k], corpus[k]);

    int total_ticks = 40 * (int)SUBSTRATE_INT;
    int64_t total_error = 0;
    int crystal_cycles = 0;

    /* Escape probability accumulators */
    int uncoupled_total = 0, alive_total = 0, cycles = 0;

    for (int t = 0; t < total_ticks; t++) {
        engine_tick(&eng);
        if ((t + 1) % SUBSTRATE_INT == 0) {
            total_error += abs(eng.onetwo.feedback[7]);

            Graph *g = &eng.shells[0].g;

            /* Count crystals */
            for (int i = 0; i < g->n_nodes; i++) {
                Node *n = &g->nodes[i];
                if (n->alive && !n->layer_zero && n->valence >= 200) crystal_cycles++;
            }

            /* Escape probability: count uncoupled nodes */
            int n_in[MAX_NODES]; memset(n_in, 0, g->n_nodes * (int)sizeof(int));
            for (int e = 0; e < g->n_edges; e++)
                if (g->edges[e].weight > 0) n_in[g->edges[e].dst]++;

            int uncoupled = 0, alive = 0;
            for (int i = 0; i < g->n_nodes; i++) {
                Node *n = &g->nodes[i];
                if (!n->alive || n->layer_zero) continue;
                alive++;
                if (n_in[i] == 0) uncoupled++;
            }
            uncoupled_total += uncoupled;
            alive_total += alive;
            cycles++;
        }
    }

    /* Final topology snapshot */
    int n_alive = 0, n_relay = 0, n_collision = 0, n_crystal_final = 0;
    int total_valence = 0, n_incoherent = 0, total_edges = 0;
    Graph *gf = &eng.shells[0].g;
    int n_in_f[MAX_NODES]; memset(n_in_f, 0, gf->n_nodes * (int)sizeof(int));
    for (int e = 0; e < gf->n_edges; e++)
        if (gf->edges[e].weight > 0) { n_in_f[gf->edges[e].dst]++; total_edges++; }

    for (int i = 0; i < gf->n_nodes; i++) {
        Node *n = &gf->nodes[i];
        if (!n->alive || n->layer_zero) continue;
        n_alive++;
        total_valence += n->valence;
        if (n->coherent < 0) n_incoherent++;
        if (n->valence >= 200) n_crystal_final++;
        else if (n_in_f[i] >= 2) n_collision++;
        else n_relay++;
    }

    double uncoupled_frac = alive_total > 0 ? (double)uncoupled_total / (double)alive_total : 0.0;

    printf("SWEEP\t%d\t%d\t%d\t%d\t%d\t%lld\t%d\t%d\t%.4f\t%d\t%d\t%d\n",
           (int)SUBSTRATE_INT, n_alive, n_relay, n_collision, n_crystal_final,
           (long long)total_error, total_valence, crystal_cycles, uncoupled_frac,
           gf->grow_interval, gf->prune_interval, total_edges);

    engine_destroy(&eng);
}

static void cmd_test(void) {
    printf("=== XYZT PC Self-Test ===\n\n");

    run_core_tests();
    run_gpu_tests();
    run_lifecycle_tests();
    run_observer_tests();
    run_stress_tests();
    run_sense_tests();
    run_collision_tests();
    run_t3_stage1_tests();
    run_t3_full_tests();
    run_save_load_tests();
    run_yee_save_load_tests();
    run_tline_tests();
    run_child_conflict_tests();
    run_external_tests();
    run_lfield_test();
    run_stress_system_tests();

    printf("\n=== RESULTS: %d passed, %d failed, %d total ===\n",
           g_pass, g_fail, g_pass + g_fail);
    if (g_fail == 0) printf("ALL PASS.\n");

    /* Z-axis diagnostic: does the thermodynamic arrow exist?
     * Uses T3-style diverse zones to create genuine asymmetry.
     * Zone E (chimera) contains Zone A's header — contain(A,E) should be
     * high but contain(E,A) should be low, creating unidirectional edges. */
    {
        /* Zone constants (from test_t3_full.c) */
        static const uint8_t zd_header[128] = {
            0x55, 0xAA, 0xA0, 0x01, 0x00, 0x80, 0x30, 0x20,
            0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
            0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
            0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
            0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F,
            0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
            0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
            0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
            0x48, 0x49, 0x4A, 0x4B, 0x4C, 0x4D, 0x4E, 0x4F,
            0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
            0x58, 0x59, 0x5A, 0x5B, 0x5C, 0x5D, 0x5E, 0x5F,
            0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
            0x68, 0x69, 0x6A, 0x6B, 0x6C, 0x6D, 0x6E, 0x6F,
            0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
            0x78, 0x79, 0x7A, 0x7B, 0x7C, 0x7D, 0x7E, 0x7F,
            0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87
        };
        static const uint8_t zd_sync[16] = {
            0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE,
            0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
        };

        #define ZD_PER_ZONE 10
        #define ZD_TOTAL (ZD_PER_ZONE * 5)
        Engine eng;
        engine_init(&eng);
        Graph *g0 = &eng.shells[0].g;
        uint8_t buf[512]; BitStream bs;
        int zone_ids[ZD_TOTAL];
        char name[16];
        const char *zone_names[] = {"crash", "prime", "telem", "ascii", "chimera"};

        /* Zone A: crashing process — shared header + opposing payloads */
        for (int i = 0; i < ZD_PER_ZONE; i++) {
            memcpy(buf, zd_header, 128);
            for (int b = 128; b < 512; b++)
                buf[b] = (uint8_t)(i < ZD_PER_ZONE/2
                    ? (0x40 + i + ((b - 128) & 0x3F))
                    : (0xF0 - i - ((b - 128) & 0x3F)));
            encode_bytes(&bs, buf, 256);
            snprintf(name, sizeof(name), "zA_%d", i);
            zone_ids[i] = engine_ingest(&eng, name, &bs);
        }
        /* Zone B: stable daemon — prime-modular patterns */
        {
            int primes[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29};
            for (int i = 0; i < ZD_PER_ZONE; i++) {
                int p = primes[i];
                for (int b = 0; b < 512; b++)
                    buf[b] = (uint8_t)((b % p) * (256 / p));
                encode_bytes(&bs, buf, 256);
                snprintf(name, sizeof(name), "zB_%d", i);
                zone_ids[ZD_PER_ZONE + i] = engine_ingest(&eng, name, &bs);
            }
        }
        /* Zone C: telemetry — protocol frames with sync header */
        for (int i = 0; i < ZD_PER_ZONE; i++) {
            memcpy(buf, zd_sync, 16);
            buf[16] = (uint8_t)i;
            buf[17] = (uint8_t)(i / 8);
            for (int b = 18; b < 504; b++)
                buf[b] = (uint8_t)(0x40 + ((i * 7 + b * 3) % 33));
            for (int b = 504; b < 508; b++)
                buf[b] = (uint8_t)(buf[b - 486] ^ buf[b - 243]);
            buf[508] = 0xFE; buf[509] = 0xED;
            buf[510] = 0xFA; buf[511] = 0xCE;
            encode_bytes(&bs, buf, 256);
            snprintf(name, sizeof(name), "zC_%d", i);
            zone_ids[ZD_PER_ZONE*2 + i] = engine_ingest(&eng, name, &bs);
        }
        /* Zone D: ASCII log — text-like byte distribution */
        for (int i = 0; i < ZD_PER_ZONE; i++) {
            uint32_t seed = (uint32_t)(i * 31337 + 42);
            for (int b = 0; b < 512; b++) {
                seed = seed * 1103515245 + 12345;
                uint8_t val = (uint8_t)(0x20 + ((seed >> 16) % 95));
                if ((b % 7) == (int)(i % 7)) val = 0x20;
                if ((b % 60) == 0 && b > 0) val = 0x0A;
                buf[b] = val;
            }
            encode_bytes(&bs, buf, 256);
            snprintf(name, sizeof(name), "zD_%d", i);
            zone_ids[ZD_PER_ZONE*3 + i] = engine_ingest(&eng, name, &bs);
        }
        /* Zone E: chimera — contains Zone A header + bits of B, C, D */
        for (int i = 0; i < ZD_PER_ZONE; i++) {
            memcpy(buf, zd_header, 128);
            int p = 2 + (i % 20);
            for (int b = 128; b < 256; b++)
                buf[b] = (uint8_t)((b % p) * (256 / p));
            memcpy(buf + 256, zd_sync, 16);
            for (int b = 272; b < 384; b++)
                buf[b] = (uint8_t)(0x40 + ((i * 5 + b * 3) % 33));
            uint32_t seed = (uint32_t)(i * 31337 + 42);
            for (int b = 384; b < 512; b++) {
                seed = seed * 1103515245 + 12345;
                buf[b] = (uint8_t)(0x20 + ((seed >> 16) % 95));
                if ((b % 7) == (int)(i % 7)) buf[b] = 0x20;
            }
            encode_bytes(&bs, buf, 256);
            snprintf(name, sizeof(name), "zE_%d", i);
            zone_ids[ZD_PER_ZONE*4 + i] = engine_ingest(&eng, name, &bs);
        }

        /* Let graph grow — need enough ticks for edges + topology computation */
        for (int t = 0; t < (int)SUBSTRATE_INT * 10; t++) engine_tick(&eng);
        graph_compute_topology(g0, 0);

        /* max_y (sequence depth) */
        int max_y = 0;
        for (int i = 0; i < g0->n_nodes; i++) {
            int y = coord_y(g0->nodes[i].coord);
            if (y > max_y) max_y = y;
        }

        /* bidir vs unidir edge counts */
        int bidir_count = 0, unidir_count = 0;
        for (int e = 0; e < g0->n_edges; e++) {
            if (g0->edges[e].weight == 0) continue;
            int d = g0->edges[e].dst, sa = g0->edges[e].src_a;
            int has_reverse = 0;
            for (int j = 0; j < g0->n_edges; j++) {
                if (j == e || g0->edges[j].weight == 0) continue;
                if (g0->edges[j].src_a == d && g0->edges[j].dst == sa) { has_reverse = 1; break; }
            }
            if (has_reverse) bidir_count++; else unidir_count++;
        }

        /* Per-zone average Y (sequence depth) */
        int zone_y_sum[5] = {0}, zone_y_cnt[5] = {0};
        for (int z = 0; z < 5; z++) {
            for (int i = 0; i < ZD_PER_ZONE; i++) {
                int id = zone_ids[z * ZD_PER_ZONE + i];
                if (id >= 0 && id < g0->n_nodes && g0->nodes[id].alive) {
                    zone_y_sum[z] += coord_y(g0->nodes[id].coord);
                    zone_y_cnt[z]++;
                }
            }
        }

        /* Containment asymmetry: A[0] vs E[0] */
        int contain_ae = 0, contain_ea = 0;
        if (zone_ids[0] >= 0 && zone_ids[0] < g0->n_nodes &&
            zone_ids[ZD_PER_ZONE*4] >= 0 && zone_ids[ZD_PER_ZONE*4] < g0->n_nodes) {
            contain_ae = bs_contain(&g0->nodes[zone_ids[0]].identity,
                                    &g0->nodes[zone_ids[ZD_PER_ZONE*4]].identity);
            contain_ea = bs_contain(&g0->nodes[zone_ids[ZD_PER_ZONE*4]].identity,
                                    &g0->nodes[zone_ids[0]].identity);
        }

        printf("\n=== TYXZT TOPOLOGY DIAGNOSTIC ===\n");
        printf("  max_y (sequence depth) = %d\n", max_y);
        printf("  bidir edges: %d, unidir edges: %d\n", bidir_count, unidir_count);
        for (int z = 0; z < 5; z++) {
            float avg = zone_y_cnt[z] > 0 ? (float)zone_y_sum[z] / zone_y_cnt[z] : 0.0f;
            printf("  Zone %c(%s): avg_y = %.2f\n",
                   'A' + z, zone_names[z], avg);
        }
        printf("  contain(A0,E0) = %d, contain(E0,A0) = %d  (delta=%d)\n",
               contain_ae, contain_ea,
               contain_ae > contain_ea ? contain_ae - contain_ea : contain_ea - contain_ae);
        printf("  Y (sequence) alive: %s\n", max_y > 0 ? "YES" : "NO -- directed edges not producing Y separation");
        engine_destroy(&eng);
    }

    /* Child learning diagnostic */
    {
        Engine eng;
        engine_init(&eng);
        uint8_t buf[512]; BitStream bs;
        for (int i = 0; i < 8; i++) {
            for (int b = 0; b < 512; b++) buf[b] = (uint8_t)(i * 31 + b * 7);
            encode_bytes(&bs, buf, 256);
            char name[16]; snprintf(name, sizeof(name), "cl_%d", i);
            engine_ingest(&eng, name, &bs);
        }
        for (int t = 0; t < (int)SUBSTRATE_INT * 15; t++) engine_tick(&eng);
        printf("\n=== CHILD LEARNING DIAGNOSTIC ===\n");
        printf("  n_children = %d\n", eng.n_children);
        for (int i = 0; i < MAX_CHILDREN; i++) {
            if (eng.child_owner[i] < 0) continue;
            Graph *c = &eng.child_pool[i];
            printf("  child[%d]: owner=%d ticks=%llu learns=%llu edges=%d "
                   "grown=%llu pruned=%llu err_accum=%d drive=%d hb=%d\n",
                   i, eng.child_owner[i],
                   (unsigned long long)c->total_ticks,
                   (unsigned long long)c->total_learns,
                   c->n_edges, (unsigned long long)c->total_grown,
                   (unsigned long long)c->total_pruned,
                   c->error_accum, c->drive, c->local_heartbeat);
        }
        printf("  Children alive: %s\n",
               (eng.n_children > 0 && eng.child_pool[0].total_learns > 0)
               ? "YES" : "NO");
        printf("  Parent SPRT: error_accum=%d heartbeats=%d drive=%d\n",
               eng.shells[0].g.error_accum, eng.shells[0].g.local_heartbeat,
               eng.shells[0].g.drive);
        engine_destroy(&eng);
    }
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

    /* Yee substrate setup */
    int gpu_ok = (yee_init() == 0);
    uint8_t *yee_substrate = NULL;
    if (gpu_ok) {
        yee_substrate = (uint8_t *)calloc(YEE_N, 1);
        wire_yee_retinas(&eng, yee_substrate);
        printf("Yee substrate: 64x64x64 (%d voxels) initialized.\n\n", YEE_N);
    } else {
        printf("WARNING: Yee init failed. Running CPU-only (retina will use fallback).\n\n");
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
        encode_bytes(&bs, bin_data, 256);
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

    /* Initial injection into Yee substrate */
    if (gpu_ok) {
        wire_engine_to_yee(&eng);
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
            wire_engine_to_yee(&eng);
            yee_tick_async();     /* launch GPU, don't wait */
        }
        engine_tick(&eng);        /* CPU works while GPU runs */
        if (gpu_ok) yee_sync();   /* wait for GPU before next inject */

        if (gpu_ok && eng.total_ticks % SUBSTRATE_INT == 0) {
            yee_download_acc(yee_substrate, YEE_N);
            wire_yee_retinas(&eng, yee_substrate);
            wire_yee_to_engine(&eng);
            sense_feedback(&eng, &eng.last_sense);
            yee_hebbian(0.01f, 0.005f);
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
        free(yee_substrate);
        if (gpu_ok) yee_destroy();
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
            wire_engine_to_yee(&eng);
            yee_tick_async();
        }
        engine_tick(&eng);
        if (gpu_ok) yee_sync();

        if (gpu_ok && eng.total_ticks % SUBSTRATE_INT == 0) {
            yee_download_acc(yee_substrate, YEE_N);
            wire_yee_retinas(&eng, yee_substrate);
            wire_yee_to_engine(&eng);
            sense_feedback(&eng, &eng.last_sense);
            yee_hebbian(0.01f, 0.005f);
        }

        if (eng.n_children > spawned) {
            printf("  evolve tick %d: new child (%d total)\n", t, eng.n_children);
            spawned = eng.n_children;
        }
    }

    /* Final Yee sync */
    if (gpu_ok) {
        yee_download_acc(yee_substrate, YEE_N);
        wire_yee_retinas(&eng, yee_substrate);
        wire_yee_to_engine(&eng);
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
        printf("  Check: wire_yee_retinas(), substrate values at parent coords,\n");
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

    /* Full engine report (no CubeState in Yee path) */
    report_full(&eng, NULL, 0);

    free(yee_substrate);
    if (gpu_ok) yee_destroy();
    engine_destroy(&eng);
}

/* ══════════════════════════════════════════════════════════════
 * INTERACTIVE MODE
 * ══════════════════════════════════════════════════════════════ */

static void cmd_run(void) {
    printf("=== XYZT PC — Interactive Mode ===\n");
    printf("Commands: ingest <text>, tick [N], status, query <text>, save <path>, load <path>, quit\n\n");

    Engine eng;
    engine_init(&eng);

    int gpu_ok = (yee_init() == 0);
    uint8_t *yee_substrate = NULL;
    if (gpu_ok) {
        yee_substrate = (uint8_t *)calloc(YEE_N, 1);
        wire_yee_retinas(&eng, yee_substrate);
        printf("Yee substrate: 64x64x64 (%d voxels)\n\n", YEE_N);
    } else {
        printf("Yee init failed, running CPU-only\n\n");
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
                yee_download_acc(yee_substrate, YEE_N);
                wire_yee_retinas(&eng, yee_substrate);
            }
            report_full(&eng, NULL, 0);
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

            /* Inject into Yee */
            if (gpu_ok) {
                wire_engine_to_yee(&eng);
            }
        }
        else if (strncmp(line, "tick", 4) == 0) {
            int n = 1;
            if (len > 5) n = atoi(line + 5);
            if (n < 1) n = 1;
            if (n > 100000) n = 100000;

            for (int i = 0; i < n; i++) {
                if (gpu_ok) {
                    wire_engine_to_yee(&eng);
                    yee_tick_async();
                }
                engine_tick(&eng);
                if (gpu_ok) yee_sync();

                if (gpu_ok && eng.total_ticks % SUBSTRATE_INT == 0) {
                    yee_download_acc(yee_substrate, YEE_N);
                    wire_yee_retinas(&eng, yee_substrate);
                    wire_yee_to_engine(&eng);
                    yee_hebbian(0.01f, 0.005f);
                }
            }

            /* Final sync at end of batch */
            if (gpu_ok) {
                yee_download_acc(yee_substrate, YEE_N);
                wire_yee_retinas(&eng, yee_substrate);
                wire_yee_to_engine(&eng);
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
                /* Long query: raw byte containment (same encoding as ingest) */
                BitStream bs;
                encode_bytes(&bs, (const uint8_t *)qtext, qlen);
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

    free(yee_substrate);
    if (gpu_ok) yee_destroy();
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

/* ══════════════════════════════════════════════════════════════
 * INSPECT MODE — dump topology of a loaded save file
 * ══════════════════════════════════════════════════════════════ */

static void cmd_inspect(const char *path) {
    printf("=== INSPECT: %s ===\n\n", path);

    Engine eng;
    engine_init(&eng);
    if (yee_init() != 0) { printf("Yee init failed\n"); engine_destroy(&eng); return; }

    int lr = engine_load(&eng, path);
    if (lr != 0) { printf("Load failed (%d)\n", lr); yee_destroy(); engine_destroy(&eng); return; }

    Graph *g0 = &eng.shells[0].g;
    printf("Loaded: %d nodes, %d edges, %d children, %llu ticks\n\n",
           g0->n_nodes, g0->n_edges, eng.n_children,
           (unsigned long long)eng.total_ticks);

    /* ── Stream observation nodes (s_*) ── */
    printf("─── OBSERVATION NODES (s_*) ───\n");
    printf("%-6s %-20s %6s %4s %5s %3s %3s %3s\n",
           "ID", "Name", "Val", "Vnc", "Cryst", "X", "Y", "Z");

    float *h_L = (float *)malloc(YEE_N * sizeof(float));
    yee_download_L(h_L, YEE_N);

    int obs_ids[512];
    int n_obs = 0;
    for (int i = 0; i < g0->n_nodes; i++) {
        Node *n = &g0->nodes[i];
        if (!n->alive) continue;
        if (n->name[0] == 's' && n->name[1] == '_') {
            int gx = coord_x(n->coord) % 64;
            int gy = coord_y(n->coord) % 64;
            int gz = coord_z(n->coord) % 64;
            int vid = gx + gy * 64 + gz * 64 * 64;
            printf("%-6d %-20s %6d %4d %5d %3d %3d %3d  L=%.4f\n",
                   i, n->name, n->val, (int)n->valence,
                   crystal_strength(n), gx, gy, gz, h_L[vid]);
            if (n_obs < 512) obs_ids[n_obs++] = i;
        }
    }
    printf("  Total observation nodes: %d\n\n", n_obs);

    /* ── Children ── */
    printf("─── CHILDREN ───\n");
    for (int c = 0; c < MAX_CHILDREN; c++) {
        if (eng.child_owner[c] < 0) continue;
        int owner = eng.child_owner[c];
        Graph *child = &eng.child_pool[c];
        Node *parent = &g0->nodes[owner];
        printf("  child[%d]:\n", c);
        printf("    parent: node %d '%s' val=%d valence=%d crystal=%d\n",
               owner, parent->name, parent->val, (int)parent->valence,
               crystal_strength(parent));
        printf("    topology: %d nodes, %d edges, %llu ticks\n",
               child->n_nodes, child->n_edges,
               (unsigned long long)child->total_ticks);
        printf("    drive=%d err_acc=%d heartbeat=%d\n",
               child->drive, child->error_accum, child->local_heartbeat);
        printf("    learns=%llu grown=%llu pruned=%llu\n",
               (unsigned long long)child->total_learns,
               (unsigned long long)child->total_grown,
               (unsigned long long)child->total_pruned);
        if (child->retina && child->retina_len >= 64) {
            printf("    retina=[");
            for (int r = 0; r < 8 && r < child->n_nodes; r++) {
                printf("%d", child->nodes[r].val);
                if (r < 7) printf(",");
            }
            printf("]\n");
        }
    }

    /* ── Strongest edges between observation nodes ── */
    printf("\n─── TOP 20 EDGES (by weight) ───\n");
    typedef struct { int eid; int w; } EW;
    EW top_edges[20];
    int n_top = 0;
    for (int e = 0; e < g0->n_edges; e++) {
        Edge *ed = &g0->edges[e];
        if (ed->weight == 0) continue;
        int w = ed->weight;
        if (n_top < 20) {
            top_edges[n_top].eid = e; top_edges[n_top].w = w; n_top++;
        } else {
            int min_k = 0;
            for (int k = 1; k < 20; k++)
                if (top_edges[k].w < top_edges[min_k].w) min_k = k;
            if (w > top_edges[min_k].w) {
                top_edges[min_k].eid = e; top_edges[min_k].w = w;
            }
        }
    }
    /* Sort descending */
    for (int i = 0; i < n_top - 1; i++)
        for (int j = i + 1; j < n_top; j++)
            if (top_edges[j].w > top_edges[i].w) {
                EW tmp = top_edges[i]; top_edges[i] = top_edges[j]; top_edges[j] = tmp;
            }
    for (int i = 0; i < n_top; i++) {
        Edge *ed = &g0->edges[top_edges[i].eid];
        printf("  w=%3d  '%s' + '%s' -> '%s'%s\n",
               ed->weight,
               g0->nodes[ed->src_a].name, g0->nodes[ed->src_b].name,
               g0->nodes[ed->dst].name,
               (ed->invert_a || ed->invert_b) ? " [INVERTED]" : "");
    }

    /* ── L-field at observation positions ── */
    printf("\n─── L-FIELD AT OBSERVATIONS ───\n");
    float l_min = 999, l_max = 0;
    for (int o = 0; o < n_obs; o++) {
        Node *n = &g0->nodes[obs_ids[o]];
        int gx = coord_x(n->coord) % 64;
        int gy = coord_y(n->coord) % 64;
        int gz = coord_z(n->coord) % 64;
        int vid = gx + gy * 64 + gz * 64 * 64;
        float l = h_L[vid];
        if (l < l_min) l_min = l;
        if (l > l_max) l_max = l;
    }
    printf("  Observation L range: [%.4f, %.4f]\n", l_min, l_max);

    /* Global L stats for comparison */
    float gl_min = 999, gl_max = 0, gl_mean = 0;
    int gl_wire = 0;
    for (int i = 0; i < YEE_N; i++) {
        gl_mean += h_L[i];
        if (h_L[i] < gl_min) gl_min = h_L[i];
        if (h_L[i] > gl_max) gl_max = h_L[i];
        if (h_L[i] < 0.95f) gl_wire++;
    }
    gl_mean /= YEE_N;
    printf("  Global L: mean=%.4f range=[%.4f, %.4f] wire=%d\n\n",
           gl_mean, gl_min, gl_max, gl_wire);

    /* ── Crystallized nodes (highest valence) ── */
    printf("─── CRYSTALLIZED NODES (valence >= 200) ───\n");
    int n_crystal = 0;
    for (int i = 0; i < g0->n_nodes; i++) {
        Node *n = &g0->nodes[i];
        if (!n->alive || n->layer_zero) continue;
        if (n->valence >= 200) {
            printf("  [%d] %-30s valence=%d val=%d crystal=%d\n",
                   i, n->name, (int)n->valence, n->val, crystal_strength(n));
            n_crystal++;
        }
    }
    printf("  Total crystallized: %d\n", n_crystal);

    free(h_L);
    yee_destroy();
    engine_destroy(&eng);
}

/* ══════════════════════════════════════════════════════════════
 * STREAM MODE — continuous stdin ingestion + Yee visualization
 * ══════════════════════════════════════════════════════════════ */

static int g_is_console = 0;  /* set once in cmd_stream */

static void viz_yee_slice(void) {
    /* 64x32 character grid: z=32 slice, y stepped by 2 for aspect ratio */
    static float *h_V = NULL;
    if (!h_V) h_V = (float *)malloc(YEE_N * sizeof(float));
    if (!h_V) return;
    yee_download_V(h_V, YEE_N);

    int gz = 32;
    if (g_is_console) printf("\033[H");  /* ANSI home — overwrite in place */
    for (int gy = 0; gy < YEE_GY; gy += 2) {
        for (int gx = 0; gx < YEE_GX; gx++) {
            float v = h_V[gx + gy * YEE_GX + gz * YEE_GX * YEE_GY];
            char c = ' ';
            if (v > 0.5f)       c = '@';
            else if (v > 0.1f)  c = '+';
            else if (v < -0.5f) c = '#';
            else if (v < -0.1f) c = '-';
            putchar(c);
        }
        putchar('\n');
    }
}

static void cmd_stream(int binary_mode) {
    Engine eng;
    engine_init(&eng);

    if (yee_init() != 0) {
        printf("Yee init failed\n");
        engine_destroy(&eng);
        return;
    }

    StreamContext sctx;
    if (io_init(&sctx) != 0) {
        printf("IO init failed\n");
        yee_destroy();
        engine_destroy(&eng);
        return;
    }

    g_is_console = io_stdout_is_console();
    if (g_is_console) printf("\033[2J");  /* clear screen */
    printf("=== STREAM MODE (%s) ===\n", binary_mode ? "binary" : "text");

    char line_buf[STREAM_WINDOW + 1];
    uint64_t obs_count = 0;
    uint64_t viz_interval = 50;

    while (!io_eof(&sctx)) {
        int n_read = 0;

        if (binary_mode) {
            /* Binary: overwrite one persistent node */
            if (io_has_data(&sctx)) {
                n_read = io_read_raw(&sctx, line_buf, STREAM_WINDOW);
                if (n_read > 0) {
                    BitStream bs;
                    encode_bytes(&bs, (uint8_t *)line_buf, n_read);
                    engine_ingest(&eng, "stream_0", &bs);
                    obs_count++;
                }
            }
        } else {
            /* Text: each line becomes a new node */
            if (io_has_data(&sctx)) {
                n_read = io_read_line(&sctx, line_buf, STREAM_WINDOW);
                if (n_read > 0) {
                    line_buf[n_read] = '\0';
                    char name[64];
                    snprintf(name, sizeof(name), "s_%06llu",
                             (unsigned long long)eng.total_ticks);
                    BitStream bs;
                    encode_bytes(&bs, (uint8_t *)line_buf, n_read);
                    engine_ingest(&eng, name, &bs);
                    obs_count++;
                }
            }
        }

        engine_tick(&eng);

        /* Visualization every viz_interval ticks */
        if (eng.total_ticks % viz_interval == 0) {
            viz_yee_slice();
            Graph *g0 = &eng.shells[0].g;
            printf("tick=%llu  obs=%llu  nodes=%d  edges=%d  children=%d  error=%d\n",
                   (unsigned long long)eng.total_ticks,
                   (unsigned long long)obs_count,
                   g0->n_nodes, g0->n_edges,
                   eng.n_children, eng.graph_error);
        }

        /* Small yield when idle to avoid busy-wait */
        if (!io_has_data(&sctx) && !io_eof(&sctx))
            io_sleep_ms(1);
    }

    /* Drain: run 10 full SUBSTRATE_INT cycles after EOF
     * to let Hebbian carve the L-field from accumulated activity */
    printf("\n  Post-EOF: running %d Hebbian cycles...\n", 10);
    for (int cycle = 0; cycle < 10; cycle++) {
        for (int t = 0; t < (int)SUBSTRATE_INT; t++) {
            wire_engine_to_yee(&eng);
            yee_tick_async();
            engine_tick(&eng);
            yee_sync();
        }
        /* Hebbian fires at end of each cycle */
        {
            uint8_t *drain_sub = (uint8_t *)calloc(YEE_N, 1);
            yee_download_acc(drain_sub, YEE_N);
            wire_yee_retinas(&eng, drain_sub);
            wire_yee_to_engine(&eng);
            yee_hebbian(0.01f, 0.005f);
            free(drain_sub);
        }
        if (g_is_console && eng.total_ticks % viz_interval == 0)
            viz_yee_slice();
    }

    Graph *g0 = &eng.shells[0].g;
    printf("\n=== STREAM COMPLETE ===\n");
    printf("  observations: %llu\n", (unsigned long long)obs_count);
    printf("  total ticks:  %llu\n", (unsigned long long)eng.total_ticks);
    printf("  nodes: %d  edges: %d  children: %d\n",
           g0->n_nodes, g0->n_edges, eng.n_children);

    /* Save if anything was ingested */
    if (obs_count > 0) {
        engine_save(&eng, "stream_output.xyzt");
        printf("  saved: stream_output.xyzt\n");
    }

    io_destroy(&sctx);
    yee_destroy();
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
    } else if (strcmp(argv[1], "sweep") == 0) {
        cmd_sweep();
    } else if (strcmp(argv[1], "tracking") == 0) {
        run_tracking_sweep();
    } else if (strcmp(argv[1], "sense-diag") == 0) {
        run_sense_diagnostic();
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
    } else if (strcmp(argv[1], "cortex") == 0) {
        /* Clean REPL: ingest → tick → query → print */
        Cortex ctx;
        if (cortex_init(&ctx) != 0) { printf("Cortex init failed\n"); return 1; }

        /* Load saved state if provided */
        if (argc >= 3) {
            if (cortex_load(&ctx, argv[2]) == 0)
                printf("Loaded: %s\n", argv[2]);
        }

        printf("=== CORTEX ===\n");
        printf("Commands: ingest <text>, tick [N], query <text>, save <path>, load <path>, quit\n\n");

        char line[4096];
        while (1) {
            printf("cortex> "); fflush(stdout);
            if (!fgets(line, sizeof(line), stdin)) break;
            int len = (int)strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = 0;
            if (len == 0) continue;
            if (strcmp(line, "quit") == 0) break;

            if (strncmp(line, "ingest ", 7) == 0) {
                char name[64];
                snprintf(name, 64, "c_%06llu", (unsigned long long)ctx.n_ingested);
                int id = cortex_ingest(&ctx, name, (const uint8_t *)(line + 7), len - 7);
                printf("  node %d '%s'\n", id, name);
            } else if (strncmp(line, "tick", 4) == 0) {
                int n = 1;
                if (len > 5) n = atoi(line + 5);
                if (n < 1) n = 1; if (n > 100) n = 100;
                cortex_tick(&ctx, n);
                Graph *g = &ctx.eng.shells[0].g;
                printf("  %d cycles. nodes=%d edges=%d children=%d\n",
                       n, g->n_nodes, g->n_edges, ctx.eng.n_children);
            } else if (strncmp(line, "query ", 6) == 0) {
                InferResult res[10];
                int n = cortex_query(&ctx, line + 6, res, 10);
                if (n == 0) printf("  No resonance.\n");
                for (int i = 0; i < n; i++)
                    printf("  [%d] %-25s energy=%.4f crystal=%d z3=%d z4=%d\n",
                           res[i].node_id, res[i].name, res[i].energy,
                           res[i].crystal, res[i].z3_freq, res[i].z4_corr);
            } else if (strncmp(line, "save ", 5) == 0) {
                cortex_save(&ctx, line + 5);
                printf("  Saved.\n");
            } else if (strncmp(line, "load ", 5) == 0) {
                cortex_load(&ctx, line + 5);
                printf("  Loaded.\n");
            } else {
                printf("  Unknown. Try: ingest, tick, query, save, load, quit\n");
            }
        }
        cortex_destroy(&ctx);
    } else if (strcmp(argv[1], "infer") == 0 && argc >= 3) {
        /* Load saved state and run wave-based inference queries */
        Engine eng;
        engine_init(&eng);
        if (yee_init() != 0) { printf("Yee init failed\n"); engine_destroy(&eng); return 1; }
        if (engine_load(&eng, argv[2]) != 0) {
            printf("Load failed: %s\n", argv[2]);
            yee_destroy(); engine_destroy(&eng); return 1;
        }
        printf("=== XYZT INFERENCE — %s ===\n", argv[2]);
        printf("  nodes=%d edges=%d children=%d\n",
               eng.shells[0].g.n_nodes, eng.shells[0].g.n_edges, eng.n_children);
        printf("  Commands: type a query, press enter. 'quit' to exit.\n\n");

        char qline[4096];
        while (1) {
            printf("infer> ");
            fflush(stdout);
            if (!fgets(qline, sizeof(qline), stdin)) break;
            int qlen = (int)strlen(qline);
            while (qlen > 0 && (qline[qlen-1] == '\n' || qline[qlen-1] == '\r')) qline[--qlen] = 0;
            if (qlen == 0) continue;
            if (strcmp(qline, "quit") == 0) break;

            InferResult results[INFER_MAX_RESULTS];
            int n = infer_query(&eng, qline, results, 10);

            if (n == 0) {
                printf("  No resonance. Query didn't reach any carved waveguides.\n\n");
            } else {
                printf("  %d nodes resonated:\n", n);
                for (int i = 0; i < n; i++) {
                    printf("  [%d] %-25s energy=%.4f val=%.4f crystal=%d z3=%d z4=%d\n",
                           results[i].node_id, results[i].name,
                           results[i].energy, results[i].val,
                           results[i].crystal, results[i].z3_freq, results[i].z4_corr);
                }
                printf("\n");
            }
        }

        yee_destroy();
        engine_destroy(&eng);
    } else if (strcmp(argv[1], "inspect") == 0 && argc >= 3) {
        cmd_inspect(argv[2]);
    } else if (strcmp(argv[1], "stream") == 0) {
        int binary = (argc >= 3 && strcmp(argv[2], "-b") == 0);
        cmd_stream(binary);
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

        int gpu_ok = (yee_init() == 0);

        int imp = engine_wire_import(&eng, wpath);
        if (imp <= 0) {
            printf("Import failed (%d)\n", imp);
            if (gpu_ok) yee_destroy();
            engine_destroy(&eng);
            return 1;
        }

        /* Run SUBSTRATE_INT ticks on Yee substrate */
        printf("Running %u ticks...\n", SUBSTRATE_INT);
        for (uint32_t i = 0; i < SUBSTRATE_INT; i++)
            engine_tick(&eng);

        engine_status(&eng);

        /* Export enriched graph */
        char out_path[512];
        snprintf(out_path, sizeof(out_path), "%s.enriched", wpath);
        engine_wire_export(&eng, out_path);
        printf("Enriched graph written to: %s\n", out_path);

        if (gpu_ok) yee_destroy();
        engine_destroy(&eng);
    } else {
        printf("Usage: xyzt_pc [run|test|bench|ingest <file>|t3|stream [-b]|exec <file.xyzt>|wire_import <path>|wire_export <path>|bridge [wire.bin]]\n");
        return 1;
    }

    return 0;
}
