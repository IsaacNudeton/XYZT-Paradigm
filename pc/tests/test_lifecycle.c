/*
 * test_lifecycle.c — Nest remove, dead stays dead, Fresnel, XOR observer,
 *                    lysis, valence decay, full cycle lysis+reuse
 */
#include "test.h"

void run_lifecycle_tests(void) {
    /* Nest Remove */
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

    /* Invariant: Dead stays dead */
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

    /* Invariant: Fresnel T+R=1 */
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

    /* XOR Observer + I_energy */
    printf("--- XOR Observer ---\n");
    {
        check("xor: no energy no val", 0, obs_xor(0, 0));
        check("xor: energy + val (constructive)", 0, obs_xor(100, 42));
        check("xor: energy + no val (destructive)", 1, obs_xor(100, 0));
        check("xor: no energy + val (passthrough)", 0, obs_xor(0, 42));

        Engine eng; engine_init(&eng);
        Graph *g0 = &eng.shells[0].g;
        int sa = graph_add(g0, "xor_a", 0, &eng.T);
        int sb = graph_add(g0, "xor_b", 0, &eng.T);
        int sd = graph_add(g0, "xor_d", 0, &eng.T);
        for (int n = sa; n <= sd; n++) {
            g0->nodes[n].layer_zero = 0;
            g0->nodes[n].identity.len = 64;
            memset(g0->nodes[n].identity.w, 0xFF, 8);
        }
        int sc = graph_add(g0, "xor_c", 0, &eng.T);
        g0->nodes[sc].layer_zero = 0; g0->nodes[sc].identity.len = 64;
        memset(g0->nodes[sc].identity.w, 0xFF, 8);
        g0->nodes[sa].val = 50; g0->nodes[sc].val = -50;
        graph_wire(g0, sa, sa, sd, 255, 0);
        graph_wire(g0, sc, sc, sd, 255, 0);
        engine_tick(&eng);
        check("destructive: I_energy > 0", 1, g0->nodes[sd].I_energy > 0 ? 1 : 0);
        check("destructive: val cancelled", 1, abs(g0->nodes[sd].val) < 50 ? 1 : 0);
        engine_destroy(&eng);
    }

    /* Lysis Trigger */
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

    /* Valence Decay Under Error */
    printf("--- Valence Decay Under Error ---\n");
    {
        Engine eng; engine_init(&eng);
        engine_ingest_text(&eng, "decay_a", "the quick brown fox jumps over the lazy dog by the river");
        engine_ingest_text(&eng, "decay_b", "the quick brown cat jumps over the lazy log by the river");
        for (int i = 0; i < (int)SUBSTRATE_INT * 3; i++) engine_tick(&eng);

        Graph *g0 = &eng.shells[0].g;
        for (int n = 0; n < g0->n_nodes; n++)
            if (g0->nodes[n].alive) g0->nodes[n].valence = 180;
        uint8_t valence_before = g0->nodes[0].valence;

        { uint8_t poison[400]; memset(poison, 0xFF, 400);
          ot_sys_ingest(&eng.onetwo, poison, 400); }

        for (int e = 0; e < g0->n_edges; e++)
            g0->edges[e].weight = 0;
        if (eng.n_shells >= 2) {
            Graph *g1 = &eng.shells[1].g;
            for (int e = 0; e < g1->n_edges; e++)
                g1->edges[e].weight = 0;
        }

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

    /* Full Cycle: Lysis + Reuse */
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
}
