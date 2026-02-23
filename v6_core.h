/*
 * v6_core.h — Portable XYZT v6 Co-Presence Engine
 *
 * Zero dependencies beyond <stdint.h>.
 * No malloc. No stdio. No string.h. No platform specifics.
 *
 * Include on any target: Pico, Pi, Arduino, FPGA, x86, GPU.
 * The engine does ONE thing: route distinctions, record co-presence.
 * The observer creates ALL meaning.
 *
 * Isaac Oravec & Claude — February 2026
 */

#ifndef V6_CORE_H
#define V6_CORE_H

#include <stdint.h>

/* ══ Configuration ═══════════════════════════════════════════ */

#ifndef V6_MAX_POS
#define V6_MAX_POS 64
#endif

#define V6_BIT(n) (1ULL << (n))

/* ══ Internal helpers (zero stdlib) ════════════════════════ */

static inline void v6__zero(void *p, unsigned n) {
    uint8_t *b = (uint8_t *)p;
    for (unsigned i = 0; i < n; i++) b[i] = 0;
}

static inline void v6__copy(void *d, const void *s, unsigned n) {
    const uint8_t *sb = (const uint8_t *)s;
    uint8_t *db = (uint8_t *)d;
    for (unsigned i = 0; i < n; i++) db[i] = sb[i];
}

/* ══ Portable bit intrinsics ════════════════════════════════ */

static inline int v6_ctz64(uint64_t x) {
    if (x == 0) return 64;
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(x);
#elif defined(_MSC_VER)
    unsigned long idx;
    if (_BitScanForward64(&idx, x)) return (int)idx;
    return 64;
#else
    int n = 0;
    if (!(x & 0xFFFFFFFFULL)) { n += 32; x >>= 32; }
    if (!(x & 0xFFFF))        { n += 16; x >>= 16; }
    if (!(x & 0xFF))          { n += 8;  x >>= 8;  }
    if (!(x & 0xF))           { n += 4;  x >>= 4;  }
    if (!(x & 0x3))           { n += 2;  x >>= 2;  }
    if (!(x & 0x1))           { n += 1; }
    return n;
#endif
}

static inline int v6_popcount64(uint64_t x) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(x);
#elif defined(_MSC_VER)
    return (int)__popcnt64(x);
#else
    x -= (x >> 1) & 0x5555555555555555ULL;
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return (int)((x * 0x0101010101010101ULL) >> 56);
#endif
}

/* ═══════════════════════════════════════════════════════════════
 * THE ENGINE — zero arithmetic, pure co-presence
 *
 * Positions hold a distinction (marked=1) or don't (marked=0).
 * Topology (reads[]) says who observes whom.
 * tick() creates co-presence: the SET of marked sources.
 * That's it. No counting. No summing. No operations.
 * ═══════════════════════════════════════════════════════════════ */

typedef struct {
    uint8_t  marked[V6_MAX_POS];       /* distinction present: 0 or 1       */
    uint64_t reads[V6_MAX_POS];        /* topology: bitmask of source pos   */
    uint64_t co_present[V6_MAX_POS];   /* result: bitmask of marked sources */
    uint8_t  active[V6_MAX_POS];       /* is this position wired?           */
    int      n_pos;
} V6Grid;

static inline void v6_init(V6Grid *g) {
    v6__zero(g, sizeof(*g));
}

static inline void v6_mark(V6Grid *g, int pos, int val) {
    if (pos < 0 || pos >= V6_MAX_POS) return;
    g->marked[pos] = val ? 1 : 0;
    if (pos >= g->n_pos) g->n_pos = pos + 1;
}

static inline void v6_wire(V6Grid *g, int pos, uint64_t rd) {
    if (pos < 0 || pos >= V6_MAX_POS) return;
    g->reads[pos] = rd;
    g->active[pos] = 1;
    if (pos >= g->n_pos) g->n_pos = pos + 1;
}

static inline void v6_unwire(V6Grid *g, int pos) {
    if (pos < 0 || pos >= V6_MAX_POS) return;
    g->reads[pos] = 0;
    g->active[pos] = 0;
}

/*
 * THE tick — zero arithmetic, only co-presence.
 * For each wired position: look at sources.
 * Record WHICH sources are marked, as a set.
 */
static inline void v6_tick(V6Grid *g) {
    uint8_t snap[V6_MAX_POS];
    v6__copy(snap, g->marked, V6_MAX_POS);

    for (int p = 0; p < g->n_pos; p++) {
        if (!g->active[p]) continue;
        uint64_t present = 0;
        uint64_t bits = g->reads[p];
        while (bits) {
            int b = v6_ctz64(bits);
            if (snap[b]) present |= V6_BIT(b);
            bits &= bits - 1;
        }
        g->co_present[p] = present;
    }
}

/* ═══════════════════════════════════════════════════════════════
 * OBSERVERS — all meaning lives here
 *
 * The engine presents co-presence.
 * The observer decides what it means.
 * Even counting is observation, not engine.
 * ═══════════════════════════════════════════════════════════════ */

static inline int v6_obs_any(uint64_t cp)              { return cp != 0; }
static inline int v6_obs_none(uint64_t cp)             { return cp == 0; }
static inline int v6_obs_count(uint64_t cp)            { return v6_popcount64(cp); }
static inline int v6_obs_parity(uint64_t cp)           { return v6_popcount64(cp) & 1; }
static inline int v6_obs_all(uint64_t cp, uint64_t rd) { return (cp & rd) == rd; }
static inline int v6_obs_ge(uint64_t cp, int n)        { return v6_popcount64(cp) >= n; }
static inline int v6_obs_exactly(uint64_t cp, int n)   { return v6_popcount64(cp) == n; }

/* Observer type selector (stored as 2-bit word in grid) */
#define V6_OBS_ANY     0
#define V6_OBS_ALL     1
#define V6_OBS_PARITY  2
#define V6_OBS_COUNT   3

static inline int v6_observe(uint64_t cp, uint64_t rd, int obs_type) {
    switch (obs_type) {
        case V6_OBS_ANY:    return v6_obs_any(cp);
        case V6_OBS_ALL:    return v6_obs_all(cp, rd);
        case V6_OBS_PARITY: return v6_obs_parity(cp);
        case V6_OBS_COUNT:  return v6_obs_count(cp);
        default:            return v6_obs_any(cp);
    }
}

/* ═══════════════════════════════════════════════════════════════
 * SELF-IDENTIFYING BIT WORDS (OS Layer)
 *
 * A word's marked positions ARE its identity.
 * Same pattern serves as data, program, and wiring.
 * No encoding. No convention. The marks ARE the meaning.
 * ═══════════════════════════════════════════════════════════════ */

/* Read which positions in [base, base+width) are marked → bitmask */
static inline uint64_t v6_word_read(V6Grid *g, int base, int width) {
    uint64_t word = 0;
    for (int i = 0; i < width && (base + i) < V6_MAX_POS; i++) {
        if (g->marked[base + i]) word |= V6_BIT(i);
    }
    return word;
}

/* Write marks into [base, base+width) from bitmask */
static inline void v6_word_write(V6Grid *g, int base, int width, uint64_t word) {
    for (int i = 0; i < width && (base + i) < V6_MAX_POS; i++) {
        v6_mark(g, base + i, (word >> i) & 1);
    }
}

/* Convert a program word to wiring mask for data at data_base.
 * The program's marked bits say which data positions to read. */
static inline uint64_t v6_word_as_wiring(V6Grid *g, int prog_base, int prog_width,
                                          int data_base) {
    uint64_t prog = v6_word_read(g, prog_base, prog_width);
    uint64_t rd = 0;
    for (int i = 0; i < prog_width; i++) {
        if (prog & V6_BIT(i)) rd |= V6_BIT(data_base + i);
    }
    return rd;
}

/* Execute one OS instruction from grid-stored program and observer.
 * 1. Read program word → wiring mask
 * 2. Wire output position
 * 3. Tick
 * 4. Read observer type from grid
 * 5. Observe co-presence at output
 * The grid programs itself. */
static inline int v6_os_exec(V6Grid *g, int prog_base, int prog_width,
                              int data_base, int out_pos, int obs_base) {
    uint64_t wiring = v6_word_as_wiring(g, prog_base, prog_width, data_base);
    v6_wire(g, out_pos, wiring);
    v6_tick(g);
    uint64_t obs_word = v6_word_read(g, obs_base, 2);
    return v6_observe(g->co_present[out_pos], wiring, (int)obs_word);
}

/* ═══════════════════════════════════════════════════════════════
 * BOOLEAN BUILDING BLOCKS — observers composed through topology
 *
 * Each creates a temporary grid, wires, ticks, observes.
 * Every operation goes through the engine. Nothing computed outside.
 * ═══════════════════════════════════════════════════════════════ */

static inline int v6_and(int a, int b) {
    V6Grid g; v6_init(&g);
    v6_mark(&g, 0, a); v6_mark(&g, 1, b);
    v6_wire(&g, 2, V6_BIT(0)|V6_BIT(1));
    v6_tick(&g);
    return v6_obs_all(g.co_present[2], V6_BIT(0)|V6_BIT(1));
}

static inline int v6_or(int a, int b) {
    V6Grid g; v6_init(&g);
    v6_mark(&g, 0, a); v6_mark(&g, 1, b);
    v6_wire(&g, 2, V6_BIT(0)|V6_BIT(1));
    v6_tick(&g);
    return v6_obs_any(g.co_present[2]);
}

static inline int v6_not(int a) {
    V6Grid g; v6_init(&g);
    v6_mark(&g, 0, a); v6_mark(&g, 1, 1); /* bias */
    v6_wire(&g, 2, V6_BIT(0)|V6_BIT(1));
    v6_tick(&g);
    return v6_obs_exactly(g.co_present[2], 1);
}

static inline int v6_xor(int a, int b) {
    V6Grid g; v6_init(&g);
    v6_mark(&g, 0, a); v6_mark(&g, 1, b);
    v6_wire(&g, 2, V6_BIT(0)|V6_BIT(1));
    v6_tick(&g);
    return v6_obs_parity(g.co_present[2]);
}

/* ═══════════════════════════════════════════════════════════════
 * HAL INTERFACE — implement these per board
 *
 * Each board provides a thin implementation (~20-30 lines).
 * The v6 engine above is identical everywhere.
 *
 * To port to a new board:
 *   1. Include this header
 *   2. Implement the hal_ functions below
 *   3. Call v6_init(), bridge, tick, observe
 *   4. That's it. Same engine. Different hardware.
 * ═══════════════════════════════════════════════════════════════ */

/* These are declared but not defined here.
 * Each board's main.c defines them.
 *
 * uint32_t hal_read_pins(void);
 * void     hal_write_pins(uint32_t mask, uint32_t value);
 * void     hal_init_pins(void);
 * int      hal_pin_count(void);
 * const char* hal_board_id(void);
 */

#endif /* V6_CORE_H */
