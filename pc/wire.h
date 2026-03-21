/*
 * wire.h — Auto-wiring layer
 *
 * Yee substrate wiring (wave physics).
 * Old CA bridge functions removed in v0.14-yee-persist.
 *
 * Isaac Oravec & Claude, February 2026
 */

#ifndef WIRE_H
#define WIRE_H

#include "engine.h"

/* ── Retina: holographic injection, no hash ── */

/* Inject data as 2D Fourier pattern on x=0 face.
 * The wave propagates through L-field. Position = where energy peaks. */
int wire_retina_inject(const uint8_t *data, int len, float amplitude);

/* Find energy peak after retina injection + propagation.
 * Returns coord_pack(peak_x, peak_y, peak_z). */
uint32_t wire_retina_find_peak(void);

/* ── Yee substrate wiring (wave physics) ── */

/* Inject engine nodes into Yee grid as voltage sources.
 * amplitude = node val/VAL_CEILING, strength = valence/255.
 * Crystallized nodes drive hard, plastic nodes barely perturb. */
int wire_engine_to_yee(const Engine *eng);

/* Read Yee accumulator back into engine.
 * Downloads acc as uint8_t substrate, reads at each node's voxel position. */
int wire_yee_to_engine(Engine *eng);

/* Point child retinas at Yee accumulator data.
 * Each child reads 64 bytes from its parent's cube. */
int wire_yee_retinas(Engine *eng, uint8_t *yee_substrate);

/* ── Output path: the inverse retina ── */

/* Download raw sponge absorption accumulator.
 * The pattern of absorbed energy at boundaries = engine's voice. */
int wire_output_read(float *output, int n);

/* Inverse retina: decode boundary absorption pattern back to bytes.
 * Correlates output face against retina basis functions.
 * Returns number of bytes decoded, or -1 on error. */
int wire_output_decode(uint8_t *decoded, int max_bytes);

#endif /* WIRE_H */
