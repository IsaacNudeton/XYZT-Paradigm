/*
 * test_save_load.c — v12 Save/Load Roundtrip
 *
 * Verifies full engine persistence: topology, params, stats,
 * OneTwoSystem, SubstrateT, children. Loaded engine must
 * continue running without crash.
 *
 * Isaac Oravec & Claude, March 2026
 */

#include "test.h"

void run_save_load_tests(void) {
    printf("\n=== Save/Load v12 Roundtrip ===\n");

    /* ── Phase 1: Build a trained engine with known state ── */
    Engine eng_a;
    engine_init(&eng_a);

    /* Ingest binary packets to create structure */
    uint8_t buf[512];
    BitStream bs;
    for (int i = 0; i < 10; i++) {
        for (int b = 0; b < 512; b++)
            buf[b] = (uint8_t)(i * 17 + b * 3);
        onetwo_parse(buf, 512, &bs);
        char name[16];
        snprintf(name, sizeof(name), "sl_%d", i);
        engine_ingest(&eng_a, name, &bs);
    }

    /* Run enough ticks for crystallization + nesting */
    for (int t = 0; t < (int)SUBSTRATE_INT * 20; t++)
        engine_tick(&eng_a);

    /* Record pre-save state */
    Graph *g0_a = &eng_a.shells[0].g;
    int pre_n_nodes = g0_a->n_nodes;
    int pre_n_edges = g0_a->n_edges;
    int pre_n_children = eng_a.n_children;
    uint64_t pre_ticks = eng_a.total_ticks;
    uint64_t pre_T = eng_a.T.count;
    int32_t pre_fb7 = eng_a.onetwo.feedback[7];
    int pre_grow_interval = g0_a->grow_interval;
    int pre_grow_mean = g0_a->grow_mean;

    printf("  Pre-save: nodes=%d edges=%d children=%d ticks=%llu T=%llu fb7=%d\n",
           pre_n_nodes, pre_n_edges, pre_n_children,
           (unsigned long long)pre_ticks, (unsigned long long)pre_T, pre_fb7);

    /* ── Phase 2: Save ── */
    const char *path = "test_roundtrip.xyzt";
    int save_rc = engine_save(&eng_a, path);
    check("sl: save succeeded", 0, save_rc);

    /* ── Phase 3: Load into fresh engine ── */
    Engine eng_b;
    engine_init(&eng_b);
    int load_rc = engine_load(&eng_b, path);
    check("sl: load succeeded", 0, load_rc);

    /* ── Phase 4: Compare every field ── */
    Graph *g0_b = &eng_b.shells[0].g;

    /* Engine scalars */
    check("sl: total_ticks", (int)pre_ticks, (int)eng_b.total_ticks);
    check("sl: learn_interval", eng_a.learn_interval, eng_b.learn_interval);
    check("sl: boundary_interval", eng_a.boundary_interval, eng_b.boundary_interval);
    check("sl: low_error_run", eng_a.low_error_run, eng_b.low_error_run);

    /* SubstrateT */
    check("sl: T.count", (int)pre_T, (int)eng_b.T.count);

    /* OneTwoSystem */
    check("sl: onetwo.feedback[7]", pre_fb7, eng_b.onetwo.feedback[7]);
    check("sl: onetwo.tick_count", eng_a.onetwo.tick_count, eng_b.onetwo.tick_count);
    check("sl: onetwo.total_inputs", eng_a.onetwo.total_inputs, eng_b.onetwo.total_inputs);

    /* Graph topology */
    check("sl: n_nodes", pre_n_nodes, g0_b->n_nodes);
    check("sl: n_edges", pre_n_edges, g0_b->n_edges);

    /* Graph params (adapted, not defaults) */
    check("sl: grow_interval", pre_grow_interval, g0_b->grow_interval);
    check("sl: grow_mean", pre_grow_mean, g0_b->grow_mean);
    check("sl: grow_threshold", g0_a->grow_threshold, g0_b->grow_threshold);
    check("sl: prune_threshold", g0_a->prune_threshold, g0_b->prune_threshold);

    /* Graph stats */
    check("sl: total_learns", (int)g0_a->total_learns, (int)g0_b->total_learns);
    check("sl: total_grown", (int)g0_a->total_grown, (int)g0_b->total_grown);

    /* Node data spot-check: first alive node */
    for (int i = 0; i < g0_a->n_nodes; i++) {
        if (!g0_a->nodes[i].alive) continue;
        Node *na = &g0_a->nodes[i];
        Node *nb = &g0_b->nodes[i];
        check("sl: node alive", na->alive, nb->alive);
        check("sl: node valence", na->valence, nb->valence);
        check("sl: node val", na->val, nb->val);
        check("sl: node coherent", na->coherent, nb->coherent);
        check("sl: node name match",
              strncmp(na->name, nb->name, NAME_LEN) == 0 ? 1 : 0, 1);
        break;  /* spot-check one node */
    }

    /* Inner T fields */
    check("sl: error_accum", g0_a->error_accum, g0_b->error_accum);
    check("sl: prev_output", g0_a->prev_output, g0_b->prev_output);
    check("sl: local_heartbeat", g0_a->local_heartbeat, g0_b->local_heartbeat);
    check("sl: drive", g0_a->drive, g0_b->drive);

    /* Children */
    check("sl: n_children", pre_n_children, eng_b.n_children);
    for (int i = 0; i < MAX_CHILDREN; i++) {
        check("sl: child_owner match",
              eng_a.child_owner[i], eng_b.child_owner[i]);
        if (eng_a.child_owner[i] >= 0) {
            Graph *ca = &eng_a.child_pool[i];
            Graph *cb = &eng_b.child_pool[i];
            check("sl: child n_nodes", ca->n_nodes, cb->n_nodes);
            check("sl: child n_edges", ca->n_edges, cb->n_edges);
            check("sl: child error_accum", ca->error_accum, cb->error_accum);
            check("sl: child drive", (int)ca->drive, (int)cb->drive);
        }
    }

    /* Boundary edges */
    check("sl: n_boundary_edges",
          eng_a.n_boundary_edges, eng_b.n_boundary_edges);

    /* ── Phase 5: Verify loaded engine can continue running ── */
    for (int t = 0; t < (int)SUBSTRATE_INT * 5; t++)
        engine_tick(&eng_b);
    check("sl: loaded engine survived 5 more cycles", 1, 1);

    printf("  Post-load run: nodes=%d edges=%d children=%d\n",
           g0_b->n_nodes, g0_b->n_edges, eng_b.n_children);

    /* Cleanup */
    engine_destroy(&eng_a);
    engine_destroy(&eng_b);
    remove(path);
}
