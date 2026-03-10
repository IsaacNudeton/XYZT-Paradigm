/*
 * test_gpu.cu — GPU boolean, nesting retina
 */
extern "C" {
#include "test.h"
}
#include "../substrate.cuh"

extern "C" void run_gpu_tests(void) {
    /* GPU Boolean */
    printf("--- GPU Boolean ---\n");
    {
        int n_cubes = 4;
        if (substrate_init(n_cubes) != 0) {
            printf("  SKIP: GPU init failed\n");
            return;
        }

        CubeState *h_cubes = (CubeState *)calloc(n_cubes, sizeof(CubeState));
        int idx = 0;
        for (int a = 0; a <= 1; a++) {
            for (int b = 0; b <= 1; b++) {
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

        /* AND test */
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

        /* XOR test */
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

    /* Nesting Retina */
    printf("--- Nesting Retina ---\n");
    {
        Engine eng;
        engine_init(&eng);

        engine_ingest_text(&eng, "alpha", "alpha data for testing the retina injection pathway");
        engine_ingest_text(&eng, "beta", "beta data completely different content for retina test");

        Graph *g0 = &eng.shells[0].g;
        for (int i = 0; i < g0->n_nodes; i++) {
            if (g0->nodes[i].alive && !g0->nodes[i].layer_zero)
                g0->nodes[i].valence = 255;
        }

        for (int i = 0; i <= (int)SUBSTRATE_INT; i++) engine_tick(&eng);

        check("children spawned", 1, eng.n_children > 0 ? 1 : 0);

        if (eng.n_children > 0) {
            int slot = -1;
            for (int i = 0; i < MAX_CHILDREN; i++)
                if (eng.child_owner[i] >= 0) { slot = i; break; }
            if (slot >= 0) {
                check("child has 13 nodes", 13, eng.child_pool[slot].n_nodes);
                check("child has 8 edges", 8, eng.child_pool[slot].n_edges);
                check("child ticked", 1, eng.child_pool[slot].total_ticks > 0 ? 1 : 0);
                check("output node alive", 1, eng.child_pool[slot].nodes[12].alive);
                check("retina nodes not layer_zero", 0, eng.child_pool[slot].nodes[0].layer_zero);
            }
        }

        engine_destroy(&eng);
    }
}
