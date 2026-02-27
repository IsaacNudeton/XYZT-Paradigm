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

static int g_pass = 0, g_fail = 0;
static void check(const char *name, int exp, int got) {
    if (exp == got) { g_pass++; }
    else { g_fail++; printf("  FAIL %s: expected %d, got %d\n", name, exp, got); }
}

static void test_engine_basic(void) {
    printf("--- Engine Basic ---\n");
    Engine eng;
    engine_init(&eng);

    /* Ingest some text */
    int id = engine_ingest_text(&eng, "hello", "hello world");
    check("ingest returns valid id", 1, id >= 0 ? 1 : 0);
    check("shell 0 has node", 1, eng.shells[0].g.n_nodes > 0 ? 1 : 0);
    check("shell 1 mirrors", 1, eng.shells[1].g.n_nodes > 0 ? 1 : 0);

    /* Run some ticks */
    for (int i = 0; i < 10; i++) engine_tick(&eng);
    check("engine ticked", 10, (int)eng.total_ticks);

    /* Query */
    BitStream bs;
    onetwo_parse((const uint8_t *)"hello world", 11, &bs);
    QueryResult results[5];
    int n = engine_query(&eng, &bs, results, 5);
    check("query finds result", 1, n > 0 ? 1 : 0);

    engine_destroy(&eng);
}

static void test_onetwo_encode(void) {
    printf("--- ONETWO Encode ---\n");
    BitStream bs;
    onetwo_parse((const uint8_t *)"hello world this is a test", 26, &bs);
    check("onetwo output length", OT_TOTAL, bs.len);
    check("onetwo has bits set", 1, bs_popcount(&bs) > 0 ? 1 : 0);

    /* Self-observe */
    BitStream self;
    onetwo_self_observe(&bs, &self);
    check("self-observe produces output", 1, bs_popcount(&self) > 0 ? 1 : 0);

    /* Two similar strings should have higher correlation than different ones */
    BitStream bs2, bs3;
    onetwo_parse((const uint8_t *)"hello world this is another test", 32, &bs2);
    onetwo_parse((const uint8_t *)"XXXXXXXXXXXXXXXXX", 17, &bs3);
    int corr_similar = bs_corr(&bs, &bs2);
    int corr_different = bs_corr(&bs, &bs3);
    check("similar > different", 1, corr_similar > corr_different ? 1 : 0);
}

static void test_gpu_boolean(void) {
    printf("--- GPU Boolean ---\n");
    int n_cubes = 4;
    if (substrate_init(n_cubes) != 0) {
        printf("  SKIP: GPU init failed\n");
        return;
    }

    /* Set up 4 cubes for (0,0), (0,1), (1,0), (1,1) */
    CubeState *h_cubes = (CubeState *)calloc(n_cubes, sizeof(CubeState));
    int idx = 0;
    for (int a = 0; a <= 1; a++) {
        for (int b = 0; b <= 1; b++) {
            /* Position 0 and 1 are marks, position 2 reads both */
            h_cubes[idx].substrate[0] = a ? 128 : 0;
            h_cubes[idx].substrate[1] = b ? 128 : 0;
            h_cubes[idx].reads[2] = (1ULL << 0) | (1ULL << 1);
            h_cubes[idx].active[2] = 1;
            idx++;
        }
    }

    substrate_upload(h_cubes, n_cubes);

    int *results = (int *)calloc(n_cubes * CUBE_SIZE, sizeof(int));

    /* Test OR (obs_any) */
    substrate_tick_and_observe(n_cubes, OBS_ANY, 2, results);
    idx = 0;
    for (int a = 0; a <= 1; a++) {
        for (int b = 0; b <= 1; b++) {
            char name[32];
            snprintf(name, 32, "GPU OR(%d,%d)", a, b);
            check(name, a | b, results[idx * CUBE_SIZE + 2]);
            idx++;
        }
    }

    /* Re-upload for AND test */
    substrate_upload(h_cubes, n_cubes);
    substrate_tick_and_observe(n_cubes, OBS_ALL, 2, results);
    idx = 0;
    for (int a = 0; a <= 1; a++) {
        for (int b = 0; b <= 1; b++) {
            char name[32];
            snprintf(name, 32, "GPU AND(%d,%d)", a, b);
            check(name, a & b, results[idx * CUBE_SIZE + 2]);
            idx++;
        }
    }

    /* Re-upload for XOR test */
    substrate_upload(h_cubes, n_cubes);
    substrate_tick_and_observe(n_cubes, OBS_PARITY, 2, results);
    idx = 0;
    for (int a = 0; a <= 1; a++) {
        for (int b = 0; b <= 1; b++) {
            char name[32];
            snprintf(name, 32, "GPU XOR(%d,%d)", a, b);
            check(name, a ^ b, results[idx * CUBE_SIZE + 2]);
            idx++;
        }
    }

    free(h_cubes);
    free(results);
    substrate_destroy();
}

static void test_transducer(void) {
    printf("--- Transducer ---\n");
    Transducer t;
    transducer_init(&t, XDUCE_RAW, NULL, 0);

    uint8_t raw[] = {10, 200, 50, 180, 30, 220, 100, 150};
    BitStream bs;
    transducer_to_bits(&t, raw, 8, &bs);
    check("transducer produces bits", 8, bs.len);
    check("threshold calibrated", 1, t.threshold > 0 ? 1 : 0);
    /* Values above threshold should be 1: 200, 180, 220, 150 */
    /* Values below threshold (115): 10, 50, 30, 100 */
    check("low value = 0", 0, bs_get(&bs, 0));   /* 10 */
    check("high value = 1", 1, bs_get(&bs, 1));   /* 200 */
}

static void test_wire_mapping(void) {
    printf("--- Wire Mapping ---\n");
    Engine eng;
    engine_init(&eng);
    engine_ingest_text(&eng, "test_node", "test data for wiring");

    CubeState *cubes = (CubeState *)calloc(8, sizeof(CubeState));
    wire_local_3d(cubes, 8);

    /* Check that positions have neighbors wired */
    /* Position (1,1,1) in cube 0 should have 6 neighbors */
    int center = local_idx(1, 1, 1);
    uint64_t rd = cubes[0].reads[center];
    int n_neighbors = xyzt_popcnt64(rd);
    check("center has 6 neighbors", 6, n_neighbors);

    /* Corner position (0,0,0) should have 3 neighbors */
    int corner = local_idx(0, 0, 0);
    rd = cubes[0].reads[corner];
    n_neighbors = xyzt_popcnt64(rd);
    check("corner has 3 neighbors", 3, n_neighbors);

    free(cubes);
    engine_destroy(&eng);
}

static void test_nesting_retina(void) {
    printf("--- Nesting Retina ---\n");
    Engine eng;
    engine_init(&eng);

    /* Ingest two different texts */
    engine_ingest_text(&eng, "alpha", "alpha data for testing the retina injection pathway");
    engine_ingest_text(&eng, "beta", "beta data completely different content for retina test");

    /* Force crystallization by maxing valence */
    Graph *g0 = &eng.shells[0].g;
    for (int i = 0; i < g0->n_nodes; i++) {
        if (g0->nodes[i].alive && !g0->nodes[i].layer_zero)
            g0->nodes[i].valence = 255;
    }

    /* Tick past SUBSTRATE_INT to trigger nest_check */
    for (int i = 0; i <= (int)SUBSTRATE_INT; i++) engine_tick(&eng);

    check("children spawned", 1, eng.n_children > 0 ? 1 : 0);

    if (eng.n_children > 0) {
        int slot = -1;
        for (int i = 0; i < MAX_CHILDREN; i++)
            if (eng.child_owner[i] >= 0) { slot = i; break; }
        if (slot >= 0) {
            check("child has 9 nodes", 9, eng.child_pool[slot].n_nodes);
            check("child has 4 edges", 4, eng.child_pool[slot].n_edges);
            check("child ticked", 1, eng.child_pool[slot].total_ticks > 0 ? 1 : 0);
            /* Output node is last (index 8) */
            check("output node alive", 1, eng.child_pool[slot].nodes[8].alive);
            check("retina nodes not layer_zero", 0, eng.child_pool[slot].nodes[0].layer_zero);
        }
    }

    engine_destroy(&eng);
}

static void cmd_test(void) {
    printf("=== XYZT PC Self-Test ===\n\n");

    /* ── T0: Constants ── */
    printf("--- Constants ---\n");
    check("SUBSTRATE_INT = 2^7 + 3^2", 137, SUBSTRATE_INT);
    check("MISMATCH_TAX_NUM", 81, MISMATCH_TAX_NUM);
    check("MISMATCH_TAX_DEN", 2251, MISMATCH_TAX_DEN);
    check("PRUNE_FLOOR", 9, PRUNE_FLOOR);
    check("CUBE_SIZE", 64, CUBE_SIZE);
    check("MAX_CHILDREN", 4, MAX_CHILDREN);

    test_engine_basic();
    test_onetwo_encode();
    test_gpu_boolean();
    test_transducer();
    test_wire_mapping();
    test_nesting_retina();

    /* ── T4: Layer Zero invariant ── */
    printf("--- Layer Zero Invariant ---\n");
    {
        Engine eng;
        engine_init(&eng);
        /* Before ingestion: add a raw node, it should be layer_zero=1 */
        int raw_id = graph_add(&eng.shells[0].g, "raw_node", 0, &eng.T);
        check("fresh node is layer_zero", 1, eng.shells[0].g.nodes[raw_id].layer_zero);
        /* After ingestion: node should lose layer_zero */
        int id = engine_ingest_text(&eng, "ingested", "hello world test data for layer zero");
        check("ingested node not layer_zero", 0, eng.shells[0].g.nodes[id].layer_zero);
        /* Un-ingested raw node should still be layer_zero */
        check("raw node still layer_zero", 1, eng.shells[0].g.nodes[raw_id].layer_zero);
        /* Layer zero nodes should not propagate (val stays 0 after ticks) */
        eng.shells[0].g.nodes[raw_id].val = 0;
        for (int i = 0; i < 50; i++) engine_tick(&eng);
        /* Raw node should not have received signal (no edges target it, and it's layer_zero) */
        check("layer_zero node val unchanged", 0, eng.shells[0].g.nodes[raw_id].val);
        engine_destroy(&eng);
    }

    /* ── T6: Save/Load round-trip ── */
    printf("--- Save/Load Round-trip ---\n");
    {
        Engine eng1, eng2;
        engine_init(&eng1);
        engine_ingest_text(&eng1, "persist_a", "data for persistence test alpha");
        engine_ingest_text(&eng1, "persist_b", "data for persistence test beta different content");
        for (int i = 0; i < 200; i++) engine_tick(&eng1);

        int save_ok = engine_save(&eng1, "test_roundtrip.xyzt");
        check("save succeeds", 0, save_ok);

        engine_init(&eng2);
        int load_ok = engine_load(&eng2, "test_roundtrip.xyzt");
        check("load succeeds", 0, load_ok);
        check("ticks preserved", (int)eng1.total_ticks, (int)eng2.total_ticks);
        check("n_shells preserved", eng1.n_shells, eng2.n_shells);
        check("shell0 n_nodes preserved", eng1.shells[0].g.n_nodes, eng2.shells[0].g.n_nodes);
        check("shell0 n_edges preserved", eng1.shells[0].g.n_edges, eng2.shells[0].g.n_edges);
        check("n_boundary preserved", eng1.n_boundary_edges, eng2.n_boundary_edges);
        /* Check a node survived */
        int found = graph_find(&eng2.shells[0].g, "persist_a");
        check("node persist_a survives load", 1, found >= 0 ? 1 : 0);

        engine_destroy(&eng1);
        engine_destroy(&eng2);
        remove("test_roundtrip.xyzt");
    }

    /* ── T27: MAX_CHILDREN pool limit ── */
    printf("--- MAX_CHILDREN Pool Limit ---\n");
    {
        Engine eng;
        engine_init(&eng);
        /* Ingest more than MAX_CHILDREN nodes and force crystallization */
        const char *names[] = {"mc_a", "mc_b", "mc_c", "mc_d", "mc_e", "mc_f"};
        const char *texts[] = {
            "alpha content for max children test with enough data",
            "beta content for max children test with enough data",
            "gamma content for max children test with enough data",
            "delta content for max children test with enough data",
            "epsilon content for max children test with enough data",
            "zeta content for max children test with enough data"
        };
        for (int i = 0; i < 6; i++)
            engine_ingest_text(&eng, names[i], texts[i]);
        /* Force all to crystallize */
        Graph *g0 = &eng.shells[0].g;
        for (int i = 0; i < g0->n_nodes; i++)
            if (g0->nodes[i].alive && !g0->nodes[i].layer_zero)
                g0->nodes[i].valence = 255;
        /* Tick past SUBSTRATE_INT to trigger nest_check */
        for (int i = 0; i <= (int)SUBSTRATE_INT; i++) engine_tick(&eng);
        /* Should cap at MAX_CHILDREN */
        check("children capped at MAX_CHILDREN", 1, eng.n_children <= MAX_CHILDREN ? 1 : 0);
        check("children exactly MAX_CHILDREN", MAX_CHILDREN, eng.n_children);
        engine_destroy(&eng);
    }

    /* ── Wire Import/Export round-trip ── */
    printf("--- Wire Import/Export ---\n");
    {
        Engine eng1;
        engine_init(&eng1);
        engine_ingest_text(&eng1, "wire_test_a", "alpha data for wire round trip test");
        engine_ingest_text(&eng1, "wire_test_b", "beta data for wire round trip test");
        /* Wire them */
        Graph *g0 = &eng1.shells[0].g;
        int a = graph_find(g0, "wire_test_a");
        int b = graph_find(g0, "wire_test_b");
        if (a >= 0 && b >= 0) graph_wire(g0, a, a, b, 200, 0);

        int exp_ok = engine_wire_export(&eng1, "test_wire_rt.bin");
        check("wire export returns nodes", 1, exp_ok > 0 ? 1 : 0);

        /* Verify file size = sizeof(TkWireGraph) = 155668 */
        FILE *wf = fopen("test_wire_rt.bin", "rb");
        check("wire export file exists", 1, wf != NULL ? 1 : 0);
        if (wf) {
            fseek(wf, 0, SEEK_END);
            long sz = ftell(wf);
            fclose(wf);
            check("wire export file size", 155668, (int)sz);
        }

        /* Import into fresh engine */
        Engine eng2;
        engine_init(&eng2);
        int imp_ok = engine_wire_import(&eng2, "test_wire_rt.bin");
        check("wire import returns nodes", 1, imp_ok > 0 ? 1 : 0);
        check("imported node found", 1, graph_find(&eng2.shells[0].g, "wire_test_a") >= 0 ? 1 : 0);

        engine_destroy(&eng1);
        engine_destroy(&eng2);
        remove("test_wire_rt.bin");
    }

    /* ── Exec: 2-bit adder ── */
    printf("--- Exec Assembly ---\n");
    {
        /* Write a minimal adder inline */
        FILE *ef = fopen("test_adder.xyzt", "w");
        if (ef) {
            fprintf(ef, "LATTICE 4 1 3\n");
            fprintf(ef, "SET 0 0 0 1\n");  /* A0=1 */
            fprintf(ef, "SET 1 0 0 1\n");  /* A1=1 */
            fprintf(ef, "SET 2 0 0 0\n");  /* B0=0 */
            fprintf(ef, "SET 3 0 0 1\n");  /* B1=1 */
            fprintf(ef, "XOR 0 0 0  2 0 0 -> 0 0 1\n");
            fprintf(ef, "AND 0 0 0  2 0 0 -> 1 0 1\n");
            fprintf(ef, "XOR 1 0 0  3 0 0 -> 2 0 1\n");
            fprintf(ef, "AND 1 0 0  3 0 0 -> 3 0 1\n");
            fprintf(ef, "RUN 1\n");
            fprintf(ef, "XOR 2 0 1  1 0 1 -> 0 0 2\n");
            fprintf(ef, "AND 2 0 1  1 0 1 -> 1 0 2\n");
            fprintf(ef, "OR  3 0 1  1 0 2 -> 2 0 2\n");
            fprintf(ef, "RUN 1\n");
            fclose(ef);
        }

        Engine eng;
        engine_init(&eng);
        int ex_ok = engine_exec(&eng, "test_adder.xyzt");
        check("exec succeeds", 0, ex_ok);
        engine_destroy(&eng);
        remove("test_adder.xyzt");
    }

    /* ── Tier 2: T1 Auto-wiring ────────────────────────────────── */
    printf("--- T1 Auto-wiring ---\n");
    {
        Engine eng; engine_init(&eng);
        int a = engine_ingest_text(&eng, "fw_a", "the cat sat on the warm soft mat by the door");
        int b = engine_ingest_text(&eng, "fw_b", "the cat ran to the warm soft hat by the door");
        for (int i = 0; i < 200; i++) engine_tick(&eng);
        Graph *g0 = &eng.shells[0].g;
        int e_ab = graph_find_edge(g0, a, b, a);
        if (e_ab < 0) e_ab = graph_find_edge(g0, b, a, b);
        check("auto-wired edge exists", 1, e_ab >= 0 ? 1 : 0);
        check("boundary edges created", 1, eng.n_boundary_edges > 0 ? 1 : 0);
        engine_destroy(&eng);
    }

    /* ── Tier 2: T3 Hebbian ──────────────────────────────────── */
    printf("--- T3 Hebbian ---\n");
    {
        Engine eng; engine_init(&eng);
        int ha = engine_ingest_text(&eng, "heb_a", "the quick brown fox jumps over the lazy dog near the river");
        int hb = engine_ingest_text(&eng, "heb_b", "the quick brown cat jumps over the lazy log near the river");
        int hc = engine_ingest_text(&eng, "heb_c", "0xDEADBEEF 10110101 binary noise random static burst xyz");
        Graph *g0 = &eng.shells[0].g;
        int e_sim = graph_find_edge(g0, ha, hb, ha);
        int e_diff = graph_find_edge(g0, ha, hc, ha);
        if (e_sim < 0) e_sim = graph_find_edge(g0, hb, ha, hb);
        if (e_diff < 0) e_diff = graph_find_edge(g0, hc, ha, hc);
        uint8_t w_sim_before = (e_sim >= 0) ? g0->edges[e_sim].weight : 0;
        uint8_t w_diff_before = (e_diff >= 0) ? g0->edges[e_diff].weight : 0;
        (void)w_sim_before; (void)w_diff_before;
        graph_learn(g0);
        uint8_t w_sim_after = (e_sim >= 0) ? g0->edges[e_sim].weight : 0;
        uint8_t w_diff_after = (e_diff >= 0) ? g0->edges[e_diff].weight : 0;
        check("similar edge exists", 1, e_sim >= 0 ? 1 : 0);
        check("hebbian: similar >= different", 1, w_sim_after >= w_diff_after ? 1 : 0);
        engine_destroy(&eng);
    }

    /* ── Tier 2: T10 Crystallization ─────────────────────────── */
    printf("--- T10 Crystallization ---\n");
    {
        Engine eng; engine_init(&eng);
        Graph *g0 = &eng.shells[0].g;
        int c1 = graph_add(g0, "crystal_hi", 0, &eng.T);
        int c2 = graph_add(g0, "crystal_lo", 0, &eng.T);
        int feeder = graph_add(g0, "feeder", 0, &eng.T);
        g0->nodes[c1].layer_zero = 0; g0->nodes[c1].identity.len = 64;
        g0->nodes[c2].layer_zero = 0; g0->nodes[c2].identity.len = 64;
        g0->nodes[feeder].layer_zero = 0; g0->nodes[feeder].identity.len = 64;
        graph_wire(g0, feeder, feeder, c1, 250, 0);
        graph_wire(g0, feeder, feeder, c2, 10, 0);
        crystal_update(&g0->nodes[c1], g0->edges, g0->n_edges, c1);
        crystal_update(&g0->nodes[c2], g0->edges, g0->n_edges, c2);
        check("crystal: strong > weak", 1, crystal_strength(&g0->nodes[c1]) > crystal_strength(&g0->nodes[c2]) ? 1 : 0);
        engine_destroy(&eng);
    }

    /* ── Tier 2: T11 Z-chain ─────────────────────────────────── */
    printf("--- T11 Z-chain ---\n");
    {
        Engine eng; engine_init(&eng);
        Graph *g0 = &eng.shells[0].g;
        int src = graph_add(g0, "z_src", 0, &eng.T);
        int mid = graph_add(g0, "z_mid", 0, &eng.T);
        int dst = graph_add(g0, "z_dst", 0, &eng.T);
        for (int n = src; n <= dst; n++) {
            g0->nodes[n].layer_zero = 0;
            g0->nodes[n].identity.len = 64;
            memset(g0->nodes[n].identity.w, 0xFF, 8);
        }
        g0->nodes[src].val = 42;
        graph_wire(g0, src, src, mid, 255, 0);
        graph_wire(g0, mid, mid, dst, 255, 0);
        int max_z = graph_compute_z(g0);
        check("Z chain has depth", 1, max_z >= 2 ? 1 : 0);
        check("src at Z=0", 0, coord_z(g0->nodes[src].coord));
        check("mid at Z=1", 1, coord_z(g0->nodes[mid].coord));
        check("dst at Z=2", 2, coord_z(g0->nodes[dst].coord));
        engine_tick(&eng);
        check("mid received signal", 1, g0->nodes[mid].val != 0 ? 1 : 0);
        check("dst received cascade", 1, g0->nodes[dst].val != 0 ? 1 : 0);
        engine_destroy(&eng);
    }

    /* ── Tier 2: T17 Accumulation ────────────────────────────── */
    printf("--- T17 Accumulation ---\n");
    {
        Engine eng; engine_init(&eng);
        Graph *g0 = &eng.shells[0].g;
        int a = graph_add(g0, "acc_a", 0, &eng.T);
        int b = graph_add(g0, "acc_b", 0, &eng.T);
        int d = graph_add(g0, "acc_d", 0, &eng.T);
        for (int n = a; n <= d; n++) {
            g0->nodes[n].layer_zero = 0;
            g0->nodes[n].identity.len = 64;
            memset(g0->nodes[n].identity.w, 0xFF, 8);
        }
        g0->nodes[a].val = 7; g0->nodes[b].val = 3;
        graph_wire(g0, a, b, d, 255, 0);
        engine_tick(&eng);
        check("accumulation: 7+3=10", 10, g0->nodes[d].val);
        check("accum cleared", 0, g0->nodes[d].accum);
        check("n_incoming cleared", 0, g0->nodes[d].n_incoming);
        engine_destroy(&eng);
    }

    /* ── Tier 2: T18 Adaptive timing ─────────────────────────── */
    printf("--- T18 Adaptive Timing ---\n");
    {
        Engine eng; engine_init(&eng);
        int old_interval = eng.shells[0].g.grow_interval;
        engine_ingest_text(&eng, "at_a", "alpha timing test data with enough content to trigger auto grow");
        engine_ingest_text(&eng, "at_b", "beta timing test data with enough content to trigger auto grow");
        engine_ingest_text(&eng, "at_c", "gamma timing test data with enough content to trigger auto grow");
        for (int i = 0; i < 500; i++) engine_tick(&eng);
        int new_interval = eng.shells[0].g.grow_interval;
        check("adaptive timing changed", 1, old_interval != new_interval ? 1 : 0);
        engine_destroy(&eng);
    }

    /* ── Tier 2: T20 Energy bounded ──────────────────────────── */
    printf("--- T20 Energy Bounded ---\n");
    {
        Engine eng; engine_init(&eng);
        Graph *g0 = &eng.shells[0].g;
        int ea = graph_add(g0, "en_a", 0, &eng.T);
        int eb = graph_add(g0, "en_b", 0, &eng.T);
        int ed = graph_add(g0, "en_d", 0, &eng.T);
        for (int n = ea; n <= ed; n++) {
            g0->nodes[n].layer_zero = 0;
            g0->nodes[n].identity.len = 64;
            memset(g0->nodes[n].identity.w, 0xFF, 8);
        }
        g0->nodes[ea].val = 7; g0->nodes[eb].val = 3; g0->nodes[ed].val = 0;
        graph_wire(g0, ea, eb, ed, 255, 0);
        engine_tick(&eng);
        check("energy bounded", 1, abs(g0->nodes[ed].val) <= 10 ? 1 : 0);
        engine_destroy(&eng);
    }

    /* ── Tier 2: T23 ONETWO val ──────────────────────────────── */
    printf("--- T23 ONETWO Val ---\n");
    {
        Engine eng; engine_init(&eng);
        int id = engine_ingest_text(&eng, "ot_test", "the quick brown fox jumps over the lazy dog");
        Node *n = &eng.shells[0].g.nodes[id];
        check("onetwo val not zero", 1, n->val != 0 ? 1 : 0);
        check("onetwo ticks ran", 1, eng.onetwo.tick_count > 0 ? 1 : 0);
        engine_destroy(&eng);
    }

    /* ── Nest Remove ─────────────────────────────────────────── */
    printf("--- Nest Remove ---\n");
    {
        Engine eng; engine_init(&eng);
        int id = engine_ingest_text(&eng, "nr_parent", "data for nest remove test with enough content");
        eng.shells[0].g.nodes[id].valence = 255;
        for (int i = 0; i <= (int)SUBSTRATE_INT; i++) engine_tick(&eng);
        int had = eng.n_children;
        check("child spawned for remove test", 1, had > 0 ? 1 : 0);
        eng.shells[0].g.nodes[id].alive = 0;
        nest_remove(&eng, id);
        check("child removed", had - 1, eng.n_children);
        check("parent child_id cleared", -1, eng.shells[0].g.nodes[id].child_id);
        engine_destroy(&eng);
    }

    /* ── Invariant: Dead stays dead ──────────────────────────── */
    printf("--- Invariant: Dead Stays Dead ---\n");
    {
        Engine eng; engine_init(&eng);
        Graph *g0 = &eng.shells[0].g;
        int dd = graph_add(g0, "dead_node", 0, &eng.T);
        g0->nodes[dd].val = 0;
        g0->nodes[dd].alive = 0;
        for (int i = 0; i < 100; i++) engine_tick(&eng);
        check("dead node val unchanged", 0, g0->nodes[dd].val);
        check("dead node still dead", 0, (int)g0->nodes[dd].alive);
        engine_destroy(&eng);
    }

    /* ── Invariant: Fresnel T+R=1 ────────────────────────────── */
    printf("--- Invariant: Fresnel Conservation ---\n");
    {
        double K_vals[] = { 0.5, 1.0, 1.5, 2.25, 0.1, 10.0 };
        int all_ok = 1;
        for (int i = 0; i < 6; i++) {
            double T = fresnel_T(K_vals[i]);
            double R = fresnel_R(K_vals[i]);
            if (fabs(T + R - 1.0) > 1e-10) all_ok = 0;
        }
        check("fresnel T+R=1 for all K", 1, all_ok);
    }

    /* ── Lysis Trigger ────────────────────────────────────────── */
    printf("--- Lysis Trigger ---\n");
    {
        Engine eng; engine_init(&eng);
        int id = engine_ingest_text(&eng, "lysis_a", "data for lysis trigger test with enough content for identity");
        eng.shells[0].g.nodes[id].valence = 255;
        for (int i = 0; i <= (int)SUBSTRATE_INT; i++) engine_tick(&eng);
        check("lysis: child spawned", 1, eng.n_children > 0 ? 1 : 0);
        int child_slot = eng.shells[0].g.nodes[id].child_id;
        check("lysis: valid child slot", 1, child_slot >= 0 ? 1 : 0);

        eng.shells[0].g.nodes[id].valence = 90;
        for (int i = 0; i < (int)SUBSTRATE_INT; i++) engine_tick(&eng);
        check("lysis: child removed", 0, eng.n_children);
        check("lysis: child_id cleared", -1, eng.shells[0].g.nodes[id].child_id);
        engine_destroy(&eng);
    }

    /* ── Valence Decay Under Error ───────────────────────────── */
    printf("--- Valence Decay Under Error ---\n");
    {
        Engine eng; engine_init(&eng);
        engine_ingest_text(&eng, "decay_a", "the quick brown fox jumps over the lazy dog by the river");
        engine_ingest_text(&eng, "decay_b", "the quick brown cat jumps over the lazy log by the river");
        /* Stabilize: let ONETWO build history */
        for (int i = 0; i < (int)SUBSTRATE_INT * 3; i++) engine_tick(&eng);

        Graph *g0 = &eng.shells[0].g;
        /* Set valence to 180 (below crystallization 200, above lysis 100) */
        for (int n = 0; n < g0->n_nodes; n++)
            if (g0->nodes[n].alive) g0->nodes[n].valence = 180;
        uint8_t valence_before = g0->nodes[0].valence;

        /* Poison ONETWO: inject all-0xFF so prev_match is sky-high */
        { uint8_t poison[400]; memset(poison, 0xFF, 400);
          ot_sys_ingest(&eng.onetwo, poison, 400); }

        /* Zero all edge weights: creates massive structural mismatch */
        for (int e = 0; e < g0->n_edges; e++)
            g0->edges[e].weight = 0;
        if (eng.n_shells >= 2) {
            Graph *g1 = &eng.shells[1].g;
            for (int e = 0; e < g1->n_edges; e++)
                g1->edges[e].weight = 0;
        }

        /* Align to SUBSTRATE_INT boundary, then tick once to trigger feedback */
        while (eng.total_ticks % SUBSTRATE_INT != (SUBSTRATE_INT - 1))
            engine_tick(&eng);
        engine_tick(&eng);

        uint8_t valence_after = g0->nodes[0].valence;
        int32_t error_1st = eng.onetwo.feedback[7];
        int32_t energy_1st = eng.onetwo.feedback[5];
        printf("  1st cycle: error=%d energy=%d  valence: %d -> %d\n",
               error_1st, energy_1st, valence_before, valence_after);
        check("error spike from poison", 1, abs(error_1st) > energy_1st / 3 ? 1 : 0);
        check("valence decayed under error", 1, valence_after < valence_before ? 1 : 0);
        engine_destroy(&eng);
    }

    /* ── Full Cycle: Lysis + Reuse ───────────────────────────── */
    printf("--- Full Cycle: Lysis + Reuse ---\n");
    {
        Engine eng; engine_init(&eng);
        const char *names[] = {"cyc_a", "cyc_b", "cyc_c", "cyc_d"};
        const char *texts[] = {
            "alpha data for cycle test with unique structural fingerprint aaa",
            "beta data for cycle test with unique structural fingerprint bbb",
            "gamma data for cycle test with unique structural fingerprint ccc",
            "delta data for cycle test with unique structural fingerprint ddd"
        };
        int ids[4];
        for (int k = 0; k < 4; k++) {
            ids[k] = engine_ingest_text(&eng, names[k], texts[k]);
            eng.shells[0].g.nodes[ids[k]].valence = 255;
        }
        for (int i = 0; i <= (int)SUBSTRATE_INT; i++) engine_tick(&eng);
        check("cycle: all 4 children spawned", MAX_CHILDREN, eng.n_children);

        eng.shells[0].g.nodes[ids[0]].valence = 50;
        for (int i = 0; i < (int)SUBSTRATE_INT; i++) engine_tick(&eng);
        check("cycle: one child removed by lysis", MAX_CHILDREN - 1, eng.n_children);

        int new_id = engine_ingest_text(&eng, "cyc_e", "epsilon new data spawning into freed slot eee");
        eng.shells[0].g.nodes[new_id].valence = 255;
        for (int i = 0; i <= (int)SUBSTRATE_INT; i++) engine_tick(&eng);
        check("cycle: new child spawned into freed slot", MAX_CHILDREN, eng.n_children);
        check("cycle: new node has child", 1, eng.shells[0].g.nodes[new_id].child_id >= 0 ? 1 : 0);
        engine_destroy(&eng);
    }

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
