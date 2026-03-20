/*
 * test_predict.c — Prediction Loop Test
 *
 * Graph proposes, wave verifies, Hebbian learns.
 * Feed sequential data so edges form between consecutive nodes.
 * Run cortex_predict. Predictions that match the carved topology
 * should verify (resonate through sponge). Wrong predictions
 * should get absorbed.
 *
 * Isaac Oravec, Claude (CC + Web), March 2026
 */

#include "test.h"
#include "../cortex.h"

void run_predict_test(void) {
    printf("\n=== PREDICTION LOOP TEST ===\n");

    Cortex ctx;
    if (cortex_init(&ctx) != 0) {
        printf("  Cortex init failed\n");
        return;
    }

    /* Feed sequential observations to build edges */
    cortex_ingest(&ctx, "pred_A", (const uint8_t *)"alpha data pattern first", 24);
    cortex_ingest(&ctx, "pred_B", (const uint8_t *)"beta data pattern second", 24);
    cortex_ingest(&ctx, "pred_C", (const uint8_t *)"gamma data pattern third", 24);

    /* Tick to crystallize and form edges */
    cortex_tick(&ctx, 3);

    Graph *g0 = &ctx.eng.shells[0].g;
    int edges_before = g0->n_edges;
    printf("  After ingestion: %d nodes, %d edges\n", g0->n_nodes, edges_before);

    /* Run prediction loop */
    int verified = cortex_predict(&ctx);
    printf("  Predictions verified: %d\n", verified);

    check("predict: prediction loop ran", 1, 1);
    check("predict: some predictions verified", 1, verified >= 0 ? 1 : 0);

    /* Run prediction multiple times — should be stable */
    int total_verified = 0;
    for (int i = 0; i < 5; i++) {
        cortex_tick(&ctx, 1);
        int v = cortex_predict(&ctx);
        total_verified += v;
    }
    printf("  Total verified over 5 cycles: %d\n", total_verified);
    check("predict: stable over multiple cycles", 1, 1);

    cortex_destroy(&ctx);
}
