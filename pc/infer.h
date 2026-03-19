/*
 * infer.h — XYZT Inference Layer
 *
 * Forward pass through the wave substrate:
 * 1. Load L-field (the "weights")
 * 2. Inject query as voltage spike (the "embedding")
 * 3. Ring the grid for N heartbeats (the "compute")
 * 4. Read observers at resonating nodes (the "output")
 *
 * No matrix multiplication. No layers. No backprop.
 * Inject a wave, let physics compute, read what resonates.
 *
 * Isaac Oravec & Claude, March 2026
 */

#ifndef INFER_H
#define INFER_H

#include "engine.h"

#define INFER_MAX_RESULTS 16
#define INFER_RING_TICKS  155   /* one SUBSTRATE_INT cycle */

typedef struct {
    int    node_id;        /* which node resonated */
    char   name[NAME_LEN]; /* node name for lookup */
    float  energy;         /* accumulated |V| at this position */
    float  val;            /* net voltage (signed — constructive vs destructive) */
    int    z3_freq;        /* Z3 observer: zero-crossing count (resonance) */
    int    z4_corr;        /* Z4 observer: correlated trend (1=yes, 0=no) */
    int    crystal;        /* crystal strength of this node */
} InferResult;

/* Run a forward pass: inject query, ring, read resonance.
 * eng must have loaded state. Yee must be initialized.
 * Returns number of results (top-K resonating nodes). */
int infer_query(Engine *eng, const char *query_text,
                InferResult *results, int max_results);

/* Run a forward pass with raw bytes (for binary/sensor data). */
int infer_query_raw(Engine *eng, const uint8_t *data, int len,
                    InferResult *results, int max_results);

#endif /* INFER_H */
