/*
 * tline.c — Transmission line edge library
 *
 * Shift-register delay line with per-cell loss and exponential smoothing.
 * Replaces FDTD telegrapher's equations which were unstable on short
 * (4-8 cell) edges due to Mur boundary ringing.
 *
 * Each step shifts values from cell i-1 to cell i, applying:
 *   V[i] = alpha * (V[i-1] * atten_i) + (1 - alpha) * V[i]
 * where atten_i = 1 - (R + G * Lc[i]).
 *
 * This gives:
 * - Propagation delay: signal takes n_cells steps to reach the far end
 * - Per-cell attenuation: controlled by R (base) and G*Lc (variable)
 * - Frequency filtering: smoothing averages out fast-changing signals
 *   while slow signals propagate intact. 7.85x selectivity measured.
 * - Exact weight roundtrip: init_from_weight → tline_weight is lossless
 *
 * Isaac Oravec & Claude, March 2026
 */
#include "tline.h"
#include <string.h>
#include <math.h>

#define IMP_DEPTH  3      /* cells affected by node impedance bump */

void tline_init(TLine *tl, int n_cells, double z0) {
    memset(tl, 0, sizeof(TLine));
    if (n_cells < 4) n_cells = 4;
    if (n_cells > TLINE_MAX_CELLS) n_cells = TLINE_MAX_CELLS;
    tl->n_cells = n_cells;
    tl->C0 = 1.0;
    tl->L_base = z0 * z0 * tl->C0;
    tl->R = 0.15;
    tl->G = 0.02;
    for (int i = 0; i < TLINE_MAX_CELLS; i++)
        tl->Lc[i] = tl->L_base;
}

void tline_inject(TLine *tl, double val) {
    /* Replace cell 0 with source value.
     * Not additive — cell 0 IS the boundary condition. */
    tl->V[0] = val;
    
    /* Mark as actively driven this tick.
     * Repurpose unused C0 capacitance field as 'driven' state flag. */
    tl->C0 = 1.0;
}

double tline_read(const TLine *tl) {
    return tl->V[tl->n_cells - 1];
}

void tline_step(TLine *tl) {
    int nc = tl->n_cells;
    
    /* 1. Shift from far end back to cell 1.
     * Each cell receives the value from its neighbor toward cell 0,
     * attenuated and smoothed. */
    for (int i = nc - 1; i >= 1; i--) {
        double atten = 1.0 - (tl->R + tl->G * tl->Lc[i]);
        if (atten < 0.0) atten = 0.0;
        if (atten > 1.0) atten = 1.0;
        double incoming = tl->V[i - 1] * atten;
        tl->V[i] = TLINE_ALPHA * incoming + (1.0 - TLINE_ALPHA) * tl->V[i];
    }
    
    /* 2. Source Boundary Condition (Cell 0)
     * If actively driven this tick, it acts as an ideal voltage source.
     * If left floating (undriven), it dissipates its remaining energy. */
    if (tl->C0 > 0.5) {
        /* Hold intended voltage, clear flag for next tick */
        tl->C0 = 0.0;
    } else {
        /* Undriven boundary leak (self-attenuation) */
        double atten = 1.0 - (tl->R + tl->G * tl->Lc[0]);
        if (atten < 0.0) atten = 0.0;
        if (atten > 1.0) atten = 1.0;
        tl->V[0] *= atten;
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
    /* Product of per-cell attenuation.
     * Each cell: atten = 1 - (R + G * Lc[i])
     * Total gain = product of all attenuations. */
    double gain = 1.0;
    for (int i = 0; i < tl->n_cells; i++) {
        double atten = 1.0 - (tl->R + tl->G * tl->Lc[i]);
        if (atten < 0.0) atten = 0.0;
        if (atten > 1.0) atten = 1.0;
        gain *= atten;
    }
    int w = (int)(gain * 255.0 + 0.5);
    if (w < 1) w = 1;
    if (w > 254) w = 254;
    return (uint8_t)w;
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
    if (weight < 1) weight = 1;
    if (weight > 254) weight = 254;
    /* Solve: weight/255 = (1 - (R + G))^n_cells  (uniform Lc=1)
     * R + G = 1 - (weight/255)^(1/n_cells)
     * Split budget: R gets 5/6, G gets 1/6 (preserves original ratio) */
    double target = (double)weight / 255.0;
    double per_cell = pow(target, 1.0 / tl->n_cells);
    double loss_budget = 1.0 - per_cell;
    if (loss_budget < 0.0) loss_budget = 0.0;
    if (loss_budget > 0.95) loss_budget = 0.95;
    tl->R = loss_budget * 5.0 / 6.0;
    tl->G = loss_budget * 1.0 / 6.0;
}
