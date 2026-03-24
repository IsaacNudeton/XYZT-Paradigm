/*
 * tline.h — Transmission line edge library
 *
 * Shift-register delay line with per-cell loss and smoothing.
 * Replaces FDTD (which was unstable on short edges).
 *
 * Each cell applies: new = alpha * (incoming * atten) + (1-alpha) * old
 * where atten = 1 - (R + G*Lc[i]).
 *
 * R controls base loss, G*Lc controls per-cell variable loss.
 * Smoothing (alpha) creates frequency-dependent filtering:
 * slow signals propagate, fast signals get averaged out.
 * That gradient IS Z.
 *
 * Isaac Oravec & Claude, March 2026
 */
#ifndef TLINE_H
#define TLINE_H

#include <stdint.h>

#define TLINE_MAX_CELLS 32
#define TLINE_ALPHA     0.5    /* smoothing: 0=frozen, 1=no smoothing */

typedef struct {
    double V[TLINE_MAX_CELLS];
    double Lc[TLINE_MAX_CELLS];   /* per-cell inductance (controls loss) */
    int    n_cells;                /* active cells (4-32) */
    double L_base;                 /* base inductance */
    int    driven;                 /* 1 = source injected this tick, 0 = floating */
    double R;                      /* base loss per cell */
    double G;                      /* Lc-dependent loss per cell */
} TLine;

/* Initialize a transmission line with n_cells, base impedance z0 */
void tline_init(TLine *tl, int n_cells, double z0);

/* Inject a value at cell 0 (source end) — replaces, not additive */
void tline_inject(TLine *tl, double val);

/* Read the value at cell n-1 (destination end) */
double tline_read(const TLine *tl);

/* One step: shift register with per-cell attenuation + smoothing */
void tline_step(TLine *tl);

/* Apply node impedance bump at boundary cells */
void tline_set_impedance(TLine *tl, double z_src, double z_dst);

/* Effective "weight" — product of per-cell attenuation.
 * Returns uint8_t so old code reading e->weight stays compatible. */
uint8_t tline_weight(const TLine *tl);

/* Strengthen: decrease Lc by rate (less loss, stronger coupling) */
void tline_strengthen(TLine *tl, double rate);

/* Weaken: increase Lc by rate (more loss, weaker coupling) */
void tline_weaken(TLine *tl, double rate);

/* Initialize TLine from an old-format weight value (v12 backward compat).
 * Tunes R,G so tline_weight() returns approximately the given weight. */
void tline_init_from_weight(TLine *tl, uint8_t weight);

#endif /* TLINE_H */
