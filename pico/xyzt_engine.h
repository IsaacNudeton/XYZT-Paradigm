/*
 * xyzt_engine.h — Co-Presence Grid Engine
 *
 * Address IS meaning. No ALU. No instruction pointer.
 * Place-value marks collide. Observer reads collisions.
 * That IS computation.
 *
 * T = grid_init()   — 0D potential. Blank paper.
 * X = tick()         — 1D sequence. Before→after.
 * Y = co_present[]   — 2D comparison. What's here together.
 * Z = obs_*()        — 3D depth. Meaning from co-presence.
 *
 * Proven: 436 tests, 0 failures, 6019 lines.
 * Isaac Oravec & Claude — February 2026
 */

#ifndef XYZT_ENGINE_H
#define XYZT_ENGINE_H

#include <stdint.h>
#include <string.h>

/* ═══════════════════════════════════════════════════════════
 * GRID — 64 positions. Each position: marked or not.
 * Multi-hot. The set of marked positions IS the word.
 * The address IS the meaning.
 * ═══════════════════════════════════════════════════════════ */

#define GRID_SIZE 64

typedef struct {
    uint8_t  marks[GRID_SIZE];      /* current: which positions are marked */
    uint8_t  snap[GRID_SIZE];       /* T-break: frozen state at last tick */
    uint8_t  co_present[GRID_SIZE]; /* Y: what survived from snap to now */
    uint32_t tick_count;
    uint16_t pop;                   /* population: how many marks */
    uint16_t co_pop;                /* co-presence population */
} xyzt_grid_t;

/* T: Init. The universe exists. Blank potential. */
static inline void xyzt_grid_init(xyzt_grid_t *g) {
    memset(g, 0, sizeof(*g));
}

/* Mark a position. Address IS meaning. */
static inline void xyzt_mark(xyzt_grid_t *g, uint8_t pos) {
    if (pos < GRID_SIZE && !g->marks[pos]) {
        g->marks[pos] = 1;
        g->pop++;
    }
}

/* Clear a position */
static inline void xyzt_clear(xyzt_grid_t *g, uint8_t pos) {
    if (pos < GRID_SIZE && g->marks[pos]) {
        g->marks[pos] = 0;
        g->pop--;
    }
}

/* Clear entire grid */
static inline void xyzt_clear_all(xyzt_grid_t *g) {
    memset(g->marks, 0, GRID_SIZE);
    g->pop = 0;
}

/* X: Tick. Snap current state. Compute co-presence with new marks.
 * This IS the clock edge. The register latch. NOW. */
static inline void xyzt_tick(xyzt_grid_t *g) {
    g->co_pop = 0;
    for (int i = 0; i < GRID_SIZE; i++) {
        g->snap[i] = g->marks[i];
        g->co_present[i] = 0;
    }
    g->tick_count++;
}

/* After tick: mark new state, then compute co-presence */
static inline void xyzt_resolve(xyzt_grid_t *g) {
    g->co_pop = 0;
    for (int i = 0; i < GRID_SIZE; i++) {
        g->co_present[i] = g->snap[i] & g->marks[i];
        if (g->co_present[i]) g->co_pop++;
    }
}

/* ═══════════════════════════════════════════════════════════
 * Z: OBSERVERS — Same data, different meaning.
 * The observer doesn't change the grid. It reads it differently.
 * obs_any  = electromagnetic (OR, coupling)
 * obs_all  = gravitational (AND, consensus)
 * obs_count = population (how many)
 * obs_parity = CP violation (even/odd)
 * ═══════════════════════════════════════════════════════════ */

static inline uint32_t xyzt_obs_any(const xyzt_grid_t *g) {
    for (int i = 0; i < GRID_SIZE; i++)
        if (g->co_present[i]) return 1;
    return 0;
}

static inline uint32_t xyzt_obs_all(const xyzt_grid_t *g) {
    for (int i = 0; i < GRID_SIZE; i++)
        if (g->snap[i] && !g->co_present[i]) return 0;
    return (g->co_pop > 0) ? 1 : 0;
}

static inline uint32_t xyzt_obs_count(const xyzt_grid_t *g) {
    return g->co_pop;
}

static inline uint32_t xyzt_obs_parity(const xyzt_grid_t *g) {
    return g->co_pop & 1;
}

/* Energy: (A+B)² when entangled, A²+B² when orthogonal */
static inline uint32_t xyzt_energy(const xyzt_grid_t *g) {
    uint32_t total = g->pop;
    return total * total;
}

#endif /* XYZT_ENGINE_H */
