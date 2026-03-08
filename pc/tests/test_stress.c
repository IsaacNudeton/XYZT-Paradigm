/*
 * test_stress.c — Adversarial stress tests (ported from use_adversarial_v3)
 *
 * S1-S4:   Prediction exhaustive (constant, linear, geometric, acceleration)
 * S5-S9:   Observation invariants (density, period, symmetry, edge inputs)
 * S10-S13: Engine behavior (determinism, value bounds, stability, wake)
 * S14-S16: Overflow and pool exhaustion
 */
#include "test.h"

void run_stress_tests(void) {
    /* S1: Predict constant exhaustive */
    printf("--- Stress: Predict Constant (201 values) ---\n");
    {
        int s1_ok = 1;
        for (int v = -100; v <= 100; v++) {
            int vals[5] = {v, v, v, v, v};
            int pt;
            int pred = ot_predict_sequence(vals, 5, &pt);
            if (pred != v || pt != 1) { s1_ok = 0; break; }
        }
        check("S1 all constants predicted", 1, s1_ok);
    }

    /* S2: Predict linear exhaustive */
    printf("--- Stress: Predict Linear (600 sequences) ---\n");
    {
        int s2_ok = 1;
        for (int d = -50; d <= 50; d++) {
            if (d == 0) continue;
            for (int len = 3; len <= 8; len++) {
                int vals[16]; vals[0] = 100;
                for (int i = 1; i < len; i++) vals[i] = vals[i-1] + d;
                int pt;
                int pred = ot_predict_sequence(vals, len, &pt);
                int expected = vals[len-1] + d;
                if (pred != expected || pt != 2) s2_ok = 0;
            }
        }
        check("S2 all linear predicted", 1, s2_ok);
    }

    /* S3: Predict geometric */
    printf("--- Stress: Predict Geometric (36 sequences) ---\n");
    {
        int s3_ok = 1;
        for (int base = 2; base <= 10; base++) {
            for (int ratio = 2; ratio <= 5; ratio++) {
                int vals[6]; vals[0] = base;
                for (int i = 1; i < 6; i++) vals[i] = vals[i-1] * ratio;
                int pt;
                int pred = ot_predict_sequence(vals, 5, &pt);
                int expected = vals[4] * ratio;
                if (pred != expected || pt != 3) s3_ok = 0;
            }
        }
        check("S3 all geometric predicted", 1, s3_ok);
    }

    /* S4: Predict acceleration */
    printf("--- Stress: Predict Acceleration (200 sequences) ---\n");
    {
        int s4_ok = 1;
        for (int a = -10; a <= 10; a++) {
            if (a == 0) continue;
            for (int d0 = -5; d0 <= 5; d0++) {
                int vals[6]; vals[0] = 0;
                int delta = d0;
                for (int i = 1; i < 6; i++) { vals[i] = vals[i-1] + delta; delta += a; }
                int pt;
                int pred = ot_predict_sequence(vals, 6, &pt);
                int expected = vals[5] + delta;
                if (pred != expected) s4_ok = 0;
            }
        }
        check("S4 all acceleration predicted", 1, s4_ok);
    }

    /* S5: Observe density all bytes */
    printf("--- Stress: Observe Density (256 bytes) ---\n");
    {
        int s5_ok = 1;
        for (int v = 0; v < 256; v++) {
            uint8_t byte = (uint8_t)v;
            BitStream b; bs_init(&b);
            for (int bit = 7; bit >= 0; bit--)
                bs_push(&b, (byte >> bit) & 1);
            OneTwoResult o = ot_observe(&b);
            int pop = 0;
            for (int bit = 0; bit < 8; bit++) if (byte & (1 << bit)) pop++;
            int expected = pop * 100 / 8;
            if (o.density != expected) s5_ok = 0;
        }
        check("S5 density correct for all bytes", 1, s5_ok);
    }

    /* S6: Observe period detection */
    printf("--- Stress: Period Detection (periods 2-32) ---\n");
    {
        int s6_ok = 1;
        for (int p = 2; p <= 32; p++) {
            BitStream b; bs_init(&b);
            for (int rep = 0; rep < 256 / p + 4; rep++)
                for (int i = 0; i < p && b.len < 512; i++)
                    bs_push(&b, i < p/2 ? 1 : 0);
            int found = ot_find_period(&b);
            if (found <= 0 || p % found != 0) s6_ok = 0;
        }
        check("S6 periods detected", 1, s6_ok);
    }

    /* S7: Observe symmetry (repeated halves) */
    printf("--- Stress: Symmetry (50 random halves) ---\n");
    {
        int s7_ok = 1;
        uint64_t rng_s = 0xCAFEBABE12345678ULL;
        for (int trial = 0; trial < 50; trial++) {
            BitStream b; bs_init(&b);
            rng_s ^= rng_s << 13; rng_s ^= rng_s >> 7; rng_s ^= rng_s << 17;
            int half = 16 + (int)(rng_s % 48);
            uint8_t bits[128];
            for (int i = 0; i < half; i++) {
                rng_s ^= rng_s << 13; rng_s ^= rng_s >> 7; rng_s ^= rng_s << 17;
                bits[i] = rng_s & 1;
            }
            for (int i = 0; i < half; i++) bs_push(&b, bits[i]);
            for (int i = 0; i < half; i++) bs_push(&b, bits[i]);
            int sym = ot_measure_symmetry(&b);
            if (sym != 100) s7_ok = 0;
        }
        check("S7 repeated halves = 100% symmetry", 1, s7_ok);
    }

    /* S8: Observe invariants (1000 random bitstreams) */
    printf("--- Stress: Observe Invariants (1000 random) ---\n");
    {
        int s8_ok = 1;
        uint64_t rng_s = 0xDEADBEEF01234567ULL;
        for (int trial = 0; trial < 1000; trial++) {
            rng_s ^= rng_s << 13; rng_s ^= rng_s >> 7; rng_s ^= rng_s << 17;
            int len = 1 + (int)(rng_s % 64);
            uint8_t data[64];
            for (int i = 0; i < len; i++) {
                rng_s ^= rng_s << 13; rng_s ^= rng_s >> 7; rng_s ^= rng_s << 17;
                data[i] = rng_s & 0xFF;
            }
            BitStream b; bs_init(&b);
            for (int i = 0; i < len; i++)
                for (int bit = 7; bit >= 0; bit--)
                    bs_push(&b, (data[i] >> bit) & 1);
            OneTwoResult o = ot_observe(&b);
            if (o.density < 0 || o.density > 100) s8_ok = 0;
            if (o.symmetry < 0 || o.symmetry > 100) s8_ok = 0;
            if (o.period < 0) s8_ok = 0;
        }
        check("S8 density/symmetry/period in range", 1, s8_ok);
    }

    /* S9: Edge inputs */
    printf("--- Stress: Edge Inputs ---\n");
    {
        BitStream b0; bs_init(&b0);
        OneTwoResult o0 = ot_observe(&b0);
        check("S9 empty density=0", 0, o0.density);

        BitStream b1; bs_init(&b1); bs_push(&b1, 1);
        OneTwoResult o1 = ot_observe(&b1);
        check("S9 single_1 density=100", 100, o1.density);

        BitStream b2; bs_init(&b2); bs_push(&b2, 0);
        OneTwoResult o2 = ot_observe(&b2);
        check("S9 single_0 density=0", 0, o2.density);

        BitStream bmax; bs_init(&bmax);
        for (int i = 0; i < BS_MAXBITS; i++) bs_push(&bmax, i & 1);
        OneTwoResult omax = ot_observe(&bmax);
        check("S9 maxlen density~50", 1, (omax.density >= 49 && omax.density <= 51) ? 1 : 0);
    }

    /* S10: Determinism */
    printf("--- Stress: Determinism ---\n");
    {
        int s10_ok = 1;
        for (int trial = 0; trial < 20; trial++) {
            Engine e1, e2;
            engine_init(&e1); engine_init(&e2);
            char buf[32];
            for (int i = 0; i < 5; i++) {
                snprintf(buf, 32, "det_input_%d_%d", trial, i);
                char text[64];
                snprintf(text, 64, "determinism test data block %d iter %d padding", trial, i);
                engine_ingest_text(&e1, buf, text);
                engine_ingest_text(&e2, buf, text);
            }
            for (int t = 0; t < 50; t++) { engine_tick(&e1); engine_tick(&e2); }
            Graph *g1 = &e1.shells[0].g, *g2 = &e2.shells[0].g;
            if (g1->n_nodes != g2->n_nodes || g1->n_edges != g2->n_edges) s10_ok = 0;
            for (int i = 0; i < g1->n_nodes && s10_ok; i++) {
                if (g1->nodes[i].val != g2->nodes[i].val) s10_ok = 0;
                if (g1->nodes[i].valence != g2->nodes[i].valence) s10_ok = 0;
            }
            engine_destroy(&e1); engine_destroy(&e2);
        }
        check("S10 20 trials deterministic", 1, s10_ok);
    }

    /* S11: Values bounded under random stress */
    printf("--- Stress: Value Bounded (500 ingests) ---\n");
    {
        Engine eng; engine_init(&eng);
        int bounded = 1;
        uint64_t rng_s = 0xABCD1234ABCD1234ULL;
        for (int i = 0; i < 500; i++) {
            char name[32]; snprintf(name, 32, "stress_%d", i);
            rng_s ^= rng_s << 13; rng_s ^= rng_s >> 7; rng_s ^= rng_s << 17;
            char text[128];
            snprintf(text, 128, "random stress payload %llu iteration %d end",
                     (unsigned long long)rng_s, i);
            engine_ingest_text(&eng, name, text);
            if (i % 50 == 0) {
                for (int t = 0; t < 100; t++) engine_tick(&eng);
            }
        }
        for (int t = 0; t < 500; t++) engine_tick(&eng);
        for (int s = 0; s < eng.n_shells; s++) {
            Graph *g = &eng.shells[s].g;
            for (int n = 0; n < g->n_nodes; n++) {
                int32_t v = g->nodes[n].val;
                if (v > 1000000 || v < -1000000) bounded = 0;
            }
        }
        check("S11 all values in [-1M, +1M]", 1, bounded);
        engine_destroy(&eng);
    }

    /* S12: Stability under repeated identical input */
    printf("--- Stress: Stability (same input x100) ---\n");
    {
        Engine eng; engine_init(&eng);
        for (int i = 0; i < 100; i++) {
            engine_ingest_text(&eng, "stable_node",
                "identical content repeated many times to test convergence");
            for (int t = 0; t < 20; t++) engine_tick(&eng);
        }
        Graph *g0 = &eng.shells[0].g;
        int idx = graph_find(g0, "stable_node");
        check("S12 stable node exists", 1, idx >= 0 ? 1 : 0);
        if (idx >= 0) {
            check("S12 valence > 0", 1, g0->nodes[idx].valence > 0 ? 1 : 0);
        }
        engine_destroy(&eng);
    }

    /* S13: Novel input wakes engine after stability */
    printf("--- Stress: Novel Wake ---\n");
    {
        Engine eng; engine_init(&eng);
        for (int i = 0; i < 50; i++) {
            engine_ingest_text(&eng, "wake_base",
                "repeated baseline content for wake test convergence");
            engine_tick(&eng);
        }
        Graph *g0 = &eng.shells[0].g;
        int nodes_before = g0->n_nodes;

        engine_ingest_text(&eng, "wake_novel",
            "completely different novel input that should cause new activity");
        for (int t = 0; t < 10; t++) engine_tick(&eng);

        int nodes_after = g0->n_nodes;
        check("S13 novel input created new node", 1, nodes_after > nodes_before ? 1 : 0);
        engine_destroy(&eng);
    }

    /* S14: int32 overflow stress */
    printf("--- Stress: Overflow Guard ---\n");
    {
        Engine eng; engine_init(&eng);
        Graph *g0 = &eng.shells[0].g;
        int a = graph_add(g0, "ovf_a", 0, &eng.T);
        int b = graph_add(g0, "ovf_b", 0, &eng.T);
        int d = graph_add(g0, "ovf_d", 0, &eng.T);
        for (int n = a; n <= d; n++) {
            g0->nodes[n].layer_zero = 0;
            g0->nodes[n].identity.len = 64;
            memset(g0->nodes[n].identity.w, 0xFF, 8);
        }
        g0->nodes[a].val = 2000000000;
        g0->nodes[b].val = 2000000000;
        graph_wire(g0, a, b, d, 255, 0);

        engine_tick(&eng);
        int32_t result = g0->nodes[d].val;
        check("S14 no int32 wrap", 1, result >= 0 ? 1 : 0);
        engine_destroy(&eng);
    }

    /* S15: Node pool exhaustion */
    printf("--- Stress: Node Pool Limit ---\n");
    {
        Engine eng; engine_init(&eng);
        int last_id = -1;
        for (int i = 0; i < MAX_NODES + 100; i++) {
            char name[32]; snprintf(name, 32, "pool_%d", i);
            char text[64]; snprintf(text, 64, "pool test data %d with enough length", i);
            last_id = engine_ingest_text(&eng, name, text);
        }
        check("S15 pool capped at MAX_NODES", 1,
              eng.shells[0].g.n_nodes <= MAX_NODES ? 1 : 0);
        check("S15 overflow returns -1", -1, last_id);
        engine_destroy(&eng);
    }

    /* S16: Edge pool exhaustion */
    printf("--- Stress: Edge Pool Limit ---\n");
    {
        Engine eng; engine_init(&eng);
        Graph *g0 = &eng.shells[0].g;
        for (int i = 0; i < 100; i++) {
            char name[32]; snprintf(name, 32, "edg_%d", i);
            int id = graph_add(g0, name, 0, &eng.T);
            if (id >= 0) {
                g0->nodes[id].layer_zero = 0;
                g0->nodes[id].identity.len = 64;
            }
        }
        int wire_count = 0, fail_count = 0;
        for (int i = 0; i < 100; i++)
            for (int j = 0; j < 100; j++)
                for (int d = 0; d < 100 && wire_count + fail_count < MAX_EDGES + 1000; d++) {
                    int r = graph_wire(g0, i, j, d, 128, 0);
                    if (r >= 0) wire_count++; else fail_count++;
                }
        check("S16 edges capped at MAX_EDGES", 1,
              g0->n_edges <= MAX_EDGES ? 1 : 0);
        check("S16 had failures (overflow rejected)", 1, fail_count > 0 ? 1 : 0);
        engine_destroy(&eng);
    }
}
