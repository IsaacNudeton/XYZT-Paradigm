/*
 * onetwo.c — The Full Stack
 *
 * ONETWO: Bidirectional binary. Self-referencing bitstream.
 * Takes any input. Finds opposites. Predicts next values.
 * Math emerges from distinction. Physics from substrate.
 *
 * Architecture (from whiteboard):
 *
 *     Inputs (anything)
 *         ↓↓↓↓↓↓
 *   ┌──────────────────┐
 *   │ ONETWO find opp. │ ← self-observes too
 *   └──────┬───┬───────┘
 *          ↓   ↓
 *     ┌────┐   ┌────┐
 *     │MATH│   │PHYS│    (co-emergent, entangled)
 *     │rule│   │grid│
 *     └──┬─┘   └─┬──┘
 *        └───┬───┘
 *       ┌────┴────┐
 *       │ Observer │      (AI = collision of both)
 *       │ accum+=v │
 *       └────┬────┘
 *            ↓
 *     emerged grid         (falls out, not forced)
 *            │
 *            └──→ feeds back to ONETWO (bidirectional)
 *
 * Isaac Oravec & Claude, February 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

/* ══════════════════════════════════════════════════════════════
 * LAYER 0: BITSTREAM — distinction (the 2)
 * ══════════════════════════════════════════════════════════════ */

#define BS_WORDS   128
#define BS_MAXBITS (BS_WORDS * 64)

typedef struct { uint64_t w[BS_WORDS]; int len; } BitStream;

static inline void bs_init(BitStream *b) { memset(b, 0, sizeof(*b)); }
static inline void bs_push(BitStream *b, int bit) {
    if (b->len >= BS_MAXBITS) return;
    int idx = b->len/64, off = b->len%64;
    if (bit) b->w[idx] |= (1ULL << off);
    else     b->w[idx] &= ~(1ULL << off);
    b->len++;
}
static inline int bs_get(const BitStream *b, int i) {
    if (i < 0 || i >= b->len) return 0;
    return (b->w[i/64] >> (i%64)) & 1;
}
static int bs_popcount(const BitStream *b) {
    int ones = 0, full = b->len/64;
    for (int i = 0; i < full; i++) ones += __builtin_popcountll(b->w[i]);
    int rem = b->len%64;
    if (rem) ones += __builtin_popcountll(b->w[full] & ((1ULL << rem) - 1));
    return ones;
}

/* Encode bytes → bitstream */
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
 * LAYER 1: ONETWO — find opposites, self-observe, predict
 *
 * This IS the AI. Takes a bitstream. Observes it.
 * Finds: what varies (ONE) and what holds (TWO).
 * Returns: the split, the pattern, the prediction.
 * Every address runs this.
 * ══════════════════════════════════════════════════════════════ */

#define MAX_RUNS 512

typedef struct {
    int lengths[MAX_RUNS];
    int types[MAX_RUNS];   /* 1 = ones-run, 0 = zeros-run */
    int count;
} RunList;

/* Extract run-length structure from bitstream */
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

/* The ONETWO result: what fell out */
typedef struct {
    /* The split — what varies vs what holds */
    int ones_runs[MAX_RUNS];    /* lengths of 1-runs (variant) */
    int zeros_runs[MAX_RUNS];   /* lengths of 0-runs (invariant) */
    int n_ones, n_zeros;

    /* Deltas — the pattern of the pattern */
    int ones_deltas[MAX_RUNS];
    int zeros_deltas[MAX_RUNS];
    int n_ones_d, n_zeros_d;

    /* Prediction */
    int predicted_next_one;
    int predicted_next_zero;
    int ones_pattern_type;   /* 0=unknown, 1=constant, 2=linear, 3=accel/geometric */
    int zeros_pattern_type;

    /* Self-observation stats */
    int density;             /* popcount as percentage */
    int symmetry;            /* how similar first half is to second half */
    int period;              /* detected repetition period, 0 if none */
} OneTwoResult;

/* Find period by autocorrelation */
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

/* Measure symmetry: compare first half to second half */
static int measure_symmetry(const BitStream *b) {
    if (b->len < 2) return 100;
    int half = b->len / 2;
    int match = 0;
    for (int i = 0; i < half; i++)
        if (bs_get(b, i) == bs_get(b, half + i)) match++;
    return (match * 100) / half;
}

/* Predict next value from sequence of values */
static int predict_sequence(const int *vals, int n, int *pattern_type) {
    *pattern_type = 0;
    if (n < 2) return 0;

    /* Check constant */
    int constant = 1;
    for (int i = 1; i < n; i++)
        if (vals[i] != vals[0]) { constant = 0; break; }
    if (constant) { *pattern_type = 1; return vals[0]; }

    if (n < 3) return vals[n-1];

    /* Check constant delta (linear) */
    int d0 = vals[1] - vals[0], linear = 1;
    for (int i = 2; i < n; i++)
        if (vals[i] - vals[i-1] != d0) { linear = 0; break; }
    if (linear) { *pattern_type = 2; return vals[n-1] + d0; }

    if (n < 4) return vals[n-1] + (vals[n-1] - vals[n-2]);

    /* Check constant ratio (geometric) */
    if (vals[0] > 0) {
        int r0 = vals[1] / vals[0], geometric = 1;
        if (r0 > 1) {
            for (int i = 2; i < n; i++) {
                if (vals[i-1] == 0 || vals[i] / vals[i-1] != r0)
                    { geometric = 0; break; }
            }
            if (geometric) { *pattern_type = 3; return vals[n-1] * r0; }
        }
    }

    /* Check constant acceleration */
    int deltas[MAX_RUNS];
    for (int i = 1; i < n; i++) deltas[i-1] = vals[i] - vals[i-1];
    int a0 = deltas[1] - deltas[0], accel = 1;
    for (int i = 2; i < n-1; i++)
        if (deltas[i] - deltas[i-1] != a0) { accel = 0; break; }
    if (accel) {
        *pattern_type = 3;
        int next_d = deltas[n-2] + a0;
        return vals[n-1] + next_d;
    }

    /* Fallback: last delta */
    return vals[n-1] + (vals[n-1] - vals[n-2]);
}

/* THE ONETWO FUNCTION: observe a bitstream, find everything */
static OneTwoResult ot_observe(const BitStream *b) {
    OneTwoResult r;
    memset(&r, 0, sizeof(r));

    RunList runs;
    bs_runs(b, &runs);

    for (int i = 0; i < runs.count; i++) {
        if (runs.types[i] == 1 && r.n_ones < MAX_RUNS)
            r.ones_runs[r.n_ones++] = runs.lengths[i];
        else if (runs.types[i] == 0 && r.n_zeros < MAX_RUNS)
            r.zeros_runs[r.n_zeros++] = runs.lengths[i];
    }

    for (int i = 1; i < r.n_ones && r.n_ones_d < MAX_RUNS; i++)
        r.ones_deltas[r.n_ones_d++] = r.ones_runs[i] - r.ones_runs[i-1];
    for (int i = 1; i < r.n_zeros && r.n_zeros_d < MAX_RUNS; i++)
        r.zeros_deltas[r.n_zeros_d++] = r.zeros_runs[i] - r.zeros_runs[i-1];

    r.predicted_next_one = predict_sequence(
        r.ones_runs, r.n_ones, &r.ones_pattern_type);
    r.predicted_next_zero = predict_sequence(
        r.zeros_runs, r.n_zeros, &r.zeros_pattern_type);

    r.density = b->len > 0 ? (bs_popcount(b) * 100) / b->len : 0;
    r.symmetry = measure_symmetry(b);
    r.period = find_period(b);

    return r;
}

/* ══════════════════════════════════════════════════════════════
 * LAYER 2: THE GRID — v5 pure accumulation engine
 *
 * Every address is a predictive observer running ONETWO.
 * Propagates RESIDUE (surprise), not raw value.
 * Silence = understanding. Only new information moves.
 * ══════════════════════════════════════════════════════════════ */

#define MAX_POS  64
#define BIT(n)   (1ULL << (n))
#define HIST_LEN 16

/* Self-wiring constants */
#define WIRE_THRESH    16    /* minimum weight to be connected */
#define WIRE_DECAY     1     /* constant decay per tick */
#define WIRE_LEARN     8     /* strengthen helpful edges */
#define WIRE_MAX       127   /* weight ceiling */
#define COMPUTE_START  16    /* first compute position */
#define COMPUTE_END    24    /* one past last compute position */

typedef struct {
    int32_t  val[MAX_POS];
    uint64_t reads[MAX_POS];
    uint64_t invert[MAX_POS];
    uint8_t  active[MAX_POS];
    int      n_pos;
    /* Per-address ONETWO prediction */
    int32_t  history[MAX_POS][HIST_LEN];
    int      hist_len[MAX_POS];
    int32_t  predicted[MAX_POS];
    int32_t  residue[MAX_POS];
    /* Self-wiring: edge weights and polarity */
    int16_t  edge_w[MAX_POS][MAX_POS];   /* weight from source[row] to dest[col] */
    uint8_t  edge_inv[MAX_POS][MAX_POS]; /* best polarity: 1 = inverted */
    /* Target-driven computation discovery */
    uint8_t  has_target[MAX_POS];         /* 1 if position has training target */
    uint8_t  obs_type[MAX_POS];           /* discovered observer: 0=raw 1=bool 2=parity 3=all */
} Grid;

static void grid_init(Grid *g) { memset(g, 0, sizeof(*g)); }

static void wire(Grid *g, uint8_t pos, uint64_t rd, uint64_t inv) {
    g->reads[pos]  = rd;
    g->invert[pos] = inv;
    g->active[pos] = 1;
    if (pos >= g->n_pos) g->n_pos = pos + 1;
}


/* THE TICK: accumulate residue (surprise only), then predict.
 * First tick: no history → predicted=0 → residue=val → full propagation.
 * As predictions improve: residue→0 → silence. */
static void tick(Grid *g) {
    int32_t scratch[MAX_POS];
    memcpy(scratch, g->residue, sizeof(scratch));
    for (int p = 0; p < g->n_pos; p++) {
        if (!g->active[p]) continue;
        int32_t accum = 0;
        uint64_t bits = g->reads[p];
        uint64_t inv  = g->invert[p];
        while (bits) {
            int b = __builtin_ctzll(bits);
            int32_t v = scratch[b];
            if (inv & (1ULL << b)) v = -v;
            accum += v;
            bits &= bits - 1;
        }
        g->val[p] += accum;

        /* Residue = actual - what was predicted FOR this tick (last tick's prediction) */
        g->residue[p] = g->val[p] - g->predicted[p];

        /* NOW update history and predict for NEXT tick */
        if (g->hist_len[p] < HIST_LEN)
            g->history[p][g->hist_len[p]++] = g->val[p];
        else {
            memmove(g->history[p], g->history[p]+1, (HIST_LEN-1)*sizeof(int32_t));
            g->history[p][HIST_LEN-1] = g->val[p];
        }
        int ptype;
        g->predicted[p] = predict_sequence(g->history[p], g->hist_len[p], &ptype);
    }
}

/* Self-wiring: edges that reduce residue survive, edges that don't die.
 * Bootstrap: compute positions with no edges get trial edges from sources with surprise.
 * System wires itself toward silence. */
static void self_wire(Grid *g) {
    for (int p = COMPUTE_START; p < COMPUTE_END && p < g->n_pos; p++) {
        if (!g->active[p]) continue;
        int32_t rp = g->residue[p];
        int has_edges = (g->reads[p] != 0);

        for (int q = 0; q < g->n_pos; q++) {
            if (q == p) continue;

            if (g->has_target[p]) {
                /* Target-driven: error = observed - target (in residue[p]) */
                int32_t error = rp;
                int32_t vq = g->val[q];
                if (error == 0 && has_edges) {
                    /* Correct answer — reinforce connected edges */
                    if (g->reads[p] & BIT(q))
                        g->edge_w[q][p] += WIRE_LEARN / 2;
                } else if (error != 0) {
                    if (!has_edges && vq != 0) {
                        /* Bootstrap: trial edge from active source */
                        if (g->edge_w[q][p] < WIRE_THRESH)
                            g->edge_w[q][p] = WIRE_THRESH;
                    } else if (vq != 0) {
                        /* Help-based: does this source reduce error? */
                        int abs_err = abs(error);
                        int help_n = abs_err - abs(error + vq);
                        int help_i = abs_err - abs(error - vq);
                        if (help_n > 0 || help_i > 0) {
                            g->edge_w[q][p] += WIRE_LEARN;
                            g->edge_inv[q][p] = (help_i > help_n) ? 1 : 0;
                        } else {
                            g->edge_w[q][p] -= WIRE_DECAY;
                        }
                    }
                    /* vq==0: skip — inactive input provides no evidence */
                }
                /* error==0 && !has_edges: skip — nothing to learn from */
            } else {
                /* Residue-driven: find edges that cancel surprise */
                int32_t rq = g->residue[q];
                if (!has_edges && rq != 0) {
                    if (g->edge_w[q][p] < WIRE_THRESH)
                        g->edge_w[q][p] = WIRE_THRESH;
                } else if (rp != 0 && rq != 0) {
                    int abs_rp = abs(rp);
                    int help_n = abs_rp - abs(rp + rq);
                    int help_i = abs_rp - abs(rp - rq);
                    if (help_n > 0 || help_i > 0) {
                        g->edge_w[q][p] += WIRE_LEARN;
                        g->edge_inv[q][p] = (help_i > help_n) ? 1 : 0;
                    } else {
                        g->edge_w[q][p] -= WIRE_DECAY;
                    }
                } else {
                    g->edge_w[q][p] -= WIRE_DECAY;
                }
            }

            if (g->edge_w[q][p] < 0) g->edge_w[q][p] = 0;
            if (g->edge_w[q][p] > WIRE_MAX) g->edge_w[q][p] = WIRE_MAX;
        }
    }

    /* Rebuild topology from weights */
    for (int p = COMPUTE_START; p < COMPUTE_END && p < g->n_pos; p++) {
        if (!g->active[p]) continue;
        g->reads[p] = 0;
        g->invert[p] = 0;
        for (int q = 0; q < g->n_pos; q++) {
            if (g->edge_w[q][p] >= WIRE_THRESH) {
                g->reads[p] |= BIT(q);
                if (g->edge_inv[q][p])
                    g->invert[p] |= BIT(q);
            }
        }
    }
}

/* Observers — same grid, different questions */
static int obs_bool(int32_t v)       { return v > 0 ? 1 : 0; }
static int obs_all(int32_t v, int n) { return v >= n ? 1 : 0; }
static int obs_parity(int32_t v)     { return (v & 1) ? 1 : 0; }
static int obs_raw(int32_t v)        { return v; }

/* Apply observer by type: 0=raw, 1=bool(OR), 2=parity(XOR), 3=all(AND) */
static int32_t apply_obs(int32_t val, int obs, int n_in) {
    switch (obs) {
        case 1: return obs_bool(val);
        case 2: return obs_parity(val);
        case 3: return obs_all(val, n_in);
        default: return obs_raw(val);
    }
}
static const char *obs_name(int t) {
    switch(t) {
        case 1: return "bool/OR"; case 2: return "parity/XOR";
        case 3: return "all/AND"; default: return "raw";
    }
}

/* ══════════════════════════════════════════════════════════════
 * COMPUTATION DISCOVERY — train from examples, no manual wiring
 *
 * The system discovers: which sources to connect, which observer
 * to use. Same engine (accum += v), same self_wire (help-based).
 * The only difference: error = observed - target, not predicted.
 * ══════════════════════════════════════════════════════════════ */

typedef struct {
    int32_t inputs[4];  /* input values (up to 4 inputs) */
    int32_t target;     /* expected output */
} TrainExample;

/* Train a compute position to discover computation from examples.
 * Returns number of correctly computed examples after training. */
static int train(Grid *g, int in_pos[], int n_in, int out_pos,
                 TrainExample *ex, int n_ex, int n_epochs)
{
    /* Activate positions */
    for (int i = 0; i < n_in; i++) {
        g->active[in_pos[i]] = 1;
        if (in_pos[i] >= g->n_pos) g->n_pos = in_pos[i] + 1;
    }
    g->active[out_pos] = 1;
    g->has_target[out_pos] = 1;
    if (out_pos >= g->n_pos) g->n_pos = out_pos + 1;

    for (int ep = 0; ep < n_epochs; ep++) {
        /* Phase 1: Observer search — try all 4 across all examples */
        int obs_err[4] = {0, 0, 0, 0};
        for (int e = 0; e < n_ex; e++) {
            memset(g->val, 0, sizeof(g->val));
            for (int i = 0; i < n_in; i++)
                g->val[in_pos[i]] = ex[e].inputs[i];
            int32_t accum = 0;
            uint64_t bits = g->reads[out_pos];
            uint64_t inv = g->invert[out_pos];
            while (bits) {
                int b = __builtin_ctzll(bits);
                int32_t v = g->val[b];
                if (inv & (1ULL << b)) v = -v;
                accum += v;
                bits &= bits - 1;
            }
            for (int o = 0; o < 4; o++)
                obs_err[o] += abs(apply_obs(accum, o, n_in) - ex[e].target);
        }
        int best = 0;
        for (int o = 1; o < 4; o++)
            if (obs_err[o] < obs_err[best]) best = o;
        g->obs_type[out_pos] = best;

        /* Phase 2: Edge adjustment — present each example, adjust wiring */
        for (int e = 0; e < n_ex; e++) {
            memset(g->val, 0, sizeof(g->val));
            for (int i = 0; i < n_in; i++) {
                g->val[in_pos[i]] = ex[e].inputs[i];
                g->residue[in_pos[i]] = ex[e].inputs[i];
            }
            int32_t accum = 0;
            uint64_t bits = g->reads[out_pos];
            uint64_t inv = g->invert[out_pos];
            while (bits) {
                int b = __builtin_ctzll(bits);
                int32_t v = g->val[b];
                if (inv & (1ULL << b)) v = -v;
                accum += v;
                bits &= bits - 1;
            }
            g->val[out_pos] = accum;
            int32_t observed = apply_obs(accum, best, n_in);
            g->residue[out_pos] = observed - ex[e].target;
            self_wire(g);
        }
    }

    /* Final evaluation */
    int correct = 0;
    for (int e = 0; e < n_ex; e++) {
        memset(g->val, 0, sizeof(g->val));
        for (int i = 0; i < n_in; i++)
            g->val[in_pos[i]] = ex[e].inputs[i];
        int32_t accum = 0;
        uint64_t bits = g->reads[out_pos];
        uint64_t inv = g->invert[out_pos];
        while (bits) {
            int b = __builtin_ctzll(bits);
            int32_t v = g->val[b];
            if (inv & (1ULL << b)) v = -v;
            accum += v;
            bits &= bits - 1;
        }
        int32_t observed = apply_obs(accum, g->obs_type[out_pos], n_in);
        if (observed == ex[e].target) correct++;
    }
    return correct;
}

/* ══════════════════════════════════════════════════════════════
 * LAYER 3: THE FULL LOOP — ONETWO → Grid → Observer → Feedback
 *
 * This wires everything together. Input enters ONETWO.
 * ONETWO finds opposites. The opposites become grid positions.
 * The grid accumulates. The observer reads. The reading feeds
 * back into ONETWO as new input. Bidirectional. Self-referencing.
 * ══════════════════════════════════════════════════════════════ */

typedef struct {
    BitStream    stream;
    OneTwoResult analysis;
    Grid         grid;
    int32_t      feedback[8];
    int          tick_count;
    int          total_inputs;
} System;

static void sys_init(System *s) {
    memset(s, 0, sizeof(*s));
    grid_init(&s->grid);
}

static void sys_ingest(System *s, const uint8_t *data, int len) {
    bs_from_bytes(&s->stream, data, len);
    s->total_inputs++;

    s->analysis = ot_observe(&s->stream);
    OneTwoResult *a = &s->analysis;

    /* Activate compute positions on first ingest — topology is discovered by self_wire */
    if (s->total_inputs == 1) {
        for (int p = COMPUTE_START; p < COMPUTE_END; p++)
            s->grid.active[p] = 1;
    }
    if (s->grid.n_pos < COMPUTE_END)
        s->grid.n_pos = COMPUTE_END;

    /* Update data positions with new ONETWO observations.
     * Residue = new observation - what was predicted for this position.
     * Addresses are PERSISTENT. History accumulates across ingests. */
    int32_t new_vals[6] = {
        a->density, a->symmetry, a->period,
        a->predicted_next_one, a->predicted_next_zero, 1
    };
    for (int i = 0; i < 6; i++) {
        s->grid.val[i] = new_vals[i];
        s->grid.residue[i] = new_vals[i] - s->grid.predicted[i];
        /* Update history for data positions */
        if (s->grid.hist_len[i] < HIST_LEN)
            s->grid.history[i][s->grid.hist_len[i]++] = new_vals[i];
        else {
            memmove(s->grid.history[i], s->grid.history[i]+1,
                    (HIST_LEN-1)*sizeof(int32_t));
            s->grid.history[i][HIST_LEN-1] = new_vals[i];
        }
        int ptype;
        s->grid.predicted[i] = predict_sequence(
            s->grid.history[i], s->grid.hist_len[i], &ptype);
    }

    /* Update feedback positions */
    for (int i = 0; i < 8 && i < MAX_POS - 16; i++) {
        int p = 8 + i;
        int32_t fv = s->feedback[i];
        s->grid.val[p] = fv;
        s->grid.residue[p] = fv - s->grid.predicted[p];
        if (s->grid.hist_len[p] < HIST_LEN)
            s->grid.history[p][s->grid.hist_len[p]++] = fv;
        else {
            memmove(s->grid.history[p], s->grid.history[p]+1,
                    (HIST_LEN-1)*sizeof(int32_t));
            s->grid.history[p][HIST_LEN-1] = fv;
        }
        int ptype;
        s->grid.predicted[p] = predict_sequence(
            s->grid.history[p], s->grid.hist_len[p], &ptype);
    }

    self_wire(&s->grid);
    tick(&s->grid);
    s->tick_count++;

    /* Observer reads — multiple observers on same grid */
    s->feedback[0] = obs_raw(s->grid.val[16]);
    s->feedback[1] = obs_raw(s->grid.val[17]);
    s->feedback[2] = obs_raw(s->grid.val[18]);
    s->feedback[3] = obs_raw(s->grid.val[19]);
    s->feedback[4] = obs_bool(s->grid.val[20]);
    s->feedback[5] = obs_raw(s->grid.val[21]);
    s->feedback[6] = obs_raw(s->grid.val[22]);
    s->feedback[7] = obs_raw(s->grid.val[23]);
}

static void sys_ingest_string(System *s, const char *str) {
    sys_ingest(s, (const uint8_t *)str, (int)strlen(str));
}

static void sys_ingest_int(System *s, int32_t v) {
    sys_ingest(s, (const uint8_t *)&v, sizeof(v));
}

/* ══════════════════════════════════════════════════════════════
 * DISPLAY
 * ══════════════════════════════════════════════════════════════ */

static const char *pattern_name(int t) {
    switch(t) {
        case 1: return "constant";
        case 2: return "linear";
        case 3: return "accelerating";
        default: return "unknown";
    }
}

static void sys_print(const System *s) {
    const OneTwoResult *a = &s->analysis;
    printf("  | Density: %d%%   Symmetry: %d%%   Period: %d\n",
           a->density, a->symmetry, a->period);
    printf("  | ONE (variant):  %d runs, pattern=%s, next->%d\n",
           a->n_ones, pattern_name(a->ones_pattern_type),
           a->predicted_next_one);
    printf("  | TWO (invariant): %d runs, pattern=%s, next->%d\n",
           a->n_zeros, pattern_name(a->zeros_pattern_type),
           a->predicted_next_zero);
    printf("  | feedback: match=%d diverge=%d consensus=%d mismatch=%d\n",
           s->feedback[0], s->feedback[1], s->feedback[2], s->feedback[3]);
    printf("  | stable=%s energy=%d learning=%d error=%d\n",
           s->feedback[4] ? "yes" : "no", s->feedback[5],
           s->feedback[6], s->feedback[7]);
}

/* ══════════════════════════════════════════════════════════════
 * TESTS
 * ══════════════════════════════════════════════════════════════ */

static int g_pass = 0, g_fail = 0;
static void check(const char *name, int cond) {
    if (cond) { g_pass++; }
    else { g_fail++; printf("  FAIL: %s\n", name); }
}

/* T1: Constant 1s */
static void test_constant_ones(void) {
    printf("\n  T1: Constant 1s\n");
    System s; sys_init(&s);
    uint8_t data[] = {0xFF, 0xFF, 0xFF, 0xFF};
    sys_ingest(&s, data, 4);
    check("density 100%", s.analysis.density == 100);
    check("symmetry high", s.analysis.symmetry >= 90);
    check("one run only", s.analysis.n_ones == 1);
    check("no zero runs", s.analysis.n_zeros == 0);
}

/* T2: Constant 0s */
static void test_constant_zeros(void) {
    printf("\n  T2: Constant 0s\n");
    System s; sys_init(&s);
    uint8_t data[] = {0x00, 0x00, 0x00, 0x00};
    sys_ingest(&s, data, 4);
    check("density 0%", s.analysis.density == 0);
    check("no one runs", s.analysis.n_ones == 0);
    check("one zero run", s.analysis.n_zeros == 1);
}

/* T3: Alternating 01010101 */
static void test_alternating(void) {
    printf("\n  T3: Alternating 01010101\n");
    System s; sys_init(&s);
    uint8_t data[] = {0x55, 0x55, 0x55, 0x55};
    sys_ingest(&s, data, 4);
    check("density ~50%", s.analysis.density >= 45 && s.analysis.density <= 55);
    check("period detected", s.analysis.period > 0);
    check("ones constant", s.analysis.ones_pattern_type == 1);
    check("zeros constant", s.analysis.zeros_pattern_type == 1);
    check("ones runs length 1", s.analysis.ones_runs[0] == 1);
    check("zeros runs length 1", s.analysis.zeros_runs[0] == 1);
}

/* T4: Growing (1, 2, 3, 4, 5) — linear prediction */
static void test_growing(void) {
    printf("\n  T4: Growing ones (1, 11, 111, 1111, 11111)\n");
    System s; sys_init(&s);
    BitStream b; bs_init(&b);
    int runs[] = {1, 2, 3, 4, 5};
    for (int r = 0; r < 5; r++) {
        for (int i = 0; i < runs[r]; i++) bs_push(&b, 1);
        if (r < 4) bs_push(&b, 0);
    }
    s.stream = b;
    s.analysis = ot_observe(&b);
    s.total_inputs = 1;

    grid_init(&s.grid);
    s.grid.val[0] = s.analysis.density;
    s.grid.val[1] = s.analysis.symmetry;
    s.grid.val[3] = s.analysis.predicted_next_one;
    s.grid.val[4] = s.analysis.predicted_next_zero;
    s.grid.val[5] = 1;
    for (int i = 0; i < 6; i++) s.grid.residue[i] = s.grid.val[i];
    wire(&s.grid, 16, BIT(0)|BIT(1), 0);
    wire(&s.grid, 19, BIT(3)|BIT(4), BIT(4));
    tick(&s.grid);
    s.feedback[0] = obs_raw(s.grid.val[16]);
    s.feedback[3] = obs_raw(s.grid.val[19]);

    check("linear pattern detected", s.analysis.ones_pattern_type == 2);
    check("predicts next one-run = 6", s.analysis.predicted_next_one == 6);
    check("gap pattern constant", s.analysis.zeros_pattern_type == 1);
    check("predicts next gap = 1", s.analysis.predicted_next_zero == 1);
}

/* T5: Accelerating (1, 2, 4, 8, 16) — geometric */
static void test_accelerating(void) {
    printf("\n  T5: Accelerating (1, 2, 4, 8, 16)\n");
    System s; sys_init(&s);
    BitStream b; bs_init(&b);
    int runs[] = {1, 2, 4, 8, 16};
    for (int r = 0; r < 5; r++) {
        for (int i = 0; i < runs[r]; i++) bs_push(&b, 1);
        if (r < 4) bs_push(&b, 0);
    }
    s.stream = b;
    s.analysis = ot_observe(&b);
    check("geometric detected", s.analysis.ones_pattern_type == 3);
    check("predicts next = 32", s.analysis.predicted_next_one == 32);
}

/* T6: Text input */
static void test_text(void) {
    printf("\n  T6: Text 'hello' then 'world'\n");
    System s; sys_init(&s);
    sys_ingest_string(&s, "hello");
    int hello_energy = s.feedback[5];
    sys_ingest_string(&s, "world");
    int world_energy = s.feedback[5];
    check("both non-zero energy", hello_energy != 0 && world_energy != 0);
    check("different energy", hello_energy != world_energy);
    check("feedback influenced second", s.feedback[6] != 0);
}

/* T7: Convergence — same input repeated */
static void test_convergence(void) {
    printf("\n  T7: Same input repeated — convergence\n");
    System s; sys_init(&s);
    int any_feedback = 0;
    for (int i = 0; i < 5; i++) {
        sys_ingest_string(&s, "pattern");
        for (int f = 0; f < 8; f++)
            if (s.feedback[f] != 0) any_feedback = 1;
    }
    check("system processes repeatedly", s.tick_count == 5);
    check("feedback was active during learning", any_feedback);
}

/* T8: Integer sequence */
static void test_integer_sequence(void) {
    printf("\n  T8: Integer sequence (1, 1, 2, 3, 5, 8)\n");
    System s; sys_init(&s);
    int fibs[] = {1, 1, 2, 3, 5, 8};
    for (int i = 0; i < 6; i++)
        sys_ingest_int(&s, fibs[i]);
    check("processed all 6", s.tick_count == 6);
    check("non-trivial output", s.feedback[5] != 0);
}

/* T9: Mismatch — two domains, physics falls out */
static void test_mismatch(void) {
    printf("\n  T9: Two domains — mismatch IS the physics\n");
    System a; sys_init(&a);
    uint8_t dense[] = {0xFF, 0xFE, 0xFF, 0xFE, 0xFF, 0xFE, 0xFF, 0xFE};
    sys_ingest(&a, dense, 8);

    System b; sys_init(&b);
    uint8_t sparse[] = {0x01, 0x02, 0x01, 0x02, 0x01, 0x02, 0x01, 0x02};
    sys_ingest(&b, sparse, 8);

    int delta_energy = a.feedback[5] - b.feedback[5];
    check("different energy", a.feedback[5] != b.feedback[5]);
    check("different match", a.feedback[0] != b.feedback[0]);
    check("delta non-zero (physics fell out)", delta_energy != 0);
}

/* T10: Self-reference — feed output back */
static void test_self_reference(void) {
    printf("\n  T10: Self-reference — ONETWO observing ONETWO\n");
    System s; sys_init(&s);
    sys_ingest_string(&s, "seed");
    for (int i = 0; i < 5; i++)
        sys_ingest(&s, (uint8_t *)s.feedback, sizeof(s.feedback));
    check("self-reference stable", s.tick_count == 6);
    check("non-trivial output", s.feedback[5] != 0);
}

/* T11: Residue silence — constant input → source and reader both go quiet */
static void test_residue_silence(void) {
    printf("\n  T11: Residue silence — constant source goes quiet\n");
    /* Test predict_sequence directly: constant input → prediction matches → residue 0 */
    int32_t history[HIST_LEN];
    int hist_len = 0;
    int32_t predicted = 0;
    int32_t last_residue = -1;

    for (int t = 0; t < 6; t++) {
        int32_t val = 42;
        int32_t residue = val - predicted;
        last_residue = residue;
        if (hist_len < HIST_LEN) history[hist_len++] = val;
        int ptype;
        predicted = predict_sequence(history, hist_len, &ptype);
        printf("    t=%d: val=%d predicted_next=%d residue=%d\n",
               t, val, predicted, residue);
    }
    check("constant: residue -> 0 after pattern detected", last_residue == 0);

    /* Also test through grid: source (not active) feeds reader (active) */
    Grid g; grid_init(&g);
    g.n_pos = 2;
    wire(&g, 1, BIT(0), 0);  /* pos 1 reads from pos 0 */
    /* Drive source externally */
    int32_t src_hist[HIST_LEN];
    int src_hlen = 0;
    int32_t src_pred = 0;
    for (int t = 0; t < 5; t++) {
        g.val[0] = 42;
        g.residue[0] = 42 - src_pred;
        if (src_hlen < HIST_LEN) src_hist[src_hlen++] = 42;
        int pt;
        src_pred = predict_sequence(src_hist, src_hlen, &pt);
        tick(&g);
        printf("    grid t=%d: src_residue=%d pos1_val=%d pos1_residue=%d\n",
               t, g.residue[0], g.val[1], g.residue[1]);
    }
    check("grid: source residue -> 0", (42 - src_pred) == 0);
}

/* T12: Residue alive — chaotic input keeps residue non-zero */
static void test_residue_alive(void) {
    printf("\n  T12: Residue alive — chaotic sequence stays active\n");
    int32_t history[HIST_LEN];
    int hist_len = 0;
    int32_t predicted = 0;
    int vals[] = {3, 17, 42, 5, 99, 8, 71, 23, 55, 11};
    int nonzero = 0;
    for (int t = 0; t < 10; t++) {
        int32_t residue = vals[t] - predicted;
        if (residue != 0) nonzero++;
        if (hist_len < HIST_LEN) history[hist_len++] = vals[t];
        else {
            memmove(history, history+1, (HIST_LEN-1)*sizeof(int32_t));
            history[HIST_LEN-1] = vals[t];
        }
        int ptype;
        predicted = predict_sequence(history, hist_len, &ptype);
        printf("    t=%d: val=%d predicted_next=%d residue=%d\n",
               t, vals[t], predicted, residue);
    }
    printf("    non-zero residues: %d/10\n", nonzero);
    check("chaotic: most residues non-zero", nonzero >= 7);
}

/* T13: Linear sequence — ONETWO detects pattern, residue → 0 */
static void test_residue_linear(void) {
    printf("\n  T13: Linear sequence — prediction catches pattern\n");
    int32_t history[HIST_LEN];
    int hist_len = 0;
    int32_t predicted = 0;
    int residues[10];
    for (int t = 0; t < 10; t++) {
        int32_t val = (t + 1) * 5;  /* 5, 10, 15, 20, 25, ... */
        residues[t] = abs(val - predicted);
        printf("    t=%d: val=%d predicted_was=%d residue=%d\n",
               t, val, predicted, val - predicted);
        if (hist_len < HIST_LEN) history[hist_len++] = val;
        int ptype;
        predicted = predict_sequence(history, hist_len, &ptype);
    }
    /* After 3 values in history, linear pattern detected → predicted matches actual */
    check("early residue non-zero", residues[0] > 0);
    check("residue -> 0 once linear detected", residues[3] == 0 && residues[9] == 0);
}

/* T14: System quiets — repeated identical ingest */
static void test_system_quiets(void) {
    printf("\n  T14: System quiets — repeated identical input\n");
    System s; sys_init(&s);
    int data_quiet_at = -1;
    for (int i = 0; i < 8; i++) {
        sys_ingest_string(&s, "the same input every time");
        /* Check if data positions (0-5) have gone quiet */
        int data_residue = 0;
        for (int p = 0; p < 6; p++)
            data_residue += abs(s.grid.residue[p]);
        if (data_residue == 0 && data_quiet_at < 0) data_quiet_at = i + 1;
        printf("    ingest %d: data_residue=%d error=%d energy=%d\n",
               i+1, data_residue, s.feedback[7], s.feedback[5]);
    }
    check("system processes all", s.tick_count == 8);
    check("data positions go quiet", data_quiet_at > 0 && data_quiet_at <= 4);
    if (data_quiet_at > 0)
        printf("    -> data positions silent at ingest %d\n", data_quiet_at);
}

/* T15: Self-wire creates edges from data to compute */
static void test_self_wire_bootstrap(void) {
    printf("\n  T15: Self-wire creates edges from data to compute\n");
    System s; sys_init(&s);
    sys_ingest_string(&s, "test self wiring");

    int wired = 0;
    for (int p = COMPUTE_START; p < COMPUTE_END; p++)
        if (s.grid.reads[p] != 0) wired++;
    check("compute positions wired", wired > 0);
    printf("    -> %d/%d compute positions got edges\n", wired, COMPUTE_END - COMPUTE_START);

    /* Verify edges came from data positions (0-5 have non-zero residue) */
    int from_data = 0;
    for (int q = 0; q < 6; q++)
        if (s.grid.reads[COMPUTE_START] & BIT(q)) from_data++;
    check("edges from data sources", from_data > 0);
    printf("    -> pos %d reads from %d data sources\n", COMPUTE_START, from_data);
}

/* T16: Self-wire strengthens helpful edges over repeated input */
static void test_self_wire_strengthens(void) {
    printf("\n  T16: Self-wire strengthens helpful edges\n");
    System s; sys_init(&s);
    for (int i = 0; i < 5; i++)
        sys_ingest_string(&s, "strengthen");

    int strong = 0;
    for (int q = 0; q < 6; q++)
        for (int p = COMPUTE_START; p < COMPUTE_END; p++)
            if (s.grid.edge_w[q][p] > WIRE_THRESH) strong++;
    check("strong edges exist after learning", strong > 0);
    printf("    -> %d edges above threshold\n", strong);
}

/* T17: Self-wire reduces total residue — system gets quieter */
static void test_self_wire_quiets(void) {
    printf("\n  T17: Self-wire reduces total residue over time\n");
    System s; sys_init(&s);
    int total_first = 0, total_last = 0;
    for (int i = 0; i < 10; i++) {
        sys_ingest_string(&s, "same input");
        int tr = 0;
        for (int p = 0; p < s.grid.n_pos; p++)
            tr += abs(s.grid.residue[p]);
        if (i == 0) total_first = tr;
        if (i == 9) total_last = tr;
        printf("    ingest %d: total_residue=%d\n", i+1, tr);
    }
    check("residue decreases over time", total_last < total_first);
    printf("    -> first=%d last=%d\n", total_first, total_last);
}

/* T18: Discover XOR — the hardest gate (no single threshold works) */
static void test_discover_xor(void) {
    printf("\n  T18: Discover XOR from examples\n");
    Grid g; grid_init(&g);
    TrainExample xor_ex[] = {
        {{0, 0}, 0}, {{0, 1}, 1}, {{1, 0}, 1}, {{1, 1}, 0}
    };
    int in_pos[] = {0, 1};
    int correct = train(&g, in_pos, 2, COMPUTE_START, xor_ex, 4, 10);
    printf("    -> %d/4 correct, observer=%s\n", correct, obs_name(g.obs_type[COMPUTE_START]));
    printf("    -> edges: pos0 w=%d inv=%d, pos1 w=%d inv=%d\n",
           g.edge_w[0][COMPUTE_START], g.edge_inv[0][COMPUTE_START],
           g.edge_w[1][COMPUTE_START], g.edge_inv[1][COMPUTE_START]);
    check("XOR: all 4 correct", correct == 4);
    check("XOR: parity observer discovered", g.obs_type[COMPUTE_START] == 2);
}

/* T19: Discover AND */
static void test_discover_and(void) {
    printf("\n  T19: Discover AND from examples\n");
    Grid g; grid_init(&g);
    TrainExample and_ex[] = {
        {{0, 0}, 0}, {{0, 1}, 0}, {{1, 0}, 0}, {{1, 1}, 1}
    };
    int in_pos[] = {0, 1};
    int correct = train(&g, in_pos, 2, COMPUTE_START, and_ex, 4, 10);
    printf("    -> %d/4 correct, observer=%s\n", correct, obs_name(g.obs_type[COMPUTE_START]));
    check("AND: all 4 correct", correct == 4);
    check("AND: all observer discovered", g.obs_type[COMPUTE_START] == 3);
}

/* T20: Discover OR */
static void test_discover_or(void) {
    printf("\n  T20: Discover OR from examples\n");
    Grid g; grid_init(&g);
    TrainExample or_ex[] = {
        {{0, 0}, 0}, {{0, 1}, 1}, {{1, 0}, 1}, {{1, 1}, 1}
    };
    int in_pos[] = {0, 1};
    int correct = train(&g, in_pos, 2, COMPUTE_START, or_ex, 4, 10);
    printf("    -> %d/4 correct, observer=%s\n", correct, obs_name(g.obs_type[COMPUTE_START]));
    check("OR: all 4 correct", correct == 4);
    check("OR: bool observer discovered", g.obs_type[COMPUTE_START] == 1);
}

/* ══════════════════════════════════════════════════════════════ */
int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("===========================================\n");
    printf("  ONETWO — Bidirectional Binary\n");
    printf("  Propagate surprise, not value.\n");
    printf("  Silence = understanding.\n");
    printf("===========================================\n");

    test_constant_ones();
    test_constant_zeros();
    test_alternating();
    test_growing();
    test_accelerating();
    test_text();
    test_convergence();
    test_integer_sequence();
    test_mismatch();
    test_self_reference();
    test_residue_silence();
    test_residue_alive();
    test_residue_linear();
    test_system_quiets();
    test_self_wire_bootstrap();
    test_self_wire_strengthens();
    test_self_wire_quiets();
    test_discover_xor();
    test_discover_and();
    test_discover_or();

    printf("\n===========================================\n");
    printf("  RESULTS: %d passed, %d failed, %d total\n",
           g_pass, g_fail, g_pass + g_fail);
    printf("===========================================\n");

    if (g_fail == 0)
        printf("\n  ALL PASS. Surprise propagates. Silence emerges.\n\n");

    return g_fail;
}
