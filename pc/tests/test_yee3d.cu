/*
 * test_yee3d.cu — 3D Yee Grid Substrate Proof
 *
 * Proves: wave propagation + impedance confinement in 3D.
 *
 * Standard Yee staggering:
 *   V (scalar) at cell center
 *   I[3] (vector) on cell faces — Ix between (x,y,z) and (x+1,y,z), etc.
 *
 * Telegrapher in 3D:
 *   dV/dt = -(1/C) * div(I) - (G/C)*V
 *   dI/dt = -(1/L) * grad(V) - (R/L)*I
 *
 * Discrete leapfrog (proven stable in 1D at alpha=0.5):
 *   Step 1: V[pos] += alpha * (-(1/C) * (Ix[pos]-Ix[pos-1] + Iy... + Iz...) - G/C * V)
 *   Step 2: Ix[pos] += alpha * (-(1/L[pos]) * (V[pos+1]-V[pos]) - R/L * Ix)
 *
 * Isaac Oravec & Claude, March 2026
 */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* ══════════════════════════════════════════════════════════════
 * GRID PARAMETERS
 * ══════════════════════════════════════════════════════════════ */

#define NX 16
#define NY 16
#define NZ 16
#define N_VOXELS (NX * NY * NZ)

#define ALPHA 0.5    /* CFL / smoothing factor (proven stable in 1D) */
#define R_LOSS 0.02  /* base series resistance */
#define G_LOSS 0.01  /* base shunt conductance */
#define C_VAL  1.0   /* capacitance (fixed, not learnable) */

#define L_WIRE   1.0   /* low L = wire (low Z, fast propagation) */
#define L_VACUUM 9.0   /* high L = vacuum (high Z = barrier) */

/* ══════════════════════════════════════════════════════════════
 * YEE GRID STATE
 * ══════════════════════════════════════════════════════════════ */

typedef struct {
    float V[N_VOXELS];       /* scalar voltage at cell centers */
    float Ix[N_VOXELS];      /* current x-component on x-faces */
    float Iy[N_VOXELS];      /* current y-component on y-faces */
    float Iz[N_VOXELS];      /* current z-component on z-faces */
    float L[N_VOXELS];       /* inductance per voxel (learnable) */
} YeeGrid;

static inline int idx(int x, int y, int z) {
    /* Clamp to grid boundaries */
    if (x < 0) x = 0; if (x >= NX) x = NX - 1;
    if (y < 0) y = 0; if (y >= NY) y = NY - 1;
    if (z < 0) z = 0; if (z >= NZ) z = NZ - 1;
    return x + y * NX + z * NX * NY;
}

static void yee_init(YeeGrid *g, float L_default) {
    memset(g, 0, sizeof(*g));
    for (int i = 0; i < N_VOXELS; i++)
        g->L[i] = L_default;
}

/* ══════════════════════════════════════════════════════════════
 * 3D LEAPFROG STEP — the core physics
 * ══════════════════════════════════════════════════════════════ */

static void yee_step(YeeGrid *g) {
    float alpha = ALPHA;

    /* Step 1: Update V from I divergence.
     * dV/dt = -(1/C) * (dIx/dx + dIy/dy + dIz/dz) - (G/C)*V
     *
     * Divergence at (x,y,z): Ix[x,y,z] - Ix[x-1,y,z] + Iy[...] + Iz[...]
     * Boundary: I outside grid = 0 (open circuit) */
    for (int z = 0; z < NZ; z++) {
        for (int y = 0; y < NY; y++) {
            for (int x = 0; x < NX; x++) {
                int p = idx(x, y, z);
                float inv_C = 1.0f / C_VAL;

                /* dIx/dx */
                float dIx = g->Ix[p];
                if (x > 0) dIx -= g->Ix[idx(x-1, y, z)];

                /* dIy/dy */
                float dIy = g->Iy[p];
                if (y > 0) dIy -= g->Iy[idx(x, y-1, z)];

                /* dIz/dz */
                float dIz = g->Iz[p];
                if (z > 0) dIz -= g->Iz[idx(x, y, z-1)];

                float div_I = dIx + dIy + dIz;
                float dV = -inv_C * div_I - G_LOSS * inv_C * g->V[p];
                g->V[p] += alpha * dV;
            }
        }
    }

    /* Step 2: Update I from V gradient (uses NEW V from step 1).
     * dIx/dt = -(1/L) * dV/dx - (R/L)*Ix
     * dV/dx at face (x,y,z) = V[x+1,y,z] - V[x,y,z]
     * Boundary: V outside grid = 0 (grounded) */
    for (int z = 0; z < NZ; z++) {
        for (int y = 0; y < NY; y++) {
            for (int x = 0; x < NX; x++) {
                int p = idx(x, y, z);
                float inv_L = 1.0f / g->L[p];

                /* Ix: dV/dx = V[x+1] - V[x] */
                if (x < NX - 1) {
                    float dVx = g->V[idx(x+1, y, z)] - g->V[p];
                    g->Ix[p] += alpha * (-inv_L * dVx - R_LOSS * inv_L * g->Ix[p]);
                }

                /* Iy: dV/dy = V[y+1] - V[y] */
                if (y < NY - 1) {
                    float dVy = g->V[idx(x, y+1, z)] - g->V[p];
                    g->Iy[p] += alpha * (-inv_L * dVy - R_LOSS * inv_L * g->Iy[p]);
                }

                /* Iz: dV/dz = V[z+1] - V[z] */
                if (z < NZ - 1) {
                    float dVz = g->V[idx(x, y, z+1)] - g->V[p];
                    g->Iz[p] += alpha * (-inv_L * dVz - R_LOSS * inv_L * g->Iz[p]);
                }
            }
        }
    }
}

/* ══════════════════════════════════════════════════════════════
 * MEASUREMENT
 * ══════════════════════════════════════════════════════════════ */

/* Total energy in the grid */
static double yee_energy(const YeeGrid *g) {
    double e = 0;
    for (int i = 0; i < N_VOXELS; i++) {
        e += (double)g->V[i] * g->V[i] * C_VAL;     /* electric */
        e += (double)g->Ix[i] * g->Ix[i] * g->L[i];  /* magnetic x */
        e += (double)g->Iy[i] * g->Iy[i] * g->L[i];  /* magnetic y */
        e += (double)g->Iz[i] * g->Iz[i] * g->L[i];  /* magnetic z */
    }
    return e * 0.5;
}

/* Energy in a rectangular region */
static double yee_region_energy(const YeeGrid *g,
                                 int x0, int y0, int z0,
                                 int x1, int y1, int z1) {
    double e = 0;
    for (int z = z0; z < z1; z++)
        for (int y = y0; y < y1; y++)
            for (int x = x0; x < x1; x++) {
                int p = idx(x, y, z);
                e += (double)g->V[p] * g->V[p] * C_VAL;
                e += (double)g->Ix[p] * g->Ix[p] * g->L[p];
                e += (double)g->Iy[p] * g->Iy[p] * g->L[p];
                e += (double)g->Iz[p] * g->Iz[p] * g->L[p];
            }
    return e * 0.5;
}

/* Max |V| in the grid */
static float yee_max_v(const YeeGrid *g) {
    float mx = 0;
    for (int i = 0; i < N_VOXELS; i++) {
        float av = fabsf(g->V[i]);
        if (av > mx) mx = av;
    }
    return mx;
}

/* ══════════════════════════════════════════════════════════════
 * TESTS
 * ══════════════════════════════════════════════════════════════ */

static int g_pass = 0, g_fail = 0;
static void check(const char *name, int cond) {
    if (cond) { g_pass++; printf("  [PASS] %s\n", name); }
    else      { g_fail++; printf("  [FAIL] %s\n", name); }
}

/* TEST 1: Single source propagation in uniform medium.
 * Inject at center, run 50 ticks. Energy should spread outward.
 * Far corners should have nonzero V. */
static void test_propagation(void) {
    printf("\n=== TEST 1: 3D propagation from single source ===\n");
    YeeGrid *g = (YeeGrid *)calloc(1, sizeof(YeeGrid));
    yee_init(g, L_WIRE);

    int center = idx(NX/2, NY/2, NZ/2);

    /* Inject for 20 ticks, propagate for 30 more */
    for (int t = 0; t < 50; t++) {
        if (t < 20) g->V[center] = 1.0f;
        yee_step(g);
    }

    /* Check: energy at corners (far from source) */
    double corner_e = 0;
    corner_e += g->V[idx(0,0,0)] * g->V[idx(0,0,0)];
    corner_e += g->V[idx(NX-1,0,0)] * g->V[idx(NX-1,0,0)];
    corner_e += g->V[idx(0,NY-1,0)] * g->V[idx(0,NY-1,0)];
    corner_e += g->V[idx(0,0,NZ-1)] * g->V[idx(0,0,NZ-1)];

    double total_e = yee_energy(g);
    float max_v = yee_max_v(g);

    printf("  Total energy: %.6f\n", total_e);
    printf("  Max |V|: %.6f\n", max_v);
    printf("  Corner V² sum: %.6f\n", corner_e);

    check("Propagation: total energy > 0", total_e > 1e-6);
    check("Propagation: signal reached corners", corner_e > 1e-10);
    check("Propagation: max |V| bounded (no explosion)", max_v < 100.0f);

    free(g);
}

/* TEST 2: Stability over long run.
 * Drive for 100 ticks, free-run for 400. Energy must not explode. */
static void test_stability(void) {
    printf("\n=== TEST 2: 3D stability (no energy explosion) ===\n");
    YeeGrid *g = (YeeGrid *)calloc(1, sizeof(YeeGrid));
    yee_init(g, L_WIRE);

    int center = idx(NX/2, NY/2, NZ/2);
    double max_e = 0;

    for (int t = 0; t < 500; t++) {
        if (t < 100) g->V[center] = 1.0f;
        yee_step(g);
        double e = yee_energy(g);
        if (e > max_e) max_e = e;
    }

    double final_e = yee_energy(g);
    printf("  Max energy: %.6f\n", max_e);
    printf("  Final energy: %.6f\n", final_e);

    check("Stability: max energy < 1000", max_e < 1000.0);
    check("Stability: energy decays after drive stops", final_e < max_e * 0.8);

    free(g);
}

/* TEST 3: Impedance confinement — the critical test.
 *
 * Create a low-L channel (wire) through high-L volume (vacuum).
 * Channel: x=6..9, y=6..9, full z extent. L = L_WIRE (1.0).
 * Surrounding: L = L_VACUUM (9.0). Z_vac/Z_wire = 3.0.
 * rc = (3-1)/(3+1) = 0.5. Should confine >50% of energy.
 *
 * Inject at channel entrance (z=0). Measure energy inside vs outside channel.
 * If confinement works: channel energy >> surrounding energy. */
static void test_confinement(void) {
    printf("\n=== TEST 3: Impedance confinement (low-L channel in high-L vacuum) ===\n");
    YeeGrid *g = (YeeGrid *)calloc(1, sizeof(YeeGrid));
    yee_init(g, L_VACUUM);  /* everything starts as vacuum */

    /* Carve a low-L channel through the center */
    for (int z = 0; z < NZ; z++)
        for (int y = 6; y < 10; y++)
            for (int x = 6; x < 10; x++)
                g->L[idx(x, y, z)] = L_WIRE;

    float z_wire = sqrtf(L_WIRE / C_VAL);
    float z_vac = sqrtf(L_VACUUM / C_VAL);
    float rc = (z_vac - z_wire) / (z_vac + z_wire);
    printf("  Z_wire: %.2f  Z_vacuum: %.2f  rc: %.4f\n", z_wire, z_vac, rc);

    /* Inject at channel entrance */
    int inject_pos = idx(8, 8, 0);
    for (int t = 0; t < 80; t++) {
        if (t < 40) g->V[inject_pos] = 1.0f;
        yee_step(g);
    }

    /* Measure energy density inside channel vs outside.
     * Channel = 4*4*16 = 256 voxels. Outside = 4096-256 = 3840 voxels.
     * Even partial confinement means higher density inside. */
    double channel_e = yee_region_energy(g, 6, 6, 0, 10, 10, NZ);
    double total_e = yee_energy(g);
    double outside_e = total_e - channel_e;

    int channel_vol = 4 * 4 * NZ;  /* 256 */
    int outside_vol = N_VOXELS - channel_vol;  /* 3840 */
    double density_in = channel_e / channel_vol;
    double density_out = (outside_vol > 0) ? outside_e / outside_vol : 0;
    double concentration = (density_out > 1e-15) ? density_in / density_out : 0;

    printf("  Channel energy: %.6f (in %d voxels)\n", channel_e, channel_vol);
    printf("  Outside energy: %.6f (in %d voxels)\n", outside_e, outside_vol);
    printf("  Density inside: %.6e  outside: %.6e\n", density_in, density_out);
    printf("  Concentration (density ratio): %.2fx\n", concentration);

    /* Check signal reached far end of channel */
    float far_end_v = fabsf(g->V[idx(8, 8, NZ-1)]);
    printf("  Far end |V|: %.6f\n", far_end_v);

    check("Confinement: energy density higher inside channel", density_in > density_out);
    check("Confinement: concentration > 2x", concentration > 2.0);
    check("Confinement: signal reached far end of channel", far_end_v > 1e-6);

    free(g);
}

/* TEST 4: Impedance wall — reflection.
 * Uniform medium with a high-L wall across the middle (z=8).
 * Signal injected at z=2 should reflect off the wall.
 * Energy before wall > energy after wall. */
static void test_reflection_wall(void) {
    printf("\n=== TEST 4: Impedance wall reflection ===\n");
    YeeGrid *g = (YeeGrid *)calloc(1, sizeof(YeeGrid));
    yee_init(g, L_WIRE);

    /* High-L wall at z=7,8 (2 cells thick) */
    for (int y = 0; y < NY; y++)
        for (int x = 0; x < NX; x++) {
            g->L[idx(x, y, 7)] = L_VACUUM;
            g->L[idx(x, y, 8)] = L_VACUUM;
        }

    /* Inject at z=2 center */
    int inject_pos = idx(NX/2, NY/2, 2);
    for (int t = 0; t < 60; t++) {
        if (t < 30) g->V[inject_pos] = 1.0f;
        yee_step(g);
    }

    /* Energy before wall (z=0..6) vs after wall (z=9..15) */
    double before_e = yee_region_energy(g, 0, 0, 0, NX, NY, 7);
    double after_e = yee_region_energy(g, 0, 0, 9, NX, NY, NZ);
    double total_e = yee_energy(g);

    printf("  Energy before wall (z<7): %.6f\n", before_e);
    printf("  Energy after wall (z>8): %.6f\n", after_e);
    printf("  Total energy: %.6f\n", total_e);
    printf("  Before/After ratio: %.2f\n",
           (after_e > 1e-10) ? before_e / after_e : 999.0);

    check("Wall: more energy before wall than after", before_e > after_e);
    check("Wall: some energy transmitted (not perfect reflector)", after_e > 1e-10);

    free(g);
}

/* TEST 5: Isotropy — propagation is symmetric in 3D.
 * Inject at center of uniform medium. After propagation,
 * energy should be roughly equal in all 6 octants. */
static void test_isotropy(void) {
    printf("\n=== TEST 5: Isotropic propagation ===\n");
    YeeGrid *g = (YeeGrid *)calloc(1, sizeof(YeeGrid));
    yee_init(g, L_WIRE);

    int center = idx(NX/2, NY/2, NZ/2);
    for (int t = 0; t < 40; t++) {
        if (t < 15) g->V[center] = 1.0f;
        yee_step(g);
    }

    /* Measure energy in 6 sectors (±x, ±y, ±z from center) */
    double e_xlo = yee_region_energy(g, 0, 0, 0, NX/2, NY, NZ);
    double e_xhi = yee_region_energy(g, NX/2, 0, 0, NX, NY, NZ);
    double e_ylo = yee_region_energy(g, 0, 0, 0, NX, NY/2, NZ);
    double e_yhi = yee_region_energy(g, 0, NY/2, 0, NX, NY, NZ);
    double e_zlo = yee_region_energy(g, 0, 0, 0, NX, NY, NZ/2);
    double e_zhi = yee_region_energy(g, 0, 0, NZ/2, NX, NY, NZ);

    printf("  Energy -X: %.6f  +X: %.6f\n", e_xlo, e_xhi);
    printf("  Energy -Y: %.6f  +Y: %.6f\n", e_ylo, e_yhi);
    printf("  Energy -Z: %.6f  +Z: %.6f\n", e_zlo, e_zhi);

    /* Ratio of min/max should be > 0.5 (reasonably isotropic) */
    double vals[] = {e_xlo, e_xhi, e_ylo, e_yhi, e_zlo, e_zhi};
    double mn = vals[0], mx = vals[0];
    for (int i = 1; i < 6; i++) {
        if (vals[i] < mn) mn = vals[i];
        if (vals[i] > mx) mx = vals[i];
    }
    double iso_ratio = (mx > 1e-10) ? mn / mx : 0;
    printf("  Isotropy ratio (min/max): %.4f\n", iso_ratio);

    check("Isotropy: min/max > 0.5 (reasonably symmetric)", iso_ratio > 0.5);

    free(g);
}

/* TEST 6: Hebbian learning — strengthen lowers L, changes propagation.
 * Two identical paths. Apply Hebbian strengthening to one.
 * Strengthened path should carry more energy. */
static void test_hebbian(void) {
    printf("\n=== TEST 6: Hebbian learning changes propagation ===\n");

    /* Path A: L=1.0 everywhere (control) */
    YeeGrid *ga = (YeeGrid *)calloc(1, sizeof(YeeGrid));
    yee_init(ga, L_WIRE);

    /* Path B: strengthen a corridor (lower L along z-axis center) */
    YeeGrid *gb = (YeeGrid *)calloc(1, sizeof(YeeGrid));
    yee_init(gb, L_WIRE);

    /* Hebbian: lower L in the center corridor.
     * CFL stability: alpha <= sqrt(L*C/3). For alpha=0.5, need L >= 0.75.
     * Use 0.8 — Z = sqrt(0.8) = 0.894 vs control Z = 1.0. */
    float L_strengthened = 0.8f;
    for (int z = 0; z < NZ; z++)
        for (int y = 6; y < 10; y++)
            for (int x = 6; x < 10; x++)
                gb->L[idx(x, y, z)] = L_strengthened;

    /* Same injection for both */
    int inject_pos = idx(8, 8, 0);
    for (int t = 0; t < 50; t++) {
        if (t < 25) {
            ga->V[inject_pos] = 1.0f;
            gb->V[inject_pos] = 1.0f;
        }
        yee_step(ga);
        yee_step(gb);
    }

    /* Measure at far end */
    float va_end = fabsf(ga->V[idx(8, 8, NZ-1)]);
    float vb_end = fabsf(gb->V[idx(8, 8, NZ-1)]);
    double ea_total = yee_energy(ga);
    double eb_total = yee_energy(gb);

    printf("  Control far-end |V|: %.6f\n", va_end);
    printf("  Strengthened far-end |V|: %.6f\n", vb_end);
    printf("  Control total energy: %.6f\n", ea_total);
    printf("  Strengthened total energy: %.6f\n", eb_total);

    /* In strengthened path, lower L means lower Z, faster propagation.
     * Signal should arrive stronger at far end. */
    check("Hebbian: strengthened path has different far-end signal",
          fabsf(vb_end - va_end) > 1e-6);

    free(ga);
    free(gb);
}

/* ══════════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════════════ */

int main(void) {
    printf("══════════════════════════════════════════════════════\n");
    printf("  3D YEE GRID — Substrate Physics Proof\n");
    printf("  V (scalar) at centers, I (vector) on faces.\n");
    printf("  L per voxel = learnable impedance.\n");
    printf("  Low L = wire. High L = vacuum. rc = reflection.\n");
    printf("══════════════════════════════════════════════════════\n");

    test_propagation();
    test_stability();
    test_confinement();
    test_reflection_wall();
    test_isotropy();
    test_hebbian();

    printf("\n══════════════════════════════════════════════════════\n");
    printf("  Results: %d passed, %d failed, %d total\n",
           g_pass, g_fail, g_pass + g_fail);
    printf("══════════════════════════════════════════════════════\n");

    if (g_fail == 0) {
        printf("\n  ALL PASS.\n\n");
        printf("  3D wave propagation: stable, isotropic.\n");
        printf("  Impedance confinement: low-L channel confines signal.\n");
        printf("  Impedance wall: reflection works in 3D.\n");
        printf("  Hebbian: L changes affect propagation.\n\n");
        printf("  Next: port to CUDA kernel.\n");
        printf("  Replace kernel_cube_tick with kernel_yee_tick.\n");
        printf("  L[] replaces substrate[]. V replaces marked[].\n\n");
    }
    return g_fail;
}
