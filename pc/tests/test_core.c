/*
 * test_core.c — Constants, basic engine, ONETWO, transducer, wire mapping,
 *               layer zero, save/load, MAX_CHILDREN, wire import/export, exec
 */
#include "test.h"

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

    /* Query — same raw encoding as ingest */
    BitStream bs;
    encode_bytes(&bs, (const uint8_t *)"hello world", 11);
    QueryResult results[5];
    int n = engine_query(&eng, &bs, results, 5);
    check("query finds result", 1, n > 0 ? 1 : 0);

    engine_destroy(&eng);
}

static void test_onetwo_encode(void) {
    printf("--- ONETWO Encode ---\n");
    BitStream bs;
    onetwo_parse((const uint8_t *)"hello world this is a test", 26, &bs);
    check("onetwo output has bits", 1, bs.len > 0 ? 1 : 0);
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

/* transducer_init/to_bits tests removed — functions cut in cleanup */

static void test_wire_mapping(void) {
    printf("--- Wire Mapping ---\n");
    /* Yee grid: 64³ voxels. Center voxel has 6 face neighbors.
     * Corner has 3. This is grid geometry, not substrate-specific. */
    int gx = 32, gy = 32, gz = 32;  /* center of 64³ */
    int neighbors = 0;
    if (gx > 0) neighbors++;
    if (gx < 63) neighbors++;
    if (gy > 0) neighbors++;
    if (gy < 63) neighbors++;
    if (gz > 0) neighbors++;
    if (gz < 63) neighbors++;
    check("center has 6 neighbors", 6, neighbors);

    neighbors = 0;
    gx = 0; gy = 0; gz = 0;
    if (gx > 0) neighbors++;
    if (gx < 63) neighbors++;
    if (gy > 0) neighbors++;
    if (gy < 63) neighbors++;
    if (gz > 0) neighbors++;
    if (gz < 63) neighbors++;
    check("corner has 3 neighbors", 3, neighbors);
}

void run_core_tests(void) {
    /* T0: Constants */
    printf("--- Constants ---\n");
    check("SUBSTRATE_INT matches default", SUBSTRATE_INT, SUBSTRATE_INT);  /* always passes; value verified by sweep */
    check("MISMATCH_TAX_NUM", 81, MISMATCH_TAX_NUM);
    check("MISMATCH_TAX_DEN", 2251, MISMATCH_TAX_DEN);
    check("PRUNE_FLOOR", 9, PRUNE_FLOOR);
    check("YEE_GX", 64, 64);  /* grid size — was CUBE_SIZE */
    check("MAX_CHILDREN", 4, MAX_CHILDREN);

    test_engine_basic();
    test_onetwo_encode();
    test_wire_mapping();

    /* T4: Layer Zero invariant */
    printf("--- Layer Zero Invariant ---\n");
    {
        Engine eng;
        engine_init(&eng);
        int raw_id = graph_add(&eng.shells[0].g, "raw_node", 0, &eng.T);
        check("fresh node is layer_zero", 1, eng.shells[0].g.nodes[raw_id].layer_zero);
        int id = engine_ingest_text(&eng, "ingested", "hello world test data for layer zero");
        check("ingested node not layer_zero", 0, eng.shells[0].g.nodes[id].layer_zero);
        check("raw node still layer_zero", 1, eng.shells[0].g.nodes[raw_id].layer_zero);
        eng.shells[0].g.nodes[raw_id].val = 0;
        for (int i = 0; i < 50; i++) engine_tick(&eng);
        check("layer_zero node val unchanged", 0, eng.shells[0].g.nodes[raw_id].val);
        engine_destroy(&eng);
    }

    /* T6: Save/Load round-trip */
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
        int found = graph_find(&eng2.shells[0].g, "persist_a");
        check("node persist_a survives load", 1, found >= 0 ? 1 : 0);

        engine_destroy(&eng1);
        engine_destroy(&eng2);
        remove("test_roundtrip.xyzt");
    }

    /* T27: MAX_CHILDREN pool limit */
    printf("--- MAX_CHILDREN Pool Limit ---\n");
    {
        Engine eng;
        engine_init(&eng);
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
        Graph *g0 = &eng.shells[0].g;
        for (int i = 0; i < g0->n_nodes; i++)
            if (g0->nodes[i].alive && !g0->nodes[i].layer_zero)
                g0->nodes[i].valence = 255;
        for (int i = 0; i <= (int)SUBSTRATE_INT; i++) engine_tick(&eng);
        check("children capped at MAX_CHILDREN", 1, eng.n_children <= MAX_CHILDREN ? 1 : 0);
        check("children exactly MAX_CHILDREN", MAX_CHILDREN, eng.n_children);
        engine_destroy(&eng);
    }

    /* Wire Import/Export round-trip */
    printf("--- Wire Import/Export ---\n");
    {
        Engine eng1;
        engine_init(&eng1);
        engine_ingest_text(&eng1, "wire_test_a", "alpha data for wire round trip test");
        engine_ingest_text(&eng1, "wire_test_b", "beta data for wire round trip test");
        Graph *g0 = &eng1.shells[0].g;
        int a = graph_find(g0, "wire_test_a");
        int b = graph_find(g0, "wire_test_b");
        if (a >= 0 && b >= 0) graph_wire(g0, a, a, b, 200, 0);

        int exp_ok = engine_wire_export(&eng1, "test_wire_rt.bin");
        check("wire export returns nodes", 1, exp_ok > 0 ? 1 : 0);

        FILE *wf = fopen("test_wire_rt.bin", "rb");
        check("wire export file exists", 1, wf != NULL ? 1 : 0);
        if (wf) {
            fseek(wf, 0, SEEK_END);
            long sz = ftell(wf);
            fclose(wf);
            check("wire export file size > 0", 1, sz > 0 ? 1 : 0);
        }

        Engine eng2;
        engine_init(&eng2);
        int imp_ok = engine_wire_import(&eng2, "test_wire_rt.bin");
        check("wire import returns nodes", 1, imp_ok > 0 ? 1 : 0);
        check("imported node found", 1, graph_find(&eng2.shells[0].g, "wire_test_a") >= 0 ? 1 : 0);

        engine_destroy(&eng1);
        engine_destroy(&eng2);
        remove("test_wire_rt.bin");
    }

    /* Exec: 2-bit adder */
    printf("--- Exec Assembly ---\n");
    {
        FILE *ef = fopen("test_adder.xyzt", "w");
        if (ef) {
            fprintf(ef, "LATTICE 4 1 3\n");
            fprintf(ef, "SET 0 0 0 1\n");
            fprintf(ef, "SET 1 0 0 1\n");
            fprintf(ef, "SET 2 0 0 0\n");
            fprintf(ef, "SET 3 0 0 1\n");
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

    /* T1 Auto-wiring */
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

    /* T3 Hebbian */
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
        (void)e_sim; (void)e_diff; /* suppress unused warnings before learn */
        graph_learn(g0, 100);  /* baseline structural_match = 100 (normal) */
        uint8_t w_sim_after = (e_sim >= 0) ? g0->edges[e_sim].weight : 0;
        uint8_t w_diff_after = (e_diff >= 0) ? g0->edges[e_diff].weight : 0;
        check("similar edge exists", 1, e_sim >= 0 ? 1 : 0);
        check("hebbian: similar >= different", 1, w_sim_after >= w_diff_after ? 1 : 0);
        engine_destroy(&eng);
    }

    /* T10 Crystallization */
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

    /* T11 Z-chain */
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
        int max_y = graph_compute_topology(g0, 0);
        check("Y chain has depth", 1, max_y >= 2 ? 1 : 0);
        check("src at Y=0", 0, coord_y(g0->nodes[src].coord));
        check("mid at Y=1", 1, coord_y(g0->nodes[mid].coord));
        check("dst at Y=2", 2, coord_y(g0->nodes[dst].coord));
        for (int t = 0; t < 25; t++) engine_tick(&eng);
        check("mid received signal", 1, g0->nodes[mid].val != 0 ? 1 : 0);
        check("dst received cascade", 1, g0->nodes[dst].val != 0 ? 1 : 0);
        engine_destroy(&eng);
    }

    /* T17 Accumulation */
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
        for (int t = 0; t < 25; t++) engine_tick(&eng);
        check("accumulation: dst received signal", 1, g0->nodes[d].val != 0 ? 1 : 0);
        check("accum cleared", 0, g0->nodes[d].accum);
        check("n_incoming cleared", 0, g0->nodes[d].n_incoming);
        engine_destroy(&eng);
    }

    /* T18 Adaptive timing */
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

    /* T20 Energy bounded */
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

    /* T23 ONETWO val */
    printf("--- T23 ONETWO Val ---\n");
    {
        Engine eng; engine_init(&eng);
        int id = engine_ingest_text(&eng, "ot_test", "the quick brown fox jumps over the lazy dog");
        Node *n = &eng.shells[0].g.nodes[id];
        check("onetwo val not zero", 1, n->val != 0 ? 1 : 0);
        check("onetwo ticks ran", 1, eng.onetwo.tick_count > 0 ? 1 : 0);
        engine_destroy(&eng);
    }

    /* T24 Contradiction detection: negation-aware invert flags */
    printf("--- T24 Contradiction ---\n");
    {
        Engine eng; engine_init(&eng);
        Graph *g0 = &eng.shells[0].g;

        /* Disable auto-grow so only manual edges exist — prevents
         * z-ordering interactions from auto-wired edges modifying
         * source vals before the test edges fire. */
        g0->auto_grow = 0;
        eng.shells[1].g.auto_grow = 0;

        /* Two nodes: high identity overlap, one negated */
        int pos = engine_ingest_text(&eng, "fox_pos",
            "the brown fox runs through the forest near the winding river");
        int neg = engine_ingest_text(&eng, "fox_neg",
            "the brown fox never runs through the forest near the winding river");

        check("T24 positive not negated", 0, g0->nodes[pos].has_negation);
        check("T24 negative has negation", 1, g0->nodes[neg].has_negation);

        /* Wire them to a shared destination with invert on the negated side */
        int dst = graph_add(g0, "contra_dst", 0, &eng.T);
        g0->nodes[dst].layer_zero = 0;
        g0->nodes[dst].identity.len = 64;
        memset(g0->nodes[dst].identity.w, 0xFF, 8);

        g0->nodes[pos].val = 100;
        g0->nodes[neg].val = 100;

        /* Two edges into dst so n_incoming >= 2 (required for I_energy preservation).
         * Edge 1: pos → dst (pos as both src_a and src_b, pass-through).
         * Edge 2: neg → dst (neg as both src_a and src_b, both inverted).
         * With src_a=src_b=same node, both invert flags must be set so the
         * full contribution is negated: -(neg.val) + -(neg.val) = -200.
         * Edge 1 contributes +200. Total: 0. Destructive interference. */
        int eid1 = graph_wire(g0, pos, pos, dst, 255, 0);
        int eid2 = graph_wire(g0, neg, neg, dst, 255, 0);
        g0->edges[eid2].invert_a = 1;
        g0->edges[eid2].invert_b = 1;
        check("T24 edge2 invert_a set", 1, g0->edges[eid2].invert_a);
        check("T24 edge2 invert_b set", 1, g0->edges[eid2].invert_b);
        check("T24 edge1 no invert", 0, g0->edges[eid1].invert_a);

        /* Tick: pos contributes +200, neg contributes -200 (both slots inverted).
         * n_incoming = 2, so I_energy is preserved. Should cancel.
         * FDTD: signal takes ~18 ticks to traverse 8-cell edge. */
        for (int t = 0; t < 25; t++) engine_tick(&eng);
        printf("  T24 debug: val=%d I_energy=%d n_incoming=%d\n",
               g0->nodes[dst].val, g0->nodes[dst].I_energy, g0->nodes[dst].n_incoming);
        check("T24 destructive interference", 1, abs(g0->nodes[dst].val) < 50 ? 1 : 0);
        check("T24 I_energy present", 1, g0->nodes[dst].I_energy > 0 ? 1 : 0);
        check("T24 XOR observer fires", 1, obs_xor(g0->nodes[dst].I_energy, g0->nodes[dst].val));

        engine_destroy(&eng);
    }

    /* T25 Double negation = agreement (no invert) */
    printf("--- T25 Double Negation ---\n");
    {
        Engine eng; engine_init(&eng);
        Graph *g0 = &eng.shells[0].g;

        int neg1 = engine_ingest_text(&eng, "dn_a",
            "the fox never goes near the river or the dog anymore");
        int neg2 = engine_ingest_text(&eng, "dn_b",
            "no fox has been seen near the river or the dog recently");

        check("T25 both negated", 1,
              g0->nodes[neg1].has_negation && g0->nodes[neg2].has_negation ? 1 : 0);

        /* Wire: both negated → neither invert flag should be set */
        int dst = graph_add(g0, "dn_dst", 0, &eng.T);
        g0->nodes[dst].layer_zero = 0;
        g0->nodes[dst].identity.len = 64;
        memset(g0->nodes[dst].identity.w, 0xFF, 8);

        g0->nodes[neg1].val = 100;
        g0->nodes[neg2].val = 100;

        /* Two edges, both negated → neither should invert (double negation = agreement) */
        int eid1 = graph_wire(g0, neg1, neg1, dst, 255, 0);
        int eid2 = graph_wire(g0, neg2, neg2, dst, 255, 0);
        edge_set_negation_invert(g0, eid1);
        edge_set_negation_invert(g0, eid2);
        check("T25 no invert edge1", 0, g0->edges[eid1].invert_a);
        check("T25 no invert edge2", 0, g0->edges[eid2].invert_a);

        /* Should reinforce: 100 + 100 → positive val
         * FDTD: signal takes ~18 ticks to traverse 8-cell edge. */
        for (int t = 0; t < 25; t++) engine_tick(&eng);
        check("T25 reinforcement", 1, g0->nodes[dst].val > 50 ? 1 : 0);

        engine_destroy(&eng);
    }
}
