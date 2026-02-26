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

static void cmd_test(void) {
    printf("=== XYZT PC Self-Test ===\n\n");

    test_engine_basic();
    test_onetwo_encode();
    test_gpu_boolean();
    test_transducer();
    test_wire_mapping();

    printf("\n=== RESULTS: %d passed, %d failed, %d total ===\n",
           g_pass, g_fail, g_pass + g_fail);
    if (g_fail == 0) printf("ALL PASS.\n");
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
                    wire_gpu_to_engine(&eng, h_cubes, n_cubes);
                    wire_hebbian_from_gpu(&eng, h_cubes, n_cubes);
                    wire_engine_to_gpu(&eng, h_cubes, n_cubes);
                    substrate_upload(h_cubes, n_cubes);
                }
            }

            /* Final sync at end of batch */
            if (gpu_ok) {
                substrate_download(h_cubes, n_cubes);
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
            if (engine_load(&eng, line + 5) == 0)
                printf("Loaded from '%s'\n", line + 5);
            else
                printf("Load failed.\n");
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
    } else {
        printf("Usage: xyzt_pc [run|test|bench|ingest <file>]\n");
        return 1;
    }

    return 0;
}
