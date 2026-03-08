/*
 * tline.c — Transmission line edge library
 *
 * FDTD telegrapher's equations with R,G loss terms.
 * Ported from xyzt_unified.c propagate_step().
 *
 * Loss is the key insight: R attenuates current (series),
 * G drains voltage (shunt). Together they create frequency-dependent
 * degradation. Low frequencies survive propagation. High frequencies
 * die. Cell 0 = full bandwidth. Cell N = low-pass filtered.
 * The loss gradient IS Z.
 *
 * Courant stability: DT <= dx * sqrt(L*C). With dx=1 and our default
 * parameters (L=1, C=1), DT=0.45 gives Courant number 0.45 < 1.
 */
#include "tline.h"
#include <string.h>
#include <math.h>

#define DT         0.45   /* FDTD timestep (Courant stable for L>=0.5) */
#define IMP_DEPTH  3      /* cells affected by node impedance bump */

void tline_init(TLine *tl, int n_cells, double z0) {
    memset(tl, 0, sizeof(TLine));
    if (n_cells < 4) n_cells = 4;
    if (n_cells > TLINE_MAX_CELLS) n_cells = TLINE_MAX_CELLS;
    tl->n_cells = n_cells;
    tl->C0 = 1.0;
    tl->L_base = z0 * z0 * tl->C0;  /* Z0 = sqrt(L/C), so L = Z0^2 * C */
    tl->R = 0.15;   /* series loss: calibrated so tline_weight ≈ 128 */
    tl->G = 0.02;   /* shunt loss: frequency-dependent degradation */
    for (int i = 0; i < TLINE_MAX_CELLS; i++)
        tl->Lc[i] = tl->L_base;
}

void tline_inject(TLine *tl, double val) {
    /* Inject as additive source at cell 0 only.
     * Caller drives this each tick for continuous source,
     * or once for a single pulse (which propagates and decays). */
    tl->V[0] += val;
}

double tline_read(const TLine *tl) {
    return tl->V[tl->n_cells - 1];
}

double tline_read_at(const TLine *tl, int cell) {
    if (cell < 0) cell = 0;
    if (cell >= tl->n_cells) cell = tl->n_cells - 1;
    return tl->V[cell];
}

void tline_step(TLine *tl) {
    int nc = tl->n_cells;

    /* Update I: dI/dt = -(1/L)*dV/dx - (R/L)*I */
    for (int i = 0; i < nc - 1; i++) {
        double dV = tl->V[i + 1] - tl->V[i];
        tl->I[i] += -(DT / tl->Lc[i]) * dV
                   - (DT * tl->R / tl->Lc[i]) * tl->I[i];
    }

    /* Update V: dV/dt = -(1/C)*dI/dx - (G/C)*V */
    for (int i = 1; i < nc - 1; i++) {
        double dI = tl->I[i] - tl->I[i - 1];
        tl->V[i] += -(DT / tl->C0) * dI
                   - (DT * tl->G / tl->C0) * tl->V[i];
    }
    /* Mur 1st-order absorbing boundary conditions.
     * Identical to universe_tline_v2.c. Stores previous-step values. */
    {
        double v = 1.0 / sqrt(tl->Lc[1] * tl->C0);
        double r = (v * DT - 1.0) / (v * DT + 1.0);
        tl->V[0] = tl->mur[1] + r * (tl->V[1] - tl->mur[0]);
        tl->mur[0] = tl->V[0]; tl->mur[1] = tl->V[1];
    }
    {
        double v = 1.0 / sqrt(tl->Lc[nc - 2] * tl->C0);
        double r = (v * DT - 1.0) / (v * DT + 1.0);
        tl->V[nc - 1] = tl->mur[3] + r * (tl->V[nc - 2] - tl->mur[2]);
        tl->mur[2] = tl->V[nc - 1]; tl->mur[3] = tl->V[nc - 2];
    }
}

void tline_set_impedance(TLine *tl, double z_src, double z_dst) {
    /* Source end: raise Lc for first IMP_DEPTH cells */
    if (z_src > 1.0001) {
        for (int d = 0; d < IMP_DEPTH && d < tl->n_cells; d++) {
            double scale = z_src * exp(-(double)d * 0.7);
            if (scale < 1.0) scale = 1.0;
            tl->Lc[d] = tl->L_base * scale;
        }
    }
    /* Destination end */
    if (z_dst > 1.0001) {
        int nc = tl->n_cells;
        for (int d = 0; d < IMP_DEPTH && d < nc; d++) {
            double scale = z_dst * exp(-(double)d * 0.7);
            if (scale < 1.0) scale = 1.0;
            tl->Lc[nc - 1 - d] = tl->L_base * scale;
        }
    }
}

uint8_t tline_weight(const TLine *tl) {
    /* Measure DC attenuation: ratio of far-end to near-end peak.
     * For a lossy line, steady-state DC gain = exp(-sqrt(RG) * n_cells).
     * We compute it from the actual Lc array for accuracy. */
    double total_loss = 0.0;
    for (int i = 0; i < tl->n_cells; i++) {
        double alpha = 0.5 * (tl->R / sqrt(tl->Lc[i] / tl->C0)
                             + tl->G * sqrt(tl->Lc[i] / tl->C0));
        total_loss += alpha;
    }
    double gain = exp(-total_loss);
    int w = (int)(gain * 255.0 + 0.5);
    if (w < 1) w = 1;
    if (w > 254) w = 254;
    return (uint8_t)w;
}

void tline_backreaction(TLine *tl, double collision_energy) {
    /* Grow Lc at both boundaries proportional to collision energy.
     * Mass forms where energy collides, not where it passes through. */
    double bump = 0.001 * collision_energy;
    for (int d = 0; d < IMP_DEPTH && d < tl->n_cells; d++) {
        double scale = exp(-(double)d * 0.5);
        tl->Lc[d] += bump * scale;
        tl->Lc[tl->n_cells - 1 - d] += bump * scale;
        /* Clamp */
        if (tl->Lc[d] > 50.0) tl->Lc[d] = 50.0;
        if (tl->Lc[tl->n_cells - 1 - d] > 50.0)
            tl->Lc[tl->n_cells - 1 - d] = 50.0;
    }
}

void tline_strengthen(TLine *tl, double rate) {
    for (int i = 0; i < tl->n_cells; i++) {
        tl->Lc[i] *= (1.0 - rate);
        if (tl->Lc[i] < 0.1) tl->Lc[i] = 0.1;
    }
}

void tline_weaken(TLine *tl, double rate) {
    for (int i = 0; i < tl->n_cells; i++) {
        tl->Lc[i] *= (1.0 + rate);
        if (tl->Lc[i] > 50.0) tl->Lc[i] = 50.0;
    }
}

void tline_init_from_weight(TLine *tl, uint8_t weight) {
    tline_init(tl, 8, 1.0);
    /* Scale R,G to produce the target weight.
     * Default: gain = exp(-0.5*(R+G)*n) with Lc=C=1.
     * Want: gain = weight/255.
     * Scale factor k: exp(-0.5*k*(R+G)*n) = weight/255
     *   k = -log(weight/255) / (0.5*(R+G)*n) */
    if (weight < 1) weight = 1;
    if (weight > 254) weight = 254;
    double target = (double)weight / 255.0;
    double base_alpha = 0.5 * (tl->R + tl->G) * tl->n_cells;
    if (base_alpha < 0.001) base_alpha = 0.001;
    double k = -log(target) / base_alpha;
    if (k < 0.0) k = 0.0;
    if (k > 20.0) k = 20.0;
    tl->R *= k;
    tl->G *= k;
}
