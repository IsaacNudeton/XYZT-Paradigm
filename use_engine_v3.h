/*
 * use.c — Universal Structure Engine
 *
 * The bedrock extracted from ~25,000 lines of XYZT.
 * Five operations. One loop. Finds structure in anything.
 *
 * observe → find runs, deltas, period, symmetry
 * predict → constant / linear / geometric / acceleration
 * residue → actual - predicted = surprise
 * accumulate → surprise flows to positions, positions count
 * wire → edges that reduce surprise survive, others die
 *
 * Feed it anything. It finds what repeats, predicts what comes next,
 * and only moves what it doesn't yet understand. Silence = understood.
 *
 * Isaac Oravec & Claude, February 2026
 *
 * Compile: gcc -O2 -o use use.c -lm
 * Run: ./use
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/* ══════════════════════════════════════════════════════════════
 * LAYER 0: BITSTREAM — distinction (the 2)
 * ══════════════════════════════════════════════════════════════ */
#define BS_WORDS 64
#define BS_MAXBITS (BS_WORDS * 64)

typedef struct { uint64_t w[BS_WORDS]; int len; } BitStream;

static void bs_init(BitStream *b) { memset(b, 0, sizeof(*b)); }

static void bs_push(BitStream *b, int bit) {
    if (b->len >= BS_MAXBITS) return;
    int idx = b->len / 64, off = b->len % 64;
    if (bit) b->w[idx] |= (1ULL << off);
    else b->w[idx] &= ~(1ULL << off);
    b->len++;
}

static int bs_get(const BitStream *b, int i) {
    if (i < 0 || i >= b->len) return 0;
    return (b->w[i / 64] >> (i % 64)) & 1;
}

static int bs_popcount(const BitStream *b) {
    int ones = 0, full = b->len / 64;
    for (int i = 0; i < full; i++) ones += __builtin_popcountll(b->w[i]);
    int rem = b->len % 64;
    if (rem) ones += __builtin_popcountll(b->w[full] & ((1ULL << rem) - 1));
    return ones;
}

static void bs_from_bytes(BitStream *b, const uint8_t *data, int nbytes) {
    bs_init(b);
    for (int i = 0; i < nbytes && b->len < BS_MAXBITS; i++)
        for (int bit = 0; bit < 8 && b->len < BS_MAXBITS; bit++)
            bs_push(b, (data[i] >> bit) & 1);
}

static void bs_from_string(BitStream *b, const char *s) {
    bs_from_bytes(b, (const uint8_t *)s, (int)strlen(s));
}

static void bs_from_int(BitStream *b, int32_t v) {
    bs_from_bytes(b, (const uint8_t *)&v, sizeof(v));
}

/* ══════════════════════════════════════════════════════════════
 * LAYER 1: OBSERVE
 * ══════════════════════════════════════════════════════════════ */
#define MAX_RUNS 256

typedef struct {
    int lengths[MAX_RUNS];
    int types[MAX_RUNS];
    int count;
} RunList;

typedef struct {
    int ones_runs[MAX_RUNS];
    int zeros_runs[MAX_RUNS];
    int n_ones, n_zeros;
    int ones_deltas[MAX_RUNS];
    int zeros_deltas[MAX_RUNS];
    int n_ones_d, n_zeros_d;
    int predicted_next_one;
    int predicted_next_zero;
    int ones_pattern;
    int zeros_pattern;
    int density;
    int symmetry;
    int period;
    int transition_rate;  /* bit flips per 100 bits — how choppy */
    int max_run;          /* longest consecutive same-bit stretch */
} Observation;

static void bs_runs(const BitStream *b, RunList *r) {
    r->count = 0;
    if (b->len == 0) return;
    int cur = bs_get(b, 0), run = 1;
    for (int i = 1; i < b->len; i++) {
        int bit = bs_get(b, i);
        if (bit == cur) { run++; }
        else {
            if (r->count < MAX_RUNS) {
                r->types[r->count] = cur;
                r->lengths[r->count] = run;
                r->count++;
            }
            cur = bit; run = 1;
        }
    }
    if (r->count < MAX_RUNS) {
        r->types[r->count] = cur;
        r->lengths[r->count] = run;
        r->count++;
    }
}

static int measure_symmetry(const BitStream *b) {
    if (b->len < 2) return 100;
    int half = b->len / 2, match = 0;
    for (int i = 0; i < half; i++)
        if (bs_get(b, i) == bs_get(b, half + i)) match++;
    return (match * 100) / half;
}

static int find_period(const BitStream *b) {
    if (b->len < 4) return 0;
    int best_period = 0, best_score = 0;
    int max_p = b->len / 2;
    if (max_p > 128) max_p = 128;
    for (int p = 1; p <= max_p; p++) {
        int match = 0, total = 0;
        for (int i = 0; i + p < b->len; i++) {
            if (bs_get(b, i) == bs_get(b, i + p)) match++;
            total++;
        }
        if (total > 0 && match * 100 / total > 90 && match > best_score) {
            best_score = match;
            best_period = p;
        }
    }
    return best_period;
}

/* ══════════════════════════════════════════════════════════════
 * LAYER 2: PREDICT
 * ══════════════════════════════════════════════════════════════ */
static int predict_sequence(const int *vals, int n, int *pattern_type) {
    *pattern_type = 0;
    if (n < 2) return n > 0 ? vals[0] : 0;

    int constant = 1;
    for (int i = 1; i < n; i++)
        if (vals[i] != vals[0]) { constant = 0; break; }
    if (constant) { *pattern_type = 1; return vals[0]; }

    if (n < 3) return vals[n - 1];

    int d0 = vals[1] - vals[0], linear = 1;
    for (int i = 2; i < n; i++)
        if (vals[i] - vals[i - 1] != d0) { linear = 0; break; }
    if (linear) { *pattern_type = 2; return vals[n - 1] + d0; }

    if (n < 4) return vals[n - 1] + (vals[n - 1] - vals[n - 2]);

    if (vals[0] > 0) {
        int r0 = vals[1] / vals[0], geometric = 1;
        if (r0 > 1) {
            for (int i = 2; i < n; i++) {
                if (vals[i - 1] == 0 || vals[i] / vals[i - 1] != r0)
                { geometric = 0; break; }
            }
            if (geometric) { *pattern_type = 3; return vals[n - 1] * r0; }
        }
    }

    int deltas[MAX_RUNS];
    for (int i = 1; i < n; i++) deltas[i - 1] = vals[i] - vals[i - 1];
    int a0 = deltas[1] - deltas[0], accel = 1;
    for (int i = 2; i < n - 1; i++)
        if (deltas[i] - deltas[i - 1] != a0) { accel = 0; break; }
    if (accel) {
        *pattern_type = 3;
        return vals[n - 1] + deltas[n - 2] + a0;
    }

    return vals[n - 1] + (vals[n - 1] - vals[n - 2]);
}

static Observation observe(const BitStream *b) {
    Observation o;
    memset(&o, 0, sizeof(o));
    RunList runs;
    bs_runs(b, &runs);

    for (int i = 0; i < runs.count; i++) {
        if (runs.types[i] == 1 && o.n_ones < MAX_RUNS)
            o.ones_runs[o.n_ones++] = runs.lengths[i];
        else if (runs.types[i] == 0 && o.n_zeros < MAX_RUNS)
            o.zeros_runs[o.n_zeros++] = runs.lengths[i];
    }

    for (int i = 1; i < o.n_ones && o.n_ones_d < MAX_RUNS; i++)
        o.ones_deltas[o.n_ones_d++] = o.ones_runs[i] - o.ones_runs[i - 1];
    for (int i = 1; i < o.n_zeros && o.n_zeros_d < MAX_RUNS; i++)
        o.zeros_deltas[o.n_zeros_d++] = o.zeros_runs[i] - o.zeros_runs[i - 1];

    o.predicted_next_one = predict_sequence(o.ones_runs, o.n_ones, &o.ones_pattern);
    o.predicted_next_zero = predict_sequence(o.zeros_runs, o.n_zeros, &o.zeros_pattern);

    o.density = b->len > 0 ? (bs_popcount(b) * 100) / b->len : 0;
    o.symmetry = measure_symmetry(b);
    o.period = find_period(b);

    /* Transition rate: bit flips per 100 bits */
    if (b->len > 1) {
        int flips = 0;
        for (int i = 1; i < b->len; i++)
            if (bs_get(b, i) != bs_get(b, i - 1)) flips++;
        o.transition_rate = (flips * 100) / (b->len - 1);
    } else {
        o.transition_rate = 0;
    }

    /* Max run: longest consecutive same-bit stretch */
    o.max_run = 0;
    if (runs.count > 0) {
        for (int i = 0; i < runs.count; i++)
            if (runs.lengths[i] > o.max_run) o.max_run = runs.lengths[i];
    }

    return o;
}

/* ══════════════════════════════════════════════════════════════
 * LAYER 3: THE GRID
 * ══════════════════════════════════════════════════════════════ */
#define MAX_POS 64
#define BIT(n) (1ULL << (n))
#define HIST_LEN 16
#define WIRE_THRESH 16
#define WIRE_DECAY 1
#define WIRE_LEARN 8
#define WIRE_MAX 127
#define DATA_END 8
#define FEEDBACK_START 8
#define FEEDBACK_END 16
#define COMPUTE_START 16
#define COMPUTE_END 24

typedef struct {
    int32_t val[MAX_POS];
    uint64_t reads[MAX_POS];
    uint64_t invert[MAX_POS];
    uint8_t active[MAX_POS];
    int n_pos;
    int32_t history[MAX_POS][HIST_LEN];
    int hist_len[MAX_POS];
    int32_t predicted[MAX_POS];
    int32_t residue[MAX_POS];
    int16_t edge_w[MAX_POS][MAX_POS];
    uint8_t edge_inv[MAX_POS][MAX_POS];
} Grid;

static void grid_init(Grid *g) { memset(g, 0, sizeof(*g)); }

static void grid_wire(Grid *g, uint8_t pos, uint64_t rd, uint64_t inv) {
    g->reads[pos] = rd;
    g->invert[pos] = inv;
    g->active[pos] = 1;
    if (pos >= g->n_pos) g->n_pos = pos + 1;
}

static void grid_tick(Grid *g) {
    int32_t scratch[MAX_POS];
    memcpy(scratch, g->residue, sizeof(scratch));

    for (int p = 0; p < g->n_pos; p++) {
        if (!g->active[p]) continue;
        int32_t accum = 0;
        uint64_t bits = g->reads[p];
        uint64_t inv = g->invert[p];
        while (bits) {
            int b = __builtin_ctzll(bits);
            int32_t v = scratch[b];
            if (inv & (1ULL << b)) v = -v;
            accum += v;
            bits &= bits - 1;
        }
        /* Leaky integrator: decay toward zero, then add new signal.
         * Without decay, feedback loops hit the ±1M clamp and oscillate.
         * Decay rate 7/8 gives ~8-tick memory. Signal must be
         * continuously reinforced or it fades. Silence = understood. */
        g->val[p] = (g->val[p] * 7) / 8 + accum;
        if (g->val[p] > 1000000) g->val[p] = 1000000;
        if (g->val[p] < -1000000) g->val[p] = -1000000;

        g->residue[p] = g->val[p] - g->predicted[p];

        if (g->hist_len[p] < HIST_LEN)
            g->history[p][g->hist_len[p]++] = g->val[p];
        else {
            memmove(g->history[p], g->history[p] + 1,
                    (HIST_LEN - 1) * sizeof(int32_t));
            g->history[p][HIST_LEN - 1] = g->val[p];
        }
        int ptype;
        g->predicted[p] = predict_sequence(
            g->history[p], g->hist_len[p], &ptype);
    }
}

static void self_wire(Grid *g) {
    /* Each compute position has a HOME data source based on its index.
     * pos16 → data[0] (density), pos17 → data[1] (symmetry), etc.
     * Home source gets double learning rate → specialization.
     *
     * Competitive wiring: for each source q, only the compute position
     * with the strongest edge from q keeps it above threshold.
     * This prevents all-to-all degeneracy where every compute node
     * wires identically. Position IS value — different positions
     * must wire differently. */

    /* Phase 1: Update edge weights (with positional affinity) */
    for (int p = COMPUTE_START; p < COMPUTE_END && p < g->n_pos; p++) {
        if (!g->active[p]) continue;
        int32_t rp = g->residue[p];
        int has_edges = (g->reads[p] != 0);
        int home = (p - COMPUTE_START) % DATA_END;

        /* Compute nodes wire from DATA only (positions 0..DATA_END-1).
         * NOT from feedback (8-15) — that path already exists through
         * engine_ingest's explicit feedback loop. Wiring compute→feedback
         * via self_wire creates a double feedback path that amplifies.
         * Clean architecture: DATA is the world. Feedback is the loop.
         * They don't cross in self_wire. */
        for (int q = 0; q < DATA_END; q++) {
            if (q == p) continue;
            int32_t rq = g->residue[q];
            int learn = WIRE_LEARN;
            if (q == home) learn = WIRE_LEARN * 3;  /* home source: 3x */

            if (!has_edges && rq != 0) {
                /* No edges yet: bootstrap. Prefer home source. */
                int target = (q == home) ? WIRE_THRESH + learn : WIRE_THRESH;
                if (g->edge_w[q][p] < target)
                    g->edge_w[q][p] = target;
            } else if (rp != 0 && rq != 0) {
                int abs_rp = abs(rp);
                int help_n = abs_rp - abs(rp + rq);
                int help_i = abs_rp - abs(rp - rq);
                if (help_n > 0 || help_i > 0) {
                    g->edge_w[q][p] += learn;
                    g->edge_inv[q][p] = (help_i > help_n) ? 1 : 0;
                } else {
                    g->edge_w[q][p] -= WIRE_DECAY;
                }
            } else {
                g->edge_w[q][p] -= WIRE_DECAY;
            }
            if (g->edge_w[q][p] < 0) g->edge_w[q][p] = 0;
            if (g->edge_w[q][p] > WIRE_MAX) g->edge_w[q][p] = WIRE_MAX;
        }
    }

    /* Phase 2: Competitive wiring — for each non-compute source q,
     * find which compute position has the strongest edge.
     * Suppress others by decaying them toward threshold.
     * This forces specialization: each source routes primarily
     * to one compute node, not all of them. */
    for (int q = 0; q < DATA_END; q++) {
        int best_p = -1, best_w = 0;
        for (int p = COMPUTE_START; p < COMPUTE_END && p < g->n_pos; p++) {
            if (g->edge_w[q][p] > best_w) {
                best_w = g->edge_w[q][p];
                best_p = p;
            }
        }
        if (best_p >= 0 && best_w > WIRE_THRESH + 4) {
            /* Suppress losers: decay non-winners faster */
            for (int p = COMPUTE_START; p < COMPUTE_END && p < g->n_pos; p++) {
                if (p == best_p) continue;
                if (g->edge_w[q][p] > WIRE_THRESH)
                    g->edge_w[q][p] -= 2;  /* extra decay for non-winners */
            }
        }
    }

    /* Phase 3: Rebuild read masks from surviving edges (DATA only) */
    for (int p = COMPUTE_START; p < COMPUTE_END && p < g->n_pos; p++) {
        if (!g->active[p]) continue;
        g->reads[p] = 0;
        g->invert[p] = 0;
        for (int q = 0; q < DATA_END; q++) {
            if (g->edge_w[q][p] >= WIRE_THRESH) {
                g->reads[p] |= BIT(q);
                if (g->edge_inv[q][p])
                    g->invert[p] |= BIT(q);
            }
        }
    }
}

static int obs_bool(int32_t v) { return v > 0 ? 1 : 0; }
static int obs_all(int32_t v, int n) { return v >= n ? 1 : 0; }
static int obs_parity(int32_t v) { return (v & 1) ? 1 : 0; }
static int obs_raw(int32_t v) { return v; }

/* ══════════════════════════════════════════════════════════════
 * LAYER 4: THE SYSTEM
 * ══════════════════════════════════════════════════════════════ */
typedef struct {
    BitStream stream;
    Observation analysis;
    Grid grid;
    int32_t feedback[8];
    int tick_count;
    int total_inputs;
} Engine;

static void engine_init(Engine *e) {
    memset(e, 0, sizeof(*e));
    grid_init(&e->grid);
}

static void engine_ingest(Engine *e, const uint8_t *data, int len) {
    bs_from_bytes(&e->stream, data, len);
    e->total_inputs++;
    e->analysis = observe(&e->stream);
    Observation *a = &e->analysis;

    if (e->total_inputs == 1) {
        for (int p = COMPUTE_START; p < COMPUTE_END; p++)
            e->grid.active[p] = 1;
    }
    if (e->grid.n_pos < COMPUTE_END)
        e->grid.n_pos = COMPUTE_END;

    int32_t new_vals[DATA_END] = {
        a->density,              /* 0: proportion of 1s */
        a->symmetry,             /* 1: first half == second half */
        a->period,               /* 2: autocorrelation period */
        a->predicted_next_one,   /* 3: next ones-run prediction */
        a->predicted_next_zero,  /* 4: next zeros-run prediction */
        a->ones_pattern * 10 + a->zeros_pattern,  /* 5: pattern type combo */
        a->transition_rate,      /* 6: bit flip frequency */
        a->max_run               /* 7: longest same-bit stretch */
    };

    for (int i = 0; i < DATA_END; i++) {
        e->grid.val[i] = new_vals[i];
        e->grid.residue[i] = new_vals[i] - e->grid.predicted[i];
        if (e->grid.hist_len[i] < HIST_LEN)
            e->grid.history[i][e->grid.hist_len[i]++] = new_vals[i];
        else {
            memmove(e->grid.history[i], e->grid.history[i] + 1,
                    (HIST_LEN - 1) * sizeof(int32_t));
            e->grid.history[i][HIST_LEN - 1] = new_vals[i];
        }
        int ptype;
        e->grid.predicted[i] = predict_sequence(
            e->grid.history[i], e->grid.hist_len[i], &ptype);
    }

    for (int i = 0; i < 8; i++) {
        int p = FEEDBACK_START + i;
        int32_t fv = e->feedback[i];
        e->grid.val[p] = fv;
        e->grid.residue[p] = fv - e->grid.predicted[p];
        if (e->grid.hist_len[p] < HIST_LEN)
            e->grid.history[p][e->grid.hist_len[p]++] = fv;
        else {
            memmove(e->grid.history[p], e->grid.history[p] + 1,
                    (HIST_LEN - 1) * sizeof(int32_t));
            e->grid.history[p][HIST_LEN - 1] = fv;
        }
        int ptype;
        e->grid.predicted[p] = predict_sequence(
            e->grid.history[p], e->grid.hist_len[p], &ptype);
    }

    self_wire(&e->grid);
    grid_tick(&e->grid);
    e->tick_count++;

    e->feedback[0] = obs_raw(e->grid.val[16]);
    e->feedback[1] = obs_raw(e->grid.val[17]);
    e->feedback[2] = obs_raw(e->grid.val[18]);
    e->feedback[3] = obs_raw(e->grid.val[19]);
    e->feedback[4] = obs_bool(e->grid.val[20]);
    e->feedback[5] = obs_raw(e->grid.val[21]);
    e->feedback[6] = obs_parity(e->grid.val[22]);
    e->feedback[7] = obs_raw(e->grid.val[23]);
}

static void engine_ingest_string(Engine *e, const char *str) {
    engine_ingest(e, (const uint8_t *)str, (int)strlen(str));
}

static void engine_ingest_int(Engine *e, int32_t v) {
    engine_ingest(e, (const uint8_t *)&v, sizeof(v));
}

static int engine_total_residue(const Engine *e) {
    long long total = 0;
    for (int p = 0; p < e->grid.n_pos; p++)
        total += abs(e->grid.residue[p]);
    return (int)(total > 999999 ? 999999 : total);
}

static int engine_data_residue(const Engine *e) {
    int total = 0;
    for (int p = 0; p < DATA_END; p++)
        total += abs(e->grid.residue[p]);
    return total;
}

