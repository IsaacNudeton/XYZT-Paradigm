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
        for (int t = 0; t < 25; t++) engine_tick(&eng);
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

        /* Force low valence + incoherent: boredom won't save it */
        eng.shells[0].g.nodes[id].valence = 50;
        eng.shells[0].g.nodes[id].coherent = -1;
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

        /* Kill all edges: set n_cells=0 and weight=0 so S3 propagation
         * skips them (n_cells==0 check) and graph_learn ignores them.
         * tline_weight clamps to 1, so we must set weight=0 directly. */
        for (int e = 0; e < g0->n_edges; e++) {
            g0->edges[e].tl.n_cells = 0;
            g0->edges[e].weight = 0;
        }
        for (int s = 1; s < eng.n_shells; s++) {
            Graph *gs = &eng.shells[s].g;
            for (int e = 0; e < gs->n_edges; e++) {
                gs->edges[e].tl.n_cells = 0;
                gs->edges[e].weight = 0;
            }
        }
        for (int e = 0; e < eng.n_boundary_edges; e++) {
            eng.boundary_edges[e].tl.n_cells = 0;
            eng.boundary_edges[e].weight = 0;
        }

        /* Reset coherence state so the coherence detector starts clean.
         * Also remove children — child propagation changes parent val,
         * which the coherence detector picks up as instability.
         * Disable auto_grow — S5 would create fresh (un-crushed) edges
         * that carry signal, changing val and triggering incoherence. */
        for (int n = 0; n < g0->n_nodes; n++) {
            g0->nodes[n].prev_edge_sum = 0;
            g0->nodes[n].prev_val = g0->nodes[n].val;
            g0->nodes[n].coherent = 1;
            g0->nodes[n].last_active = (uint32_t)T_now(&eng.T);
            g0->nodes[n].child_id = -1;
        }
        for (int s = 0; s < eng.n_shells; s++)
            eng.shells[s].g.auto_grow = 0;

        while (eng.total_ticks % SUBSTRATE_INT != (SUBSTRATE_INT - 1))
            engine_tick(&eng);
        engine_tick(&eng);
        uint8_t valence_after = g0->nodes[0].valence;
        int32_t error_1st = eng.onetwo.feedback[7];
        int32_t energy_1st = eng.onetwo.feedback[5];
        printf("  1st cycle: error=%d energy=%d  valence: %d -> %d  coherent=%d alive=%d\n",
               error_1st, energy_1st, valence_before, valence_after,
               g0->nodes[0].coherent, g0->nodes[0].alive);
        check("error spike from poison", 1, abs(error_1st) > energy_1st / 3 ? 1 : 0);
        /* Targeted decay: coherent nodes are protected. With edges zeroed,
         * all nodes lack neighbors → scale<2 → coherent → no decay. */
        check("coherent nodes protected under error", 1, valence_after == valence_before ? 1 : 0);
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
        /* Hold node 0 at low valence while reinforcing others.
         * With directed edges + child tick fix, boredom feedback increments
         * valence for active nodes each tick. Hold it explicitly so lysis
         * can fire at the SUBSTRATE_INT boundary. */
        for (int i = 0; i < (int)SUBSTRATE_INT; i++) {
            for (int k = 1; k < 4; k++)
                eng.shells[0].g.nodes[ids[k]].valence = 255;
            eng.shells[0].g.nodes[ids[0]].valence = 50;
            engine_tick(&eng);
        }
        check("cycle: one child removed by lysis", MAX_CHILDREN - 1, eng.n_children);

        int new_id = engine_ingest_text(&eng, "cyc_e", "epsilon new data spawning into freed slot eee");
        eng.shells[0].g.nodes[new_id].valence = 255;
        for (int i = 0; i <= (int)SUBSTRATE_INT; i++) {
            /* Keep survivors reinforced during spawn cycle */
            for (int k = 1; k < 4; k++)
                if (eng.shells[0].g.nodes[ids[k]].alive)
                    eng.shells[0].g.nodes[ids[k]].valence = 255;
            engine_tick(&eng);
        }
        check("cycle: new child spawned into freed slot", MAX_CHILDREN, eng.n_children);
        check("cycle: new node has child", 1, eng.shells[0].g.nodes[new_id].child_id >= 0 ? 1 : 0);
        engine_destroy(&eng);
    }

    /* Grow Opportunity: should create collision points (n_in >= 2) */
    printf("--- Grow Opportunity ---\n");
    {
        Engine eng; engine_init(&eng);
        Graph *g0 = &eng.shells[0].g;

        for (int k = 0; k < 6; k++) {
            char nm[16]; snprintf(nm, sizeof(nm), "gop_%d", k);
            engine_ingest_text(&eng, nm,
                "shared content for grow opportunity test with structural overlap");
        }

        for (int i = 0; i < (int)SUBSTRATE_INT * 3; i++) engine_tick(&eng);

        int n_collision = 0;
        int n_in_count[MAX_NODES]; memset(n_in_count, 0, sizeof(n_in_count));
        for (int e = 0; e < g0->n_edges; e++)
            if (g0->edges[e].weight > 0) n_in_count[g0->edges[e].dst]++;
        for (int i = 0; i < g0->n_nodes; i++)
            if (g0->nodes[i].alive && !g0->nodes[i].layer_zero && n_in_count[i] >= 2)
                n_collision++;

        printf("  collision vertices: %d out of %d alive nodes\n", n_collision, g0->n_nodes);
        check("grow created collision points", 1, n_collision > 0 ? 1 : 0);
        engine_destroy(&eng);
    }

    /* Crystal Coherence: coherence field is computed for all alive nodes */
    printf("--- Coherence Computed ---\n");
    {
        Engine eng; engine_init(&eng);
        int id = engine_ingest_text(&eng, "coh_a",
            "coherence test content alpha with structural fingerprint");
        int nbr = engine_ingest_text(&eng, "coh_b",
            "coherence test content beta different fingerprint pattern");
        graph_wire(&eng.shells[0].g, id, id, nbr, 200, 0);
        graph_wire(&eng.shells[0].g, nbr, nbr, id, 200, 0);

        /* Run past first SUBSTRATE_INT to establish prev_val, then another to compute coherence */
        for (int i = 0; i < (int)SUBSTRATE_INT * 2 + 1; i++) engine_tick(&eng);

        /* Both nodes should have coherent field set (not 0 = unknown) */
        int8_t ca = eng.shells[0].g.nodes[id].coherent;
        int8_t cb = eng.shells[0].g.nodes[nbr].coherent;
        printf("  coh_a coherent=%d, coh_b coherent=%d\n", ca, cb);
        check("coh_a computed", 1, ca != 0 ? 1 : 0);
        check("coh_b computed", 1, cb != 0 ? 1 : 0);

        /* Prev_val should be set (not still 0 from init) */
        check("prev_val tracked", 1,
              (eng.shells[0].g.nodes[id].prev_val != 0 ||
               eng.shells[0].g.nodes[nbr].prev_val != 0) ? 1 : 0);
        engine_destroy(&eng);
    }

    /* Frustration erodes incoherent crystal, spares coherent */
    printf("--- Frustration Erodes Incoherent ---\n");
    {
        Engine eng; engine_init(&eng);
        Graph *g0 = &eng.shells[0].g;

        int id = engine_ingest_text(&eng, "frust_target",
            "frustration erosion target content for close loop test");
        g0->nodes[id].valence = 220;
        g0->nodes[id].coherent = -1;   /* incoherent */
        g0->nodes[id].val = 500;       /* far from mean */

        int nbr = engine_ingest_text(&eng, "frust_safe",
            "frustration safe neighbor coherent content different");
        g0->nodes[nbr].valence = 200;
        g0->nodes[nbr].coherent = 1;   /* coherent */
        g0->nodes[nbr].val = 100;

        graph_wire(g0, id, id, nbr, 200, 0);
        graph_wire(g0, nbr, nbr, id, 200, 0);

        uint8_t target_before = g0->nodes[id].valence;
        uint8_t safe_before = g0->nodes[nbr].valence;

        /* Force high error to trigger frustration */
        eng.onetwo.feedback[7] = 500;
        eng.onetwo.feedback[4] = 0;

        for (int i = 0; i < 20; i++) engine_tick(&eng);

        printf("  incoherent crystal: valence %d -> %d\n",
               target_before, g0->nodes[id].valence);
        printf("  coherent crystal: valence %d -> %d\n",
               safe_before, g0->nodes[nbr].valence);

        check("frustration: incoherent eroded", 1,
              g0->nodes[id].valence < target_before ? 1 : 0);
        check("frustration: coherent mostly held", 1,
              g0->nodes[nbr].valence >= safe_before - 5 ? 1 : 0);
        engine_destroy(&eng);
    }

    /* Boredom crystallizes coherent active nodes */
    printf("--- Boredom Crystallizes Coherent ---\n");
    {
        Engine eng; engine_init(&eng);
        Graph *g0 = &eng.shells[0].g;

        int id = engine_ingest_text(&eng, "bore_target",
            "boredom crystallization coherent active test content");
        g0->nodes[id].valence = 100;
        g0->nodes[id].coherent = 1;    /* coherent */
        g0->nodes[id].val = 50;        /* nonzero = active */
        g0->nodes[id].last_active = (uint32_t)T_now(&eng.T);

        uint8_t val_before = g0->nodes[id].valence;

        /* Force low error + stable = boredom */
        eng.onetwo.feedback[7] = 10;
        eng.onetwo.feedback[4] = 1;

        for (int i = 0; i < 50; i++) engine_tick(&eng);

        printf("  coherent node: valence %d -> %d\n",
               val_before, g0->nodes[id].valence);
        check("boredom: coherent gained valence", 1,
              g0->nodes[id].valence > val_before ? 1 : 0);
        engine_destroy(&eng);
    }

    /* Curiosity leaves system alone */
    printf("--- Curiosity No Interference ---\n");
    {
        Engine eng; engine_init(&eng);
        Graph *g0 = &eng.shells[0].g;

        int id = engine_ingest_text(&eng, "curious_node",
            "curiosity drive state no interference test content");
        g0->nodes[id].valence = 150;
        g0->nodes[id].val = 100;

        uint8_t val_before = g0->nodes[id].valence;

        /* Error at MISMATCH_TAX_NUM boundary: curiosity zone */
        eng.onetwo.feedback[7] = (int32_t)(MISMATCH_TAX_NUM);
        eng.onetwo.feedback[4] = 0;   /* not stable = not boredom */

        for (int i = 0; i < 20; i++) engine_tick(&eng);

        printf("  curiosity: valence %d -> %d\n",
               val_before, g0->nodes[id].valence);
        check("curiosity: no major valence change", 1,
              abs((int)g0->nodes[id].valence - (int)val_before) < 10 ? 1 : 0);
        engine_destroy(&eng);
    }
}
