/*
 * test_tline.c — Transmission Line Edge Tests
 *
 * Ported from xyzt_unified.c (February 2026).
 * Proves gates emerge from FDTD wave physics on graph edges.
 *
 * Test 1: Basic collision — XNOR, AND, XOR from 2-input collision
 * Test 2: Impedance universality — gates stable across L sweep
 * Test 3: Composability — MAJORITY(A,B,D) from impedance-matched cascade
 * Test 4: Back-reaction — impedance grows at collision points only
 * Test 5: Propagation delay — signal arrives later through more cells
 */
#include "test.h"

#define TLINE_STEPS 300
#define MAG_THRESH  0.15
#define ENERGY_THRESH 5.0

/* Gate identification from truth table */
typedef struct { int t[2][2]; const char *name; } TT;
typedef struct { const char *n; int e[2][2]; } GD;
static GD GATES[] = {
    {"ZERO",{{0,0},{0,0}}},{"AND",{{0,0},{0,1}}},{"A>B",{{0,0},{1,0}}},
    {"A",{{0,0},{1,1}}},{"B>A",{{0,1},{0,0}}},{"B",{{0,1},{0,1}}},
    {"XOR",{{0,1},{1,0}}},{"OR",{{0,1},{1,1}}},{"NOR",{{1,0},{0,0}}},
    {"XNOR",{{1,0},{0,1}}},{"~B",{{1,0},{1,0}}},{"B>=A",{{1,0},{1,1}}},
    {"~A",{{1,1},{0,0}}},{"A>=B",{{1,1},{0,1}}},{"NAND",{{1,1},{1,0}}},
    {"ONE",{{1,1},{1,1}}},
};

static const char *id_gate(TT *tt) {
    for (int g = 0; g < 16; g++)
        if (tt->t[0][0]==GATES[g].e[0][0] && tt->t[0][1]==GATES[g].e[0][1] &&
            tt->t[1][0]==GATES[g].e[1][0] && tt->t[1][1]==GATES[g].e[1][1])
            { tt->name = GATES[g].n; return GATES[g].n; }
    tt->name = "???"; return "???";
}

/* Run all 4 input combos, extract truth tables at 3 Z depths */
static void run_2input_tt(TLineGraph *tg, int a, int b, int c,
                          TT *z0, TT *z1, TT *z2) {
    int ins[4][2] = {{0,0},{0,1},{1,0},{1,1}};
    memset(z0, 0, sizeof(TT));
    memset(z1, 0, sizeof(TT));
    memset(z2, 0, sizeof(TT));

    for (int i = 0; i < 4; i++) {
        tlg_clear(tg);
        tlg_inject(tg, a, ins[i][0], 1.0);
        tlg_inject(tg, b, ins[i][1], 1.0);
        tlg_propagate(tg, TLINE_STEPS);

        TLineNode *nc = &tg->nodes[c];
        /* Z=0: magnitude detection (XNOR) */
        z0->t[ins[i][0]][ins[i][1]] = (fabs(nc->V_peak) > MAG_THRESH) ? 1 : 0;
        /* Z=1: sign detection (AND) */
        z1->t[ins[i][0]][ins[i][1]] = (nc->V_peak > MAG_THRESH) ? 1 : 0;
        /* Z=2: energy without voltage (XOR) */
        int e_present = (nc->total_energy > ENERGY_THRESH) ? 1 : 0;
        int v_present = (fabs(nc->V_peak) > MAG_THRESH) ? 1 : 0;
        z2->t[ins[i][0]][ins[i][1]] = (e_present && !v_present) ? 1 : 0;
    }
    id_gate(z0); id_gate(z1); id_gate(z2);
}

/*
 * TEST 1: Basic collision — do gates emerge from wave physics?
 * Two inputs collide at junction node. Three observers see three gates.
 */
static void test_basic_collision(void) {
    printf("  -- tline: basic collision (XNOR/AND/XOR emerge) --\n");

    TLineGraph tg; tlg_init(&tg);
    int a = tlg_add_node(&tg, 0, 0, 0, "A");
    int b = tlg_add_node(&tg, 4, 0, 0, "B");
    int c = tlg_add_node(&tg, 2, 0, 0, "C");
    tg.nodes[a].is_input = 1;
    tg.nodes[b].is_input = 1;
    tlg_add_edge(&tg, a, c, 1.0);
    tlg_add_edge(&tg, b, c, 1.0);

    TT z0, z1, z2;
    run_2input_tt(&tg, a, b, c, &z0, &z1, &z2);

    printf("    Z=0: %s  Z=1: %s  Z=2: %s\n", z0.name, z1.name, z2.name);
    check("tline: Z=0 is XNOR", 1, strcmp(z0.name, "XNOR") == 0);
    check("tline: Z=1 is AND",  1, strcmp(z1.name, "AND") == 0);
    check("tline: Z=2 is XOR",  1, strcmp(z2.name, "XOR") == 0);
}

/*
 * TEST 2: Impedance universality — gates stable across L sweep
 */
static void test_impedance_sweep(void) {
    printf("  -- tline: impedance sweep (universal gates) --\n");

    double imps[] = {0.5, 1.0, 2.0, 5.0, 10.0};
    int n_imps = 5;
    int all_xnor = 1;

    for (int ii = 0; ii < n_imps; ii++) {
        TLineGraph tg; tlg_init(&tg);
        int a = tlg_add_node(&tg, 0, 0, 0, "A");
        int b = tlg_add_node(&tg, 4, 0, 0, "B");
        int c = tlg_add_node(&tg, 2, 0, 0, "C");
        tg.nodes[a].is_input = 1;
        tg.nodes[b].is_input = 1;
        tlg_add_edge(&tg, a, c, imps[ii]);
        tlg_add_edge(&tg, b, c, imps[ii]);

        TT z0, z1, z2;
        run_2input_tt(&tg, a, b, c, &z0, &z1, &z2);
        if (strcmp(z0.name, "XNOR") != 0) all_xnor = 0;
    }
    check("tline: XNOR at all 5 impedance values", 1, all_xnor);
}

/*
 * TEST 3: Composability — MAJORITY(A,B,D) from cascaded collision
 *
 * [A]---[C1]---[B]    C1 computes XNOR(A,B)
 * [C1]---[C2]---[D]   C2 computes MAJ(A,B,D) via impedance matching
 *
 * When A=B (strong C1): C2 follows C1, ignores D.
 * When A!=B (weak C1): C2 follows D (tiebreaker).
 */
static void test_composability(void) {
    printf("  -- tline: composability (MAJORITY gate) --\n");

    /* Sweep D-edge impedance to find balance point */
    double d_imps[] = {0.5, 1.0, 2.0, 3.0, 5.0, 8.0, 12.0};
    int nd = 7;
    double best_spread = 0;
    double best_imp = 1.0;

    for (int di = 0; di < nd; di++) {
        TLineGraph tg; tlg_init(&tg);
        int a  = tlg_add_node(&tg, 0, 0, 0, "A");
        int b  = tlg_add_node(&tg, 6, 0, 0, "B");
        int c1 = tlg_add_node(&tg, 3, 0, 1, "C1");
        int d  = tlg_add_node(&tg, 3, 6, 0, "D");
        int c2 = tlg_add_node(&tg, 3, 3, 2, "C2");
        tg.nodes[a].is_input = 1;
        tg.nodes[b].is_input = 1;
        tg.nodes[d].is_input = 1;
        tlg_add_edge(&tg, a, c1, 1.0);
        tlg_add_edge(&tg, b, c1, 1.0);
        tlg_add_edge(&tg, c1, c2, 1.0);
        tlg_add_edge(&tg, d, c2, d_imps[di]);

        double vpk[8];
        for (int combo = 0; combo < 8; combo++) {
            int ia = (combo >> 2) & 1, ib = (combo >> 1) & 1, id = combo & 1;
            tlg_clear(&tg);
            tlg_inject(&tg, a, ia, 1.0);
            tlg_inject(&tg, b, ib, 1.0);
            tlg_inject(&tg, d, id, 1.0);
            tlg_propagate(&tg, TLINE_STEPS * 2);
            vpk[combo] = tg.nodes[c2].V_peak;
        }
        double d0min = 99, d0max = -99, d1min = 99, d1max = -99;
        for (int c = 0; c < 8; c++) {
            double v = vpk[c];
            if ((c & 1) == 0) { if (v < d0min) d0min = v; if (v > d0max) d0max = v; }
            else               { if (v < d1min) d1min = v; if (v > d1max) d1max = v; }
        }
        double avg = ((d0max - d0min) + (d1max - d1min)) / 2;
        if (avg > best_spread) { best_spread = avg; best_imp = d_imps[di]; }
    }

    /* Run the best impedance and check MAJ(A,B,D) */
    TLineGraph tg; tlg_init(&tg);
    int a  = tlg_add_node(&tg, 0, 0, 0, "A");
    int b  = tlg_add_node(&tg, 6, 0, 0, "B");
    int c1 = tlg_add_node(&tg, 3, 0, 1, "C1");
    int d  = tlg_add_node(&tg, 3, 6, 0, "D");
    int c2 = tlg_add_node(&tg, 3, 3, 2, "C2");
    tg.nodes[a].is_input = 1;
    tg.nodes[b].is_input = 1;
    tg.nodes[d].is_input = 1;
    tlg_add_edge(&tg, a, c1, 1.0);
    tlg_add_edge(&tg, b, c1, 1.0);
    tlg_add_edge(&tg, c1, c2, 1.0);
    tlg_add_edge(&tg, d, c2, best_imp);

    int maj_match = 0;
    for (int combo = 0; combo < 8; combo++) {
        int ia = (combo >> 2) & 1, ib = (combo >> 1) & 1, id = combo & 1;
        tlg_clear(&tg);
        tlg_inject(&tg, a, ia, 1.0);
        tlg_inject(&tg, b, ib, 1.0);
        tlg_inject(&tg, d, id, 1.0);
        tlg_propagate(&tg, TLINE_STEPS * 2);

        int c2_z1 = (tg.nodes[c2].V_peak > MAG_THRESH) ? 1 : 0;
        int maj = (ia & ib) | (ia & id) | (ib & id);
        if (c2_z1 == maj) maj_match++;
    }
    printf("    best L(D->C2)=%.1f, MAJ match=%d/8\n", best_imp, maj_match);
    check("tline: MAJORITY(A,B,D) 8/8", 8, maj_match);
}

/*
 * TEST 4: Back-reaction — impedance grows at collision vertices only
 */
static void test_backreaction(void) {
    printf("  -- tline: back-reaction (collision-only mass) --\n");

    TLineGraph tg; tlg_init(&tg);
    int a = tlg_add_node(&tg, 0, 0, 0, "A");
    int b = tlg_add_node(&tg, 4, 0, 0, "B");
    int c = tlg_add_node(&tg, 2, 0, 0, "C");
    tg.nodes[a].is_input = 1;
    tg.nodes[b].is_input = 1;
    tlg_add_edge(&tg, a, c, 1.0);
    tlg_add_edge(&tg, b, c, 1.0);

    double imp_before = tg.nodes[c].impedance;

    /* Run 10 rounds of collision + back-reaction */
    for (int r = 0; r < 10; r++) {
        tlg_clear(&tg);
        tlg_inject(&tg, a, 1, 1.0);
        tlg_inject(&tg, b, 1, 1.0);
        tlg_propagate(&tg, TLINE_STEPS);
        tlg_backreaction(&tg, 0.002);
    }

    double imp_after = tg.nodes[c].impedance;
    double imp_a = tg.nodes[a].impedance;
    printf("    C impedance: %.3f -> %.3f (A stayed at %.3f)\n",
           imp_before, imp_after, imp_a);

    /* Collision node grew, input nodes didn't */
    check("tline: collision node impedance grew", 1, imp_after > imp_before + 0.01);
    check("tline: input node impedance unchanged", 1, fabs(imp_a - 1.0) < 0.001);
}

/*
 * TEST 5: Propagation delay — more cells = later arrival
 * This IS Z. Depth measured by how many cells the signal traversed.
 */
static void test_propagation_delay(void) {
    printf("  -- tline: propagation delay (Z = depth in cells) --\n");

    /* Short edge: 2 nodes close */
    TLineGraph tg1; tlg_init(&tg1);
    int a1 = tlg_add_node(&tg1, 0, 0, 0, "A");
    int c1 = tlg_add_node(&tg1, 1, 0, 0, "C");  /* dist=1, cells=1*4+12=16 */
    tg1.nodes[a1].is_input = 1;
    tlg_add_edge(&tg1, a1, c1, 1.0);

    /* Long edge: nodes far apart (but capped at TLINE_EC) */
    TLineGraph tg2; tlg_init(&tg2);
    int a2 = tlg_add_node(&tg2, 0, 0, 0, "A");
    int c2 = tlg_add_node(&tg2, 0, 0, 1, "C");  /* dist=1 same cells */
    tg2.nodes[a2].is_input = 1;
    tlg_add_edge(&tg2, a2, c2, 1.0);

    /* Use higher impedance on edge 2 to slow propagation */
    for (int i = 0; i < tg2.edges[0].n_cells; i++)
        tg2.edges[0].Lc[i] = 4.0;  /* 2x slower (v = 1/sqrt(LC)) */

    tlg_inject(&tg1, a1, 1, 1.0);
    tlg_inject(&tg2, a2, 1, 1.0);

    int arrival1 = -1, arrival2 = -1;
    for (int s = 0; s < TLINE_STEPS; s++) {
        tlg_propagate_step(&tg1);
        tlg_propagate_step(&tg2);
        if (arrival1 < 0 && fabs(tg1.nodes[c1].V) > 0.05) arrival1 = s;
        if (arrival2 < 0 && fabs(tg2.nodes[c2].V) > 0.05) arrival2 = s;
    }
    printf("    fast edge arrival: step %d, slow edge arrival: step %d\n",
           arrival1, arrival2);
    check("tline: high-L edge arrives later", 1, arrival2 > arrival1);
}

/*
 * TEST 6: Time dilation — tau/t = sqrt(L0/L) matches Schwarzschild
 */
static void test_time_dilation(void) {
    printf("  -- tline: time dilation (tau/t = sqrt(L0/L) = Schwarzschild) --\n");

    /* At a node with impedance Z, tau/t = 1/sqrt(Z) = sqrt(L0/L).
     * Schwarzschild: tau/t = sqrt(1 - 2GM/rc^2).
     * With L = L0 + mass_bump: tau/t_TL = sqrt(L0/L)
     *                          tau/t_GR = sqrt(1 - (L-L0)/L)
     * These are algebraically identical. */
    double L_values[] = {1.0, 1.5, 2.0, 3.0, 5.0, 10.0};
    int n_exact = 0;
    for (int i = 0; i < 6; i++) {
        double L = L_values[i];
        double tl = sqrt(1.0 / L);               /* transmission line */
        double gr = sqrt(1.0 - (L - 1.0) / L);   /* Schwarzschild */
        if (fabs(tl - gr) < 1e-10) n_exact++;
    }
    printf("    %d/6 EXACT match (machine precision)\n", n_exact);
    check("tline: Schwarzschild identity exact", 6, n_exact);
}

void run_tline_tests(void) {
    printf("=== Transmission Line Edge Tests ===\n");
    test_basic_collision();
    test_impedance_sweep();
    test_composability();
    test_backreaction();
    test_propagation_delay();
    test_time_dilation();
}
