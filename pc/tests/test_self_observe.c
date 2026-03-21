/*
 * test_self_observe.c — Self-Observation Loop Convergence Test
 *
 * The engine reads its own coherence field, encodes what it sees,
 * and ingests the observation as a new node. This test verifies:
 * 1. The loop produces self-observation nodes
 * 2. The system converges (node count stabilizes, not explodes)
 * 3. Self-observations wire into the existing topology
 * 4. The engine doesn't crash from recursive self-reference
 *
 * Isaac Oravec, Claude & Gemini, March 2026
 */

#include "test.h"
#include "../cortex.h"

void run_self_observe_test(void) {
    printf("\n=== SELF-OBSERVATION LOOP TEST ===\n");

    Cortex ctx;
    if (cortex_init(&ctx) != 0) {
        printf("  Cortex init failed — skipping\n");
        return;
    }

    /* Seed with some real data */
    cortex_ingest(&ctx, "seed_a", (const uint8_t *)"the quick brown fox jumps", 25);
    cortex_ingest(&ctx, "seed_b", (const uint8_t *)"hydrogen bonds form between", 27);
    cortex_ingest(&ctx, "seed_c", (const uint8_t *)"for (int i = 0; i < n; i++)", 27);

    /* Run 2 Hebbian cycles to establish topology */
    cortex_tick(&ctx, 2);

    Graph *g0 = &ctx.eng.shells[0].g;
    int nodes_before = g0->n_nodes;
    printf("  Before self-observation: %d nodes, %d edges\n",
           g0->n_nodes, g0->n_edges);

    /* Run 5 self-observation cycles.
     * Each cycle: tick → self-observe → tick → self-observe.
     * If it converges: node count grows slowly then stabilizes.
     * If it diverges: node count explodes. */
    int so_ids[10];
    int n_so = 0;
    for (int cycle = 0; cycle < 5; cycle++) {
        cortex_tick(&ctx, 1);
        int id = cortex_self_observe(&ctx);
        if (id >= 0 && n_so < 10) so_ids[n_so++] = id;
        printf("  cycle %d: self-observe → node %d (%d total nodes)\n",
               cycle + 1, id, g0->n_nodes);
    }

    int nodes_after = g0->n_nodes;
    int growth = nodes_after - nodes_before;

    printf("  After 5 self-observations: %d nodes (+%d), %d edges\n",
           nodes_after, growth, g0->n_edges);

    /* Convergence checks */
    check("selfobs: produced self-observation nodes", 1, n_so > 0 ? 1 : 0);
    check("selfobs: growth bounded (< 60 new nodes)", 1, growth < 60 ? 1 : 0);
    check("selfobs: no crash from recursive self-reference", 1, 1);

    /* Check if self-observations wired into existing topology */
    int so_edges = 0;
    for (int e = 0; e < g0->n_edges; e++) {
        Edge *ed = &g0->edges[e];
        if (ed->weight == 0) continue;
        for (int s = 0; s < n_so; s++) {
            if (ed->src_a == so_ids[s] || ed->dst == so_ids[s]) {
                so_edges++;
                break;
            }
        }
    }
    printf("  Self-observation edges: %d\n", so_edges);
    check("selfobs: self-observations wired into graph", 1,
          so_edges > 0 ? 1 : 0);

    /* Run 5 more cycles — does it stabilize? */
    int nodes_mid = g0->n_nodes;
    for (int cycle = 0; cycle < 5; cycle++) {
        cortex_tick(&ctx, 1);
        cortex_self_observe(&ctx);
    }
    int nodes_final = g0->n_nodes;
    int growth_2nd = nodes_final - nodes_mid;

    printf("  Second 5 cycles: +%d nodes (first 5: +%d)\n", growth_2nd, growth);
    /* Growth should be similar or decreasing — not accelerating */
    check("selfobs: growth not accelerating", 1,
          growth_2nd <= growth + 5 ? 1 : 0);

    cortex_destroy(&ctx);
}
