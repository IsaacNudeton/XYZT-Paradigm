/*
 * test_tline_standalone.c — 7 standalone tests for tline library
 *
 * Compiles independently: gcc -O2 -o test_tline_sa.exe tests/test_tline_standalone.c tline.c -lm
 * Does NOT depend on engine.h, engine.c, or any engine test infrastructure.
 *
 * Tests:
 *   T1: Propagation delay (signal arrives late, not instant)
 *   T2: Attenuation (amplitude drops with distance)
 *   T3: Frequency-dependent loss (slow survives, fast dies)
 *   T4: Z-depth observation (same pulse, different depths, different info)
 *   T5: Impedance matching (reflection at mismatch)
 *   T6: Back-reaction (collision grows Lc)
 *   T7: Backward compatibility (tline_weight returns stable uint8_t)
 */
#include <stdio.h>
#include <math.h>
#include "../tline.h"

static int g_pass = 0, g_fail = 0;

static void check(const char *name, int pass) {
    if (pass) { g_pass++; }
    else { g_fail++; printf("  FAIL: %s\n", name); }
}

/*
 * T1: Propagation delay — inject pulse at cell 0, verify it arrives
 * at the far end LATER, not at tick 0.
 */
static void test_propagation_delay(void) {
    printf("  T1: propagation delay\n");
    TLine tl;
    tline_init(&tl, 16, 1.0);
    tl.R = 0.01; tl.G = 0.002;  /* light loss for this test */

    tline_inject(&tl, 1.0);

    /* Far end should be ~0 initially */
    double v0 = tline_read(&tl);
    check("T1: far end starts near zero", fabs(v0) < 0.01);

    /* Step until signal arrives */
    int arrival = -1;
    for (int s = 0; s < 200; s++) {
        tline_step(&tl);
        if (arrival < 0 && fabs(tline_read(&tl)) > 0.05)
            arrival = s;
        /* Don't keep injecting — single pulse */
    }
    printf("    arrival at step %d (n_cells=%d)\n", arrival, tl.n_cells);
    check("T1: signal arrives after step 0", arrival > 0);
    check("T1: signal arrives within 200 steps", arrival > 0 && arrival < 200);
}

/*
 * T2: Attenuation — pulse amplitude drops as it propagates.
 */
static void test_attenuation(void) {
    printf("  T2: attenuation\n");

    /* Compare peaks at near and far end — far end must be smaller.
     * Use moderate loss so the effect is clear over 16 cells. */
    TLine tl;
    tline_init(&tl, 16, 1.0);
    tl.R = 0.08; tl.G = 0.02;  /* higher loss for clear attenuation */

    tline_inject(&tl, 1.0);
    double peak_near = 0, peak_far = 0;
    for (int s = 0; s < 200; s++) {
        tline_step(&tl);
        double vn = fabs(tline_read_at(&tl, 2));
        double vf = fabs(tline_read(&tl));
        if (vn > peak_near) peak_near = vn;
        if (vf > peak_far) peak_far = vf;
    }
    printf("    near peak (cell 2)=%.4f, far peak (cell 15)=%.4f\n", peak_near, peak_far);
    check("T2: far-end peak < near-end peak (attenuated)", peak_far < peak_near);
    check("T2: far-end peak > 0.01 (signal arrived)", peak_far > 0.01);
}

/*
 * T3: Frequency-dependent loss — slow signal survives better than fast.
 */
static void test_frequency_dependent_loss(void) {
    printf("  T3: frequency-dependent loss\n");

    /* Fast signal: alternating +1/-1 every tick */
    TLine tl_fast;
    tline_init(&tl_fast, 16, 1.0);
    double peak_fast = 0;
    for (int s = 0; s < 200; s++) {
        tline_inject(&tl_fast, (s % 2 == 0) ? 1.0 : -1.0);
        tline_step(&tl_fast);
        if (s > 50) {  /* after warmup */
            double v = fabs(tline_read(&tl_fast));
            if (v > peak_fast) peak_fast = v;
        }
    }

    /* Slow signal: +1 for 20 ticks, -1 for 20 ticks */
    TLine tl_slow;
    tline_init(&tl_slow, 16, 1.0);
    double peak_slow = 0;
    for (int s = 0; s < 200; s++) {
        double val = ((s / 20) % 2 == 0) ? 1.0 : -1.0;
        tline_inject(&tl_slow, val);
        tline_step(&tl_slow);
        if (s > 50) {
            double v = fabs(tline_read(&tl_slow));
            if (v > peak_slow) peak_slow = v;
        }
    }

    printf("    fast peak=%.4f, slow peak=%.4f\n", peak_fast, peak_slow);
    check("T3: slow signal has higher peak at far end", peak_slow > peak_fast);
}

/*
 * T4: Z-depth observation — same pulse, read at different cells.
 * Closer cells see it sooner and sharper.
 */
static void test_z_depth_observation(void) {
    printf("  T4: Z-depth observation\n");
    TLine tl;
    tline_init(&tl, 16, 1.0);
    tl.R = 0.02; tl.G = 0.005;

    tline_inject(&tl, 1.0);

    int arrival[4] = {-1, -1, -1, -1};
    int cells[4] = {2, 5, 9, 14};
    double peak[4] = {0, 0, 0, 0};

    for (int s = 0; s < 200; s++) {
        tline_step(&tl);
        for (int c = 0; c < 4; c++) {
            double v = fabs(tline_read_at(&tl, cells[c]));
            if (v > peak[c]) peak[c] = v;
            if (arrival[c] < 0 && v > 0.05) arrival[c] = s;
        }
    }

    printf("    cell  arrival  peak\n");
    for (int c = 0; c < 4; c++)
        printf("    %4d  %7d  %.4f\n", cells[c], arrival[c], peak[c]);

    check("T4: cell 2 arrives before cell 14", arrival[0] < arrival[3]);
    check("T4: cell 2 peak > cell 14 peak", peak[0] > peak[3]);
    check("T4: monotonic arrival order",
          arrival[0] <= arrival[1] && arrival[1] <= arrival[2] && arrival[2] <= arrival[3]);
}

/*
 * T5: Impedance discontinuity — Lc bump attenuates signal.
 */
static void test_impedance_matching(void) {
    printf("  T5: impedance discontinuity (mass = Lc bump = reflection)\n");

    /* Uniform line: no Lc discontinuity. Pulse propagates through.
     * Use low loss so the signal clearly reaches the far end. */
    TLine tl_u;
    tline_init(&tl_u, 16, 1.0);
    tl_u.R = 0.02; tl_u.G = 0.005;
    tline_inject(&tl_u, 1.0);
    double peak_u = 0;
    for (int s = 0; s < 200; s++) {
        tline_step(&tl_u);
        double v = fabs(tline_read(&tl_u));
        if (v > peak_u) peak_u = v;
    }

    /* Line with Lc bump in the middle (mass barrier) */
    TLine tl_m;
    tline_init(&tl_m, 16, 1.0);
    tl_m.R = 0.02; tl_m.G = 0.005;
    /* Place "mass" at cells 7-9: Lc = 10x base */
    for (int i = 7; i <= 9; i++) tl_m.Lc[i] = 10.0;
    tline_inject(&tl_m, 1.0);
    double peak_m = 0;
    for (int s = 0; s < 200; s++) {
        tline_step(&tl_m);
        double v = fabs(tline_read(&tl_m));
        if (v > peak_m) peak_m = v;
    }

    printf("    uniform far-end peak=%.4f, mass-barrier peak=%.4f\n", peak_u, peak_m);
    /* Mass barrier reflects energy → less reaches far end */
    check("T5: mass barrier reduces far-end peak", peak_m < peak_u);
}

/*
 * T6: Back-reaction — collision grows Lc, single source doesn't.
 */
static void test_backreaction(void) {
    printf("  T6: back-reaction\n");

    TLine tl;
    tline_init(&tl, 8, 1.0);
    double Lc_before = tl.Lc[0];

    /* Simulate collision: high energy at boundary */
    tline_backreaction(&tl, 100.0);
    double Lc_after_collision = tl.Lc[0];

    /* Interior cell should also grow */
    double Lc_interior_before = tl.Lc[4];

    printf("    Lc[0]: %.4f -> %.4f (collision)\n", Lc_before, Lc_after_collision);
    printf("    Lc[4]: %.4f (interior, untouched by boundary reaction)\n", Lc_interior_before);
    check("T6: boundary Lc grew after collision", Lc_after_collision > Lc_before);
    check("T6: interior Lc unchanged", fabs(Lc_interior_before - tl.L_base) < 0.001);
}

/*
 * T7: Backward compatibility — tline_weight() returns stable uint8_t.
 */
static void test_backward_compat(void) {
    printf("  T7: backward compatibility (tline_weight)\n");

    TLine tl;
    tline_init(&tl, 8, 1.0);
    uint8_t w = tline_weight(&tl);

    printf("    tline_weight(8 cells, Z0=1.0, R=0.02, G=0.005) = %d\n", w);
    check("T7: weight is in range [1, 254]", w >= 1 && w <= 254);

    /* Even higher loss → lower weight */
    TLine tl2;
    tline_init(&tl2, 8, 1.0);
    tl2.R = 0.5; tl2.G = 0.1;  /* much more loss */
    uint8_t w2 = tline_weight(&tl2);
    printf("    tline_weight(8 cells, R=0.2, G=0.05) = %d\n", w2);
    check("T7: higher loss gives lower weight", w2 < w);

    /* More cells → lower weight (longer line attenuates more) */
    TLine tl3;
    tline_init(&tl3, 24, 1.0);
    uint8_t w3 = tline_weight(&tl3);
    printf("    tline_weight(24 cells, Z0=1.0) = %d\n", w3);
    check("T7: longer line gives lower weight", w3 < w);
}

int main(void) {
    printf("\n=== TLine Standalone Tests ===\n\n");
    test_propagation_delay();
    test_attenuation();
    test_frequency_dependent_loss();
    test_z_depth_observation();
    test_impedance_matching();
    test_backreaction();
    test_backward_compat();
    printf("\n=== RESULTS: %d passed, %d failed, %d total ===\n",
           g_pass, g_fail, g_pass + g_fail);
    if (g_fail == 0) printf("ALL PASS.\n");
    return g_fail;
}
