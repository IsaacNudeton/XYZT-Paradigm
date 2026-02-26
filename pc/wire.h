/*
 * wire.h — Auto-wiring layer
 *
 * v3 proximity wiring (spatial coords -> initial edges)
 * v6 tile gateway management
 * Hebbian on CPU (correlate GPU expression results)
 *
 * Isaac Oravec & Claude, February 2026
 */

#ifndef WIRE_H
#define WIRE_H

#include "engine.h"
#include "substrate.cuh"

/* Wire a voxel to its spatial neighbors within the volume.
 * 6-connected (face neighbors) for local wiring. */
void wire_local_3d(CubeState *cubes, int n_cubes);

/* Wire gateway positions between adjacent cubes.
 * Face positions at cube boundaries connect to neighbor cube face positions. */
void wire_gateways(CubeState *cubes, int n_cubes);

/* CPU-side Hebbian learning: correlate GPU results with engine graph.
 * Maps GPU co-presence patterns back to engine edge weights. */
void wire_hebbian_from_gpu(Engine *eng, const CubeState *cubes, int n_cubes);

/* Inject engine node values into GPU substrate.
 * Maps engine nodes to voxel positions by spatial coordinate. */
void wire_engine_to_gpu(const Engine *eng, CubeState *cubes, int n_cubes);

/* Read GPU expression results back into engine.
 * Maps voxel co-presence back to engine node accumulation. */
void wire_gpu_to_engine(Engine *eng, const CubeState *cubes, int n_cubes);

#endif /* WIRE_H */
