/*
 * cortex.h — Thin controller over engine + Yee substrate
 *
 * Clean ingest→inject→tick→read loop. The DISH_SPEC's cortex.cu
 * without gutting engine.c. Parallel runtime path.
 *
 * Isaac Oravec & Claude, March 2026
 */

#ifndef CORTEX_H
#define CORTEX_H

#include "engine.h"
#include "infer.h"

typedef struct {
    Engine  eng;
    uint8_t *yee_sub;    /* accumulator readback buffer */
    int     gpu_ok;      /* yee initialized */
    uint64_t n_ingested; /* total observations */
} Cortex;

/* Initialize engine + Yee substrate */
int  cortex_init(Cortex *c);

/* Ingest raw bytes as a new observation */
int  cortex_ingest(Cortex *c, const char *name, const uint8_t *data, int len);

/* Tick N cycles (each = SUBSTRATE_INT ticks with full Yee loop) */
void cortex_tick(Cortex *c, int n_cycles);

/* Query via wave inference — returns top-K resonating nodes */
int  cortex_query(Cortex *c, const char *text, InferResult *out, int max);

/* Save/load full state (engine + Yee L-field) */
int  cortex_save(Cortex *c, const char *path);
int  cortex_load(Cortex *c, const char *path);

/* Prediction loop: graph proposes, wave verifies, Hebbian learns.
 * Finds what the engine is focused on, predicts connected nodes,
 * injects predictions at 0.3x amplitude, reads which resonated.
 * Returns number of verified predictions. */
int cortex_predict(Cortex *c);

/* Self-observation: engine reads its own coherence field, encodes what
 * it sees, and ingests the observation as a new node. The engine watches
 * itself think. Returns the node ID of the self-observation, or -1. */
int cortex_self_observe(Cortex *c);

/* Autonomous heartbeat: the full loop running continuously.
 * Perceive → predict → verify → self-observe → voice → feedback.
 * No human command needed. The engine thinks on its own.
 * Returns total cycles completed. */
int cortex_heartbeat(Cortex *c, int n_cycles);

/* Cleanup */
void cortex_destroy(Cortex *c);

#endif /* CORTEX_H */
