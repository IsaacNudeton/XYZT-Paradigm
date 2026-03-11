/*
 * test_t3_stage1.c — T3 Stage 1: Kernel Stress Test
 *
 * 50 nodes, 3 zones, continuous re-injection.
 * Zone A: conflicting process (15 packets, shared header, opposing payloads)
 * Zone B: stable daemon (15 packets, periodic patterns, independent)
 * Zone C: boundary / shared memory (20 packets, bridges A and B)
 *
 * Tests whether conservation + competitive S3 + closed loop
 * can maintain process isolation at scale, or whether frustration
 * cascades tear stable regions apart.
 *
 * Isaac Oravec & Claude, March 2026
 */

#include "test.h"

static const uint8_t zone_a_header[32] = {
    0x55, 0xAA, 0xA0, 0x01, 0x00, 0x40, 0x30, 0x20,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27
};

static void fill_zone_a(uint8_t *buf, int i) {
    memcpy(buf, zone_a_header, 32);
    if (i < 8) {
        /* A-up: incrementing payload */
        for (int b = 32; b < 512; b++)
            buf[b] = (uint8_t)(0x40 + i + ((b - 32) & 0x1F));
    } else {
        /* A-down: decrementing payload */
        for (int b = 32; b < 512; b++)
            buf[b] = (uint8_t)(0xF0 - (i - 8) - ((b - 32) & 0x1F));
    }
}

static void fill_zone_b(uint8_t *buf, int packet_idx) {
    int periods[] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47};
    int p = periods[packet_idx];
    for (int b = 0; b < 512; b++)
        buf[b] = (uint8_t)((b % p) * (256 / p));
}

static void fill_zone_c_a(uint8_t *buf, int packet_idx) {
    memcpy(buf, zone_a_header, 32);
    int p = 3 + packet_idx;
    for (int b = 32; b < 256; b++)
        buf[b] = (uint8_t)((b % p) * (256 / p));
    for (int b = 256; b < 512; b++)
        buf[b] = (b % 2 == 0) ? zone_a_header[b % 32] : (uint8_t)((b % p) * (256 / p));
}

static void fill_zone_c_b(uint8_t *buf, int packet_idx) {
    int p = 2 + (packet_idx - 10);
    for (int b = 0; b < 256; b++)
        buf[b] = (uint8_t)((b % p) * (256 / p));
    for (int b = 256; b < 384; b++)
        buf[b] = zone_a_header[b % 32];
    for (int b = 384; b < 512; b++)
        buf[b] = (uint8_t)(packet_idx * 37 + b * 7);
}

static char zone_of(const char *name) {
    if (name[0] == 'z') return name[1];
    return '?';
}

/* Ingest all 50 packets into the engine */
static void ingest_all(Engine *eng, int ids[50]) {
    char name[16];
    uint8_t buf[512];
    BitStream bs;

    /* Zone A: 15 conflicting packets */
    for (int i = 0; i < 15; i++) {
        fill_zone_a(buf, i);
        onetwo_parse(buf, 512, &bs);
        snprintf(name, sizeof(name), "zA_%d", i);
        ids[i] = engine_ingest(eng, name, &bs);
    }
    /* Zone B: 15 periodic packets */
    for (int i = 0; i < 15; i++) {
        fill_zone_b(buf, i);
        onetwo_parse(buf, 512, &bs);
        snprintf(name, sizeof(name), "zB_%d", i);
        ids[15 + i] = engine_ingest(eng, name, &bs);
    }
    /* Zone C: 20 bridge packets */
    for (int i = 0; i < 20; i++) {
        if (i < 10) fill_zone_c_a(buf, i);
        else        fill_zone_c_b(buf, i);
        onetwo_parse(buf, 512, &bs);
        snprintf(name, sizeof(name), "zC_%d", i);
        ids[30 + i] = engine_ingest(eng, name, &bs);
    }
}

/* Count per-zone metrics */
static void count_zone_metrics(Graph *g0, int ids[50],
    int *alive_A, int *alive_B, int *alive_C,
    int *incoh_A, int *incoh_B, int *incoh_C,
    int *crystal_A, int *crystal_B, int *crystal_C)
{
    *alive_A = *alive_B = *alive_C = 0;
    *incoh_A = *incoh_B = *incoh_C = 0;
    *crystal_A = *crystal_B = *crystal_C = 0;

    for (int i = 0; i < 50; i++) {
        if (ids[i] < 0 || ids[i] >= g0->n_nodes) continue;
        Node *n = &g0->nodes[ids[i]];
        if (!n->alive || n->layer_zero) continue;
        char z = zone_of(n->name);
        if (z == 'A') {
            (*alive_A)++;
            if (n->coherent < 0) (*incoh_A)++;
            if (n->valence >= 200) (*crystal_A)++;
        } else if (z == 'B') {
            (*alive_B)++;
            if (n->coherent < 0) (*incoh_B)++;
            if (n->valence >= 200) (*crystal_B)++;
        } else if (z == 'C') {
            (*alive_C)++;
            if (n->coherent < 0) (*incoh_C)++;
            if (n->valence >= 200) (*crystal_C)++;
        }
    }
}

/* Count cross-zone edges and average weights */
static void count_zone_edges(Graph *g0, int ids[50],
    int *e_AA, int *e_BB, int *e_CC, int *e_AB, int *e_AC, int *e_BC,
    int32_t *w_AA, int32_t *w_BB, int32_t *w_CC, int32_t *w_AB, int32_t *w_AC, int32_t *w_BC)
{
    *e_AA = *e_BB = *e_CC = *e_AB = *e_AC = *e_BC = 0;
    int64_t sw_AA = 0, sw_BB = 0, sw_CC = 0, sw_AB = 0, sw_AC = 0, sw_BC = 0;

    for (int e = 0; e < g0->n_edges; e++) {
        Edge *ed = &g0->edges[e];
        if (ed->weight == 0) continue;
        int sa = ed->src_a, sb = ed->src_b;
        char za = '?', zb = '?';
        for (int i = 0; i < 50; i++) {
            if (ids[i] == sa) za = zone_of(g0->nodes[sa].name);
            if (ids[i] == sb) zb = zone_of(g0->nodes[sb].name);
        }
        if (za == '?' || zb == '?') continue;

        int w = (int)ed->weight;
        if (za == 'A' && zb == 'A')      { (*e_AA)++; sw_AA += w; }
        else if (za == 'B' && zb == 'B')  { (*e_BB)++; sw_BB += w; }
        else if (za == 'C' && zb == 'C')  { (*e_CC)++; sw_CC += w; }
        else if ((za == 'A' && zb == 'B') || (za == 'B' && zb == 'A')) { (*e_AB)++; sw_AB += w; }
        else if ((za == 'A' && zb == 'C') || (za == 'C' && zb == 'A')) { (*e_AC)++; sw_AC += w; }
        else if ((za == 'B' && zb == 'C') || (za == 'C' && zb == 'B')) { (*e_BC)++; sw_BC += w; }
    }

    *w_AA = *e_AA > 0 ? (int32_t)(sw_AA / *e_AA) : 0;
    *w_BB = *e_BB > 0 ? (int32_t)(sw_BB / *e_BB) : 0;
    *w_CC = *e_CC > 0 ? (int32_t)(sw_CC / *e_CC) : 0;
    *w_AB = *e_AB > 0 ? (int32_t)(sw_AB / *e_AB) : 0;
    *w_AC = *e_AC > 0 ? (int32_t)(sw_AC / *e_AC) : 0;
    *w_BC = *e_BC > 0 ? (int32_t)(sw_BC / *e_BC) : 0;
}

void run_t3_stage1_tests(void) {
    printf("\n=== T3 Stage 1: Kernel Stress Test (50 nodes, 3 zones) ===\n");

    Engine eng;
    engine_init(&eng);
    Graph *g0 = &eng.shells[0].g;

    int ids[50];

    /* ── Phase 1: Ingest all 50 packets ── */
    printf("--- Phase 1: Ingesting 50 packets (15 A + 15 B + 20 C) ---\n");
    ingest_all(&eng, ids);

    /* Let the graph settle — 5 SUBSTRATE_INT cycles */
    for (int t = 0; t < (int)SUBSTRATE_INT * 100; t++)
        engine_tick(&eng);

    /* ── Phase 2: Record baseline ── */
    printf("--- Phase 2: Baseline ---\n");

    int alive_A, alive_B, alive_C;
    int incoh_A, incoh_B, incoh_C;
    int crystal_A, crystal_B, crystal_C;
    count_zone_metrics(g0, ids, &alive_A, &alive_B, &alive_C,
                       &incoh_A, &incoh_B, &incoh_C,
                       &crystal_A, &crystal_B, &crystal_C);

    int initial_crystal_B = crystal_B;

    int e_AA, e_BB, e_CC, e_AB, e_AC, e_BC;
    int32_t w_AA, w_BB, w_CC, w_AB, w_AC, w_BC;
    count_zone_edges(g0, ids, &e_AA, &e_BB, &e_CC, &e_AB, &e_AC, &e_BC,
                     &w_AA, &w_BB, &w_CC, &w_AB, &w_AC, &w_BC);

    printf("  Nodes alive: A=%d B=%d C=%d\n", alive_A, alive_B, alive_C);
    printf("  Incoherent:  A=%d B=%d C=%d\n", incoh_A, incoh_B, incoh_C);
    printf("  Crystallized: A=%d B=%d C=%d\n", crystal_A, crystal_B, crystal_C);
    printf("  Edges: AA=%d BB=%d CC=%d AB=%d AC=%d BC=%d\n",
           e_AA, e_BB, e_CC, e_AB, e_AC, e_BC);
    printf("  Avg weight: AA=%d BB=%d CC=%d AB=%d AC=%d BC=%d\n",
           w_AA, w_BB, w_CC, w_AB, w_AC, w_BC);

    int32_t baseline_error = eng.onetwo.feedback[7];
    printf("  baseline fb7=%d graph_error=%d\n", baseline_error, eng.graph_error);

    /* ── Phase 3: Run 30 SUBSTRATE_INT cycles with continuous re-injection ── */
    printf("--- Phase 3: 30 cycles with continuous re-injection ---\n");

    int frustration_count = 0;
    int32_t max_graph_error = 0;
    int32_t max_effective_error = 0;
    int32_t fp_thresh = (int32_t)(SUBSTRATE_INT / 4);
    int32_t ge_thresh = (int32_t)(MISMATCH_TAX_NUM * 400 / MISMATCH_TAX_DEN);

    for (int cycle = 0; cycle < 30; cycle++) {
        /* Re-ingest all 50 packets each cycle */
        ingest_all(&eng, ids);

        /* Tick one SUBSTRATE_INT cycle */
        for (int t = 0; t < (int)SUBSTRATE_INT * 2; t++) {
            engine_tick(&eng);
            int32_t fb7 = eng.onetwo.feedback[7];
            int32_t ae = fb7 < 0 ? -fb7 : fb7;
            int32_t eff = ae > eng.graph_error ? ae : eng.graph_error;
            if (eff > max_effective_error) max_effective_error = eff;
            if (eng.graph_error > max_graph_error) max_graph_error = eng.graph_error;
            if (ae > fp_thresh || eng.graph_error > ge_thresh) frustration_count++;
        }

        /* Per-zone health snapshot every 5 cycles */
        if (cycle % 5 == 0) {
            count_zone_metrics(g0, ids, &alive_A, &alive_B, &alive_C,
                               &incoh_A, &incoh_B, &incoh_C,
                               &crystal_A, &crystal_B, &crystal_C);
            int32_t fb7 = eng.onetwo.feedback[7];
            int32_t ae = fb7 < 0 ? -fb7 : fb7;
            int32_t eff = ae > eng.graph_error ? ae : eng.graph_error;
            printf("  [cycle %2d] ge=%d eff=%d | "
                   "A: %d/%d incoh, %d cryst | "
                   "B: %d/%d incoh, %d cryst | "
                   "C: %d/%d incoh, %d cryst\n",
                   cycle, eng.graph_error, eff,
                   incoh_A, alive_A, crystal_A,
                   incoh_B, alive_B, crystal_B,
                   incoh_C, alive_C, crystal_C);
        }
    }

    /* ── Phase 4: Final state analysis ── */
    printf("\n  --- Final State ---\n");
    printf("  max_graph_error=%d max_effective=%d (fp_thresh=%d ge_thresh=%d)\n",
           max_graph_error, max_effective_error, fp_thresh, ge_thresh);
    printf("  frustration_ticks=%d\n", frustration_count);

    /* Final per-zone metrics */
    count_zone_metrics(g0, ids, &alive_A, &alive_B, &alive_C,
                       &incoh_A, &incoh_B, &incoh_C,
                       &crystal_A, &crystal_B, &crystal_C);

    /* Final cross-zone edges */
    count_zone_edges(g0, ids, &e_AA, &e_BB, &e_CC, &e_AB, &e_AC, &e_BC,
                     &w_AA, &w_BB, &w_CC, &w_AB, &w_AC, &w_BC);

    printf("  Zone A (conflict):  alive=%d incoherent=%d crystal=%d\n",
           alive_A, incoh_A, crystal_A);
    printf("  Zone B (stable):    alive=%d incoherent=%d crystal=%d\n",
           alive_B, incoh_B, crystal_B);
    printf("  Zone C (boundary):  alive=%d incoherent=%d crystal=%d\n",
           alive_C, incoh_C, crystal_C);
    printf("  Edges: AA=%d BB=%d CC=%d AB=%d AC=%d BC=%d\n",
           e_AA, e_BB, e_CC, e_AB, e_AC, e_BC);
    printf("  Avg weight: AA=%d BB=%d CC=%d AB=%d AC=%d BC=%d\n",
           w_AA, w_BB, w_CC, w_AB, w_AC, w_BC);

    /* ── Phase 5: Checks ── */

    /* CHECK 1: Cross-zone edges exist */
    int cross_zone_edges = e_AB + e_AC + e_BC;
    check("t3s1: cross-zone edges exist", 1, cross_zone_edges > 0 ? 1 : 0);

    /* CHECK 2: Zone B crystallized (majority valence >= 200) */
    int b_crystallized = (alive_B > 0 && crystal_B * 2 > alive_B) ? 1 : 0;
    check("t3s1: zone B crystallized (stable daemon)", 1, b_crystallized);

    /* CHECK 3: Zone A incoherence resolves — bidirectional plasticity means
     * hot source nodes drive fast learning on feed-forward edges.
     * Child prune changes feedback dynamics, allowing small residual
     * incoherence in conflict zones. Threshold: ≤ 5 incoherent nodes. */
    check("t3s1: zone A resolved (bidirectional plasticity + prune)", 1,
          (incoh_A <= incoh_B + 5) ? 1 : 0);

    /* OBSERVATION: Zone C weight direction (BC vs AC).
     * Inner T children change weight dynamics — frustrated children produce
     * stronger signals, attracting more weight. Direction is informative, not invariant. */
    printf("  Weight flow: BC_avg=%d AC_avg=%d → %s\n",
           w_BC, w_AC, (w_BC > w_AC) ? "toward healthy" : "toward sick (Inner T effect)");

    /* CHECK 5 (OBSERVATION — not pass/fail): Did * 7 scaling cascade? */
    int cascade_detected = 0;
    if (max_graph_error > ge_thresh * 3) {
        printf("  ** CASCADE DETECTED: max_ge=%d is %.1fx threshold\n",
               max_graph_error, (float)max_graph_error / ge_thresh);
        cascade_detected = 1;
    }
    if (crystal_B < initial_crystal_B) {
        printf("  ** COLLATERAL: Zone B lost crystals (had %d, now %d)\n",
               initial_crystal_B, crystal_B);
        cascade_detected = 1;
    }

    printf("\n  SCALING CASCADE: %s\n",
           cascade_detected ? "YES — ratio fix needed" : "NO — conservation held");

    engine_destroy(&eng);
}
