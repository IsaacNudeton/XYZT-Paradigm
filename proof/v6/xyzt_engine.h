/*
 * xyzt_engine.h — Single source of truth
 *
 * The co-presence engine. Zero arithmetic inside.
 * tick() routes distinctions through topology.
 * Observers create ALL meaning from co-presence.
 *
 * Every test file includes this. One definition. One truth.
 *
 * Isaac Oravec & Claude, February 2026
 */

#ifndef XYZT_ENGINE_H
#define XYZT_ENGINE_H

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ══════════════════════════════════════════════════════════════
 * 64-POSITION GRID ENGINE
 * ══════════════════════════════════════════════════════════════ */

#define XYZT_MAX_POS 64
#define XYZT_BIT(n) (1ULL << (n))

typedef struct {
    uint8_t  marked[XYZT_MAX_POS];
    uint64_t reads[XYZT_MAX_POS];
    uint64_t co_present[XYZT_MAX_POS];
    uint8_t  active[XYZT_MAX_POS];
    int      n_pos;
} XyztGrid;

static inline void xyzt_grid_init(XyztGrid *g) {
    memset(g, 0, sizeof(*g));
}

static inline void xyzt_mark(XyztGrid *g, int pos, int val) {
    g->marked[pos] = val ? 1 : 0;
    if (pos >= g->n_pos) g->n_pos = pos + 1;
}

static inline void xyzt_wire(XyztGrid *g, int pos, uint64_t rd) {
    g->reads[pos]  = rd;
    g->active[pos] = 1;
    if (pos >= g->n_pos) g->n_pos = pos + 1;
}

static inline void xyzt_tick(XyztGrid *g) {
    uint8_t snap[XYZT_MAX_POS];
    memcpy(snap, g->marked, XYZT_MAX_POS);
    for (int p = 0; p < g->n_pos; p++) {
        if (!g->active[p]) continue;
        uint64_t present = 0;
        uint64_t bits = g->reads[p];
        while (bits) {
            int b = __builtin_ctzll(bits);
            if (snap[b]) present |= (1ULL << b);
            bits &= bits - 1;
        }
        g->co_present[p] = present;
    }
}

/* ══════════════════════════════════════════════════════════════
 * 64-BIT OBSERVERS — all meaning lives here
 * ══════════════════════════════════════════════════════════════ */

static inline int xyzt_obs_any(uint64_t cp)                { return cp != 0 ? 1 : 0; }
static inline int xyzt_obs_all(uint64_t cp, uint64_t rd)   { return (cp & rd) == rd ? 1 : 0; }
static inline int xyzt_obs_none(uint64_t cp)               { return cp == 0 ? 1 : 0; }
static inline int xyzt_obs_parity(uint64_t cp)             { return __builtin_popcountll(cp) & 1; }
static inline int xyzt_obs_count(uint64_t cp)              { return __builtin_popcountll(cp); }
static inline int xyzt_obs_ge(uint64_t cp, int n)          { return __builtin_popcountll(cp) >= n ? 1 : 0; }
static inline int xyzt_obs_exactly(uint64_t cp, int n)     { return __builtin_popcountll(cp) == n ? 1 : 0; }

/* ══════════════════════════════════════════════════════════════
 * 16-POSITION TILE ENGINE (for tiled/mesh architectures)
 * ══════════════════════════════════════════════════════════════ */

#define XYZT_TILE_SIZE 16

typedef struct {
    uint8_t  marked[XYZT_TILE_SIZE];
    uint16_t reads[XYZT_TILE_SIZE];
    uint16_t co_present[XYZT_TILE_SIZE];
    uint8_t  active[XYZT_TILE_SIZE];
    int      tile_id;
} XyztTile;

static inline void xyzt_tile_init(XyztTile *t, int id) {
    memset(t, 0, sizeof(*t));
    t->tile_id = id;
}

static inline void xyzt_tile_mark(XyztTile *t, int pos, int val) {
    t->marked[pos] = val ? 1 : 0;
}

static inline void xyzt_tile_wire(XyztTile *t, int pos, uint16_t rd) {
    t->reads[pos] = rd;
    t->active[pos] = 1;
}

static inline void xyzt_tile_tick(XyztTile *t) {
    uint8_t snap[XYZT_TILE_SIZE];
    memcpy(snap, t->marked, XYZT_TILE_SIZE);
    for (int p = 0; p < XYZT_TILE_SIZE; p++) {
        if (!t->active[p]) continue;
        uint16_t present = 0;
        uint16_t bits = t->reads[p];
        while (bits) {
            int b = __builtin_ctz(bits);
            if (snap[b]) present |= (1 << b);
            bits &= bits - 1;
        }
        t->co_present[p] = present;
    }
}

/* ══════════════════════════════════════════════════════════════
 * 16-BIT OBSERVERS (tile-scoped)
 * ══════════════════════════════════════════════════════════════ */

static inline int xyzt_obs16_any(uint16_t cp)              { return cp != 0 ? 1 : 0; }
static inline int xyzt_obs16_all(uint16_t cp, uint16_t rd) { return (cp & rd) == rd ? 1 : 0; }
static inline int xyzt_obs16_none(uint16_t cp)             { return cp == 0 ? 1 : 0; }
static inline int xyzt_obs16_parity(uint16_t cp)           { return __builtin_popcount(cp) & 1; }
static inline int xyzt_obs16_count(uint16_t cp)            { return __builtin_popcount(cp); }
static inline int xyzt_obs16_ge(uint16_t cp, int n)        { return __builtin_popcount(cp) >= n ? 1 : 0; }
static inline int xyzt_obs16_exactly(uint16_t cp, int n)   { return __builtin_popcount(cp) == n ? 1 : 0; }

/* ══════════════════════════════════════════════════════════════
 * SELF-IDENTIFYING BIT WORDS (OS layer)
 * ══════════════════════════════════════════════════════════════ */

static inline uint64_t xyzt_word_read(XyztGrid *g, int base, int width) {
    uint64_t word = 0;
    for (int i = 0; i < width && (base + i) < XYZT_MAX_POS; i++)
        if (g->marked[base + i]) word |= XYZT_BIT(i);
    return word;
}

static inline void xyzt_word_write(XyztGrid *g, int base, int width, uint64_t word) {
    for (int i = 0; i < width && (base + i) < XYZT_MAX_POS; i++)
        xyzt_mark(g, base + i, (word >> i) & 1);
}

static inline uint64_t xyzt_word_as_wiring(XyztGrid *g, int prog_base, int prog_width, int data_base) {
    uint64_t prog = xyzt_word_read(g, prog_base, prog_width);
    uint64_t rd = 0;
    for (int i = 0; i < prog_width; i++)
        if (prog & XYZT_BIT(i)) rd |= XYZT_BIT(data_base + i);
    return rd;
}

/* ══════════════════════════════════════════════════════════════
 * TEST FRAMEWORK — shared across all test files
 * ══════════════════════════════════════════════════════════════ */

#ifdef XYZT_TEST_MAIN

static int xyzt_g_pass = 0, xyzt_g_fail = 0;

static void xyzt_check(const char *name, int exp, int got) {
    if (exp == got) xyzt_g_pass++;
    else {
        xyzt_g_fail++;
        printf("  FAIL %s: expected %d, got %d\n", name, exp, got);
    }
}

static void xyzt_results(void) {
    printf("RESULTS: %d passed, %d failed, %d total\n",
           xyzt_g_pass, xyzt_g_fail, xyzt_g_pass + xyzt_g_fail);
}

#endif /* XYZT_TEST_MAIN */

#endif /* XYZT_ENGINE_H */
