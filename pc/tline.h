/*
 * tline.h — Transmission line edge library
 *
 * Per-cell FDTD with R,G loss terms (telegrapher's equations).
 * Extracted from xyzt_unified.c + universe_tline_v2.c.
 *
 * Without loss (R=0, G=0), propagation is just time delay — Z collapses
 * to T. With loss, high frequencies attenuate faster than low frequencies.
 * Cell 0 sees the full signal. Cell N sees the marble.
 * That gradient IS Z.
 *
 * Isaac Oravec & Claude, March 2026
 */
#ifndef TLINE_H
#define TLINE_H

#include <stdint.h>

#define TLINE_MAX_CELLS 32

typedef struct {
    double V[TLINE_MAX_CELLS];
    double I[TLINE_MAX_CELLS];
    double Lc[TLINE_MAX_CELLS];   /* per-cell inductance */
    int    n_cells;                /* active cells (4-32) */
    double L_base;                 /* base inductance */
    double C0;                     /* capacitance per cell */
    double R;                      /* series resistance per cell (current loss) */
    double G;                      /* shunt conductance per cell (voltage drain) */
    double mur[4];                 /* Mur ABC state: [V0_old, V1_old, Vn_old, Vn1_old] */
} TLine;

/* Initialize a transmission line with n_cells, base impedance z0 */
void tline_init(TLine *tl, int n_cells, double z0);

/* Inject a value at cell 0 (source end) */
void tline_inject(TLine *tl, double val);

/* Read the value at cell n-1 (destination end) */
double tline_read(const TLine *tl);

/* Read the value at any cell (for Z-depth observation) */
double tline_read_at(const TLine *tl, int cell);

/* One FDTD step — propagate signals through all cells */
void tline_step(TLine *tl);

/* Apply node impedance bump at boundary cells */
void tline_set_impedance(TLine *tl, double z_src, double z_dst);

/* Effective "weight" — what the old engine would see.
 * Ratio of output amplitude to input amplitude at steady state.
 * Returns uint8_t so old code reading e->weight stays compatible. */
uint8_t tline_weight(const TLine *tl);

/* Back-reaction: collision energy grows Lc at boundary cells.
 * Call when 2+ edges deliver energy to same node simultaneously. */
void tline_backreaction(TLine *tl, double collision_energy);

/* Strengthen: decrease Lc by rate (faster propagation, stronger coupling) */
void tline_strengthen(TLine *tl, double rate);

/* Weaken: increase Lc by rate (slower propagation, weaker coupling) */
void tline_weaken(TLine *tl, double rate);

/* Initialize TLine from an old-format weight value (v12 backward compat).
 * Tunes Lc so tline_weight() returns approximately the given weight. */
void tline_init_from_weight(TLine *tl, uint8_t weight);

#endif /* TLINE_H */
