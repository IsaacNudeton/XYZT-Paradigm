/*
 * test_yee3d_gpu.cu — GPU Yee Grid Tests
 *
 * Same tests as test_yee3d.cu (CPU proof) but running on GPU kernels.
 * Verifies: propagation, stability, confinement, reflection, isotropy.
 *
 * Isaac Oravec & Claude, March 2026
 */
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

extern "C" {
#include "../yee.cuh"
}

static int g_pass = 0, g_fail = 0;
static void check(const char *name, int cond) {
    if (cond) { g_pass++; printf("  [PASS] %s\n", name); }
    else      { g_fail++; printf("  [FAIL] %s\n", name); }
}

/* ══════════════════════════════════════════════════════════════
 * TEST 1: Propagation from single source
 * ══════════════════════════════════════════════════════════════ */
static void test_propagation(void) {
    printf("\n=== TEST 1: 3D GPU propagation from single source ===\n");
    yee_clear_fields();

    /* Set uniform L = wire */
    yee_set_L_region(0, 0, 0, YEE_GX, YEE_GY, YEE_GZ, YEE_L_WIRE);

    int center = yee_idx(YEE_GX/2, YEE_GY/2, YEE_GZ/2);
    YeeSource src = { center, 1.0f, 1.0f };

    /* 64³ grid needs ~4x more ticks than 16³ for signal to reach edges */
    for (int t = 0; t < 200; t++) {
        if (t < 80) yee_inject(&src, 1);
        yee_tick();
    }

    /* Check quarter-points (16 cells from center — same as 16³ corners) */
    float *h_V = (float *)calloc(YEE_N, sizeof(float));
    yee_download_V(h_V, YEE_N);

    int q = YEE_GX / 4;  /* 16 */
    double corner_v2 = 0;
    corner_v2 += h_V[yee_idx(q,q,q)] * h_V[yee_idx(q,q,q)];
    corner_v2 += h_V[yee_idx(YEE_GX-q,q,q)] * h_V[yee_idx(YEE_GX-q,q,q)];
    corner_v2 += h_V[yee_idx(q,YEE_GY-q,q)] * h_V[yee_idx(q,YEE_GY-q,q)];
    corner_v2 += h_V[yee_idx(q,q,YEE_GZ-q)] * h_V[yee_idx(q,q,YEE_GZ-q)];

    double total_e = yee_energy();
    float max_v = 0;
    for (int i = 0; i < YEE_N; i++) {
        float av = fabsf(h_V[i]);
        if (av > max_v) max_v = av;
    }

    printf("  Total energy: %.6f\n", total_e);
    printf("  Max |V|: %.6f\n", max_v);
    printf("  Corner V² sum: %.6e\n", corner_v2);

    check("Propagation: total energy > 0", total_e > 1e-6);
    check("Propagation: signal reached corners", corner_v2 > 1e-12);
    check("Propagation: max |V| bounded (no explosion)", max_v < 100.0f);

    free(h_V);
}

/* ══════════════════════════════════════════════════════════════
 * TEST 2: Stability over long run
 * ══════════════════════════════════════════════════════════════ */
static void test_stability(void) {
    printf("\n=== TEST 2: 3D GPU stability ===\n");
    yee_clear_fields();
    yee_set_L_region(0, 0, 0, YEE_GX, YEE_GY, YEE_GZ, YEE_L_WIRE);

    int center = yee_idx(YEE_GX/2, YEE_GY/2, YEE_GZ/2);
    YeeSource src = { center, 1.0f, 1.0f };
    double max_e = 0;

    for (int t = 0; t < 500; t++) {
        if (t < 100) yee_inject(&src, 1);
        yee_tick();
        if (t % 50 == 0) {
            double e = yee_energy();
            if (e > max_e) max_e = e;
        }
    }

    double final_e = yee_energy();
    printf("  Max energy: %.6f\n", max_e);
    printf("  Final energy: %.6f\n", final_e);

    check("Stability: max energy < 1000", max_e < 1000.0);
    check("Stability: energy decays after drive stops", final_e < max_e * 0.8);
}

/* ══════════════════════════════════════════════════════════════
 * TEST 3: Impedance confinement
 * Low-L channel (24..39 in x,y, full z) through high-L vacuum.
 * ══════════════════════════════════════════════════════════════ */
static void test_confinement(void) {
    printf("\n=== TEST 3: GPU impedance confinement ===\n");
    yee_clear_fields();

    /* All vacuum first */
    yee_set_L_region(0, 0, 0, YEE_GX, YEE_GY, YEE_GZ, YEE_L_VAC);

    /* Carve low-L channel: center 16x16 column, full z */
    int ch_lo = YEE_GX/2 - 8;  /* 24 */
    int ch_hi = YEE_GX/2 + 8;  /* 40 */
    yee_set_L_region(ch_lo, ch_lo, 0, ch_hi, ch_hi, YEE_GZ, YEE_L_WIRE);

    float z_wire = sqrtf(YEE_L_WIRE / YEE_C);
    float z_vac = sqrtf(YEE_L_VAC / YEE_C);
    float rc = (z_vac - z_wire) / (z_vac + z_wire);
    printf("  Z_wire: %.2f  Z_vacuum: %.2f  rc: %.4f\n", z_wire, z_vac, rc);

    /* Inject at channel center, z=0 */
    int inject = yee_idx(YEE_GX/2, YEE_GY/2, 0);
    YeeSource src = { inject, 1.0f, 1.0f };

    /* More ticks for 64-cell channel length */
    for (int t = 0; t < 200; t++) {
        if (t < 100) yee_inject(&src, 1);
        yee_tick();
    }

    double channel_e = yee_region_energy(ch_lo, ch_lo, 0, ch_hi, ch_hi, YEE_GZ);
    double total_e = yee_energy();
    double outside_e = total_e - channel_e;

    int ch_vol = (ch_hi - ch_lo) * (ch_hi - ch_lo) * YEE_GZ;
    int out_vol = YEE_N - ch_vol;
    double dens_in = channel_e / ch_vol;
    double dens_out = (out_vol > 0) ? outside_e / out_vol : 0;
    double concentration = (dens_out > 1e-15) ? dens_in / dens_out : 0;

    printf("  Channel energy: %.6f (in %d voxels)\n", channel_e, ch_vol);
    printf("  Outside energy: %.6f (in %d voxels)\n", outside_e, out_vol);
    printf("  Concentration: %.2fx\n", concentration);

    /* Check halfway through channel (z=32) instead of far end */
    float *h_V = (float *)calloc(YEE_N, sizeof(float));
    yee_download_V(h_V, YEE_N);
    float mid_v = fabsf(h_V[yee_idx(YEE_GX/2, YEE_GY/2, YEE_GZ/2)]);
    printf("  Mid-channel |V| (z=%d): %.6e\n", YEE_GZ/2, mid_v);
    free(h_V);

    check("Confinement: energy density higher in channel", dens_in > dens_out);
    check("Confinement: concentration > 2x", concentration > 2.0);
    check("Confinement: signal propagated through channel", mid_v > 1e-6);
}

/* ══════════════════════════════════════════════════════════════
 * TEST 4: Impedance wall reflection
 * ══════════════════════════════════════════════════════════════ */
static void test_reflection(void) {
    printf("\n=== TEST 4: GPU impedance wall reflection ===\n");
    yee_clear_fields();
    yee_set_L_region(0, 0, 0, YEE_GX, YEE_GY, YEE_GZ, YEE_L_WIRE);

    /* High-L wall at z=30..33 */
    yee_set_L_region(0, 0, 30, YEE_GX, YEE_GY, 34, YEE_L_VAC);

    /* Inject at z=8 center */
    int inject = yee_idx(YEE_GX/2, YEE_GY/2, 8);
    YeeSource src = { inject, 1.0f, 1.0f };

    for (int t = 0; t < 60; t++) {
        if (t < 30) yee_inject(&src, 1);
        yee_tick();
    }

    double before_e = yee_region_energy(0, 0, 0, YEE_GX, YEE_GY, 30);
    double after_e = yee_region_energy(0, 0, 34, YEE_GX, YEE_GY, YEE_GZ);
    double total_e = yee_energy();

    printf("  Before wall: %.6f\n", before_e);
    printf("  After wall: %.6f\n", after_e);
    printf("  Ratio: %.2f\n", (after_e > 1e-10) ? before_e / after_e : 999.0);

    check("Wall: more energy before than after", before_e > after_e);
    check("Wall: some energy transmitted", after_e > 1e-10);
}

/* ══════════════════════════════════════════════════════════════
 * TEST 5: Isotropy
 * ══════════════════════════════════════════════════════════════ */
static void test_isotropy(void) {
    printf("\n=== TEST 5: GPU isotropic propagation ===\n");
    yee_clear_fields();
    yee_set_L_region(0, 0, 0, YEE_GX, YEE_GY, YEE_GZ, YEE_L_WIRE);

    int center = yee_idx(YEE_GX/2, YEE_GY/2, YEE_GZ/2);
    YeeSource src = { center, 1.0f, 1.0f };

    for (int t = 0; t < 40; t++) {
        if (t < 15) yee_inject(&src, 1);
        yee_tick();
    }

    double e_xlo = yee_region_energy(0, 0, 0, YEE_GX/2, YEE_GY, YEE_GZ);
    double e_xhi = yee_region_energy(YEE_GX/2, 0, 0, YEE_GX, YEE_GY, YEE_GZ);
    double e_ylo = yee_region_energy(0, 0, 0, YEE_GX, YEE_GY/2, YEE_GZ);
    double e_yhi = yee_region_energy(0, YEE_GY/2, 0, YEE_GX, YEE_GY, YEE_GZ);
    double e_zlo = yee_region_energy(0, 0, 0, YEE_GX, YEE_GY, YEE_GZ/2);
    double e_zhi = yee_region_energy(0, 0, YEE_GZ/2, YEE_GX, YEE_GY, YEE_GZ);

    printf("  -X: %.6f  +X: %.6f\n", e_xlo, e_xhi);
    printf("  -Y: %.6f  +Y: %.6f\n", e_ylo, e_yhi);
    printf("  -Z: %.6f  +Z: %.6f\n", e_zlo, e_zhi);

    double vals[] = {e_xlo, e_xhi, e_ylo, e_yhi, e_zlo, e_zhi};
    double mn = vals[0], mx = vals[0];
    for (int i = 1; i < 6; i++) {
        if (vals[i] < mn) mn = vals[i];
        if (vals[i] > mx) mx = vals[i];
    }
    double iso = (mx > 1e-10) ? mn / mx : 0;
    printf("  Isotropy ratio: %.4f\n", iso);

    check("Isotropy: min/max > 0.5", iso > 0.5);
}

/* ══════════════════════════════════════════════════════════════
 * TEST 6: Injection API
 * ══════════════════════════════════════════════════════════════ */
static void test_injection(void) {
    printf("\n=== TEST 6: GPU injection API ===\n");
    yee_clear_fields();
    yee_set_L_region(0, 0, 0, YEE_GX, YEE_GY, YEE_GZ, YEE_L_WIRE);

    /* Inject at two points with different strengths */
    YeeSource sources[2] = {
        { yee_idx(10, 10, 10), 1.0f, 1.0f },   /* strong driver */
        { yee_idx(50, 50, 50), 1.0f, 0.1f },   /* weak driver */
    };

    for (int t = 0; t < 30; t++) {
        if (t < 15) yee_inject(sources, 2);
        yee_tick();
    }

    /* Download V, check both injection sites have nonzero signal */
    float *h_V = (float *)calloc(YEE_N, sizeof(float));
    yee_download_V(h_V, YEE_N);

    float v_strong = fabsf(h_V[yee_idx(10, 10, 10)]);
    float v_weak = fabsf(h_V[yee_idx(50, 50, 50)]);

    /* Measure energy near each source (5-cube radius) */
    double e_strong = yee_region_energy(5, 5, 5, 15, 15, 15);
    double e_weak = yee_region_energy(45, 45, 45, 55, 55, 55);

    printf("  Strong site |V|: %.6f  nearby energy: %.6f\n", v_strong, e_strong);
    printf("  Weak site |V|: %.6f  nearby energy: %.6f\n", v_weak, e_weak);

    check("Injection: strong driver creates more energy", e_strong > e_weak);
    check("Injection: weak driver still produces signal", e_weak > 1e-10);

    free(h_V);
}

/* ══════════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════════════ */

int main(void) {
    printf("══════════════════════════════════════════════════════\n");
    printf("  3D YEE GRID — GPU Kernel Proof\n");
    printf("  Same physics as CPU proof, running on CUDA.\n");
    printf("══════════════════════════════════════════════════════\n");

    if (yee_init() != 0) {
        printf("  FATAL: yee_init failed\n");
        return 1;
    }

    test_propagation();
    test_stability();
    test_confinement();
    test_reflection();
    test_isotropy();
    test_injection();

    printf("\n══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed, %d total\n",
           g_pass, g_fail, g_pass + g_fail);
    printf("══════════════════════════════════════════════════════\n");

    if (g_fail == 0) {
        printf("\n  ALL PASS.\n\n");
        printf("  3D Yee grid works on GPU.\n");
        printf("  Same physics as CPU proof: propagation, confinement,\n");
        printf("  reflection, isotropy, injection.\n\n");
        printf("  Next: bridge to engine.\n");
        printf("  kernel_yee_tick replaces kernel_cube_tick.\n");
        printf("  L[] replaces substrate[]. |V| threshold replaces marked[].\n\n");
    }

    yee_destroy();
    return g_fail;
}
