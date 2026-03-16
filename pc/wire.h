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
#include "substrate.cuh"

/* Wire a voxel to its spatial neighbors within the volume.
 * 6-connected (face neighbors) for local wiring.
 * Used by old CA regression tests (run_gpu_tests). */
void wire_local_3d(CubeState *cubes, int n_cubes);

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

#endif /* WIRE_H */
