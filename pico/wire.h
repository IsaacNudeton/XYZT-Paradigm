/*
 * wire.h — Hebbian Wire Graph
 *
 * Two features that co-occur get connected.
 * Connections that fire together strengthen.
 * Connections that don't, decay.
 * The resulting topology IS understanding.
 * Not a model OF understanding. IS understanding.
 *
 * On a PCB: traces that carry correlated signals
 * are functionally connected. Same principle.
 * We just make the traces rewritable.
 */

#ifndef WIRE_H
#define WIRE_H

#include <stdint.h>
#include <string.h>

/* Max features the graph tracks.
 * On Pico with 264KB SRAM, this is the budget.
 * 128 features × 128 = 16384 edges × 2 bytes = 32KB.
 * Leaves plenty for everything else. */
#define WIRE_MAX_FEATURES 128
#define WIRE_STRENGTHEN   8    /* co-occurrence bump */
#define WIRE_DECAY        1    /* per-tick decay */
#define WIRE_SATURATE     255  /* max edge weight (uint8_t) */

typedef struct {
    uint8_t  edge[WIRE_MAX_FEATURES][WIRE_MAX_FEATURES]; /* adjacency weights */
    uint32_t fire_count[WIRE_MAX_FEATURES];               /* how often each feature fires */
    uint16_t n_features;                                   /* how many features exist */
} wire_graph_t;

static inline void wire_init(wire_graph_t *w) {
    memset(w, 0, sizeof(*w));
}

/* Hebbian bind: two features co-occurred. Strengthen edge. */
static inline void wire_bind(wire_graph_t *w, uint8_t a, uint8_t b) {
    if (a >= WIRE_MAX_FEATURES || b >= WIRE_MAX_FEATURES || a == b) return;
    uint16_t va = (uint16_t)w->edge[a][b] + WIRE_STRENGTHEN;
    if (va > WIRE_SATURATE) va = WIRE_SATURATE;
    w->edge[a][b] = (uint8_t)va;
    w->edge[b][a] = (uint8_t)va;  /* symmetric */
}

/* Decay all edges. What doesn't fire together, unwires. */
static inline void wire_decay_all(wire_graph_t *w) {
    for (int i = 0; i < WIRE_MAX_FEATURES; i++) {
        for (int j = i + 1; j < WIRE_MAX_FEATURES; j++) {
            if (w->edge[i][j] > WIRE_DECAY) {
                w->edge[i][j] -= WIRE_DECAY;
                w->edge[j][i] -= WIRE_DECAY;
            } else {
                w->edge[i][j] = 0;
                w->edge[j][i] = 0;
            }
        }
    }
}

/* Query: strongest neighbor of feature f */
static inline uint8_t wire_strongest(const wire_graph_t *w, uint8_t f) {
    uint8_t best = 0, best_w = 0;
    for (int i = 0; i < WIRE_MAX_FEATURES; i++) {
        if (i != f && w->edge[f][i] > best_w) {
            best_w = w->edge[f][i];
            best = (uint8_t)i;
        }
    }
    return best;
}

/* Query: total weight from feature f (how connected is it?) */
static inline uint32_t wire_total_weight(const wire_graph_t *w, uint8_t f) {
    uint32_t sum = 0;
    for (int i = 0; i < WIRE_MAX_FEATURES; i++)
        sum += w->edge[f][i];
    return sum;
}

/* Register a new feature, returns its index */
static inline uint8_t wire_add_feature(wire_graph_t *w) {
    if (w->n_features >= WIRE_MAX_FEATURES) return WIRE_MAX_FEATURES - 1;
    return (uint8_t)(w->n_features++);
}

#endif /* WIRE_H */
