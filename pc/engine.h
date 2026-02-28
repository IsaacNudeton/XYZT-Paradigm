/*
 * engine.h — Unified XYZT Engine API (CPU side)
 *
 * Shells + Fresnel boundaries + ONETWO feedback + nesting.
 * Extracted from v9 (xyzt.c), cleaned for library use.
 *
 * One engine operation: accum += v. Observer creates all operations.
 * Position is value. Collision is computation.
 *
 * Isaac Oravec & Claude, February 2026
 */

#ifndef XYZT_ENGINE_H
#define XYZT_ENGINE_H

#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <ctype.h>

#ifdef _OPENMP
#include <omp.h>
#endif

/* Portable popcount for 64-bit values.
 * Works with MSVC, GCC, Clang, and CUDA nvcc. */
static inline int xyzt_popcnt64(uint64_t x) {
#if defined(__CUDA_ARCH__)
    return __popcll(x);
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_popcountll(x);
#else
    /* Portable fallback (Hamming weight) */
    x = x - ((x >> 1) & 0x5555555555555555ULL);
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
    return (int)((x * 0x0101010101010101ULL) >> 56);
#endif
}

static inline int xyzt_popcnt32(uint32_t x) {
#if defined(__CUDA_ARCH__)
    return __popc(x);
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_popcount(x);
#else
    x = x - ((x >> 1) & 0x55555555u);
    x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
    x = (x + (x >> 4)) & 0x0F0F0F0Fu;
    return (int)((x * 0x01010101u) >> 24);
#endif
}

static inline int xyzt_ctzll(uint64_t x) {
#if defined(__CUDA_ARCH__)
    return __ffsll(x) - 1;
#elif defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(x);
#else
    if (x == 0) return 64;
    int n = 0;
    if ((x & 0xFFFFFFFF) == 0) { n += 32; x >>= 32; }
    if ((x & 0xFFFF) == 0) { n += 16; x >>= 16; }
    if ((x & 0xFF) == 0) { n += 8; x >>= 8; }
    if ((x & 0xF) == 0) { n += 4; x >>= 4; }
    if ((x & 0x3) == 0) { n += 2; x >>= 2; }
    if ((x & 0x1) == 0) { n += 1; }
    return n;
#endif
}

/* ══════════════════════════════════════════════════════════════
 * {2,3} SUBSTRATE CONSTANTS — from Lean proofs
 * ══════════════════════════════════════════════════════════════ */
#ifndef SUBSTRATE_INT
#define SUBSTRATE_INT      137u    /* 2^7 + 3^2 */
#endif
#define MISMATCH_TAX_NUM    81u    /* 81/2251 ≈ 0.035982 */
#define MISMATCH_TAX_DEN  2251u
#define PRUNE_FLOOR          9u    /* ≈ 0.036 * 255 */
#define VAL_CEILING        1000000 /* hard ceiling on node values (±1M) */
#define LYSIS_THRESHOLD    100     /* valence below this -> kill child (apoptosis) */
#define VALENCE_DECAY_RATE   2     /* per SUBSTRATE_INT cycle under high error */

/* Shell impedance: K = 3/2 per boundary crossing */
static const double SHELL_Z[3] = { 1.0, 1.5, 2.25 };

/* ══════════════════════════════════════════════════════════════
 * BITSTREAM — distinction primitive (the 2)
 * ══════════════════════════════════════════════════════════════ */
#define BS_WORDS   128
#define BS_MAXBITS (BS_WORDS * 64)
#define FP_TOTAL   5312    /* 7-layer text fingerprint total bits */

typedef struct { uint64_t w[BS_WORDS]; int len; } BitStream;

static inline void bs_init(BitStream *b) { memset(b, 0, sizeof(*b)); }

static inline void bs_push(BitStream *b, int bit) {
    if (b->len >= BS_MAXBITS) return;
    int idx = b->len / 64, off = b->len % 64;
    if (bit) b->w[idx] |= (1ULL << off);
    else     b->w[idx] &= ~(1ULL << off);
    b->len++;
}

static inline int bs_get(const BitStream *b, int i) {
    if (i < 0 || i >= b->len) return 0;
    return (b->w[i / 64] >> (i % 64)) & 1;
}

static inline void bs_set(BitStream *b, int pos, int bit) {
    if (pos < 0 || pos >= BS_MAXBITS) return;
    int idx = pos / 64, off = pos % 64;
    if (bit) b->w[idx] |= (1ULL << off);
    else     b->w[idx] &= ~(1ULL << off);
    if (pos >= b->len) b->len = pos + 1;
}

static inline int bs_popcount(const BitStream *b) {
    int ones = 0, full = b->len / 64;
    for (int i = 0; i < full; i++)
        ones += xyzt_popcnt64(b->w[i]);
    int rem = b->len % 64;
    if (rem)
        ones += xyzt_popcnt64(b->w[full] & ((1ULL << rem) - 1));
    return ones;
}

/* Jaccard similarity */
static inline int bs_corr(const BitStream *a, const BitStream *b) {
    int n = a->len < b->len ? a->len : b->len;
    if (n < 1) return 0;
    int and_pop = 0, or_pop = 0, full = n / 64;
    for (int i = 0; i < full; i++) {
        and_pop += xyzt_popcnt64(a->w[i] & b->w[i]);
        or_pop  += xyzt_popcnt64(a->w[i] | b->w[i]);
    }
    int rem = n % 64;
    if (rem) {
        uint64_t mask = (1ULL << rem) - 1;
        and_pop += xyzt_popcnt64((a->w[full] & b->w[full]) & mask);
        or_pop  += xyzt_popcnt64((a->w[full] | b->w[full]) & mask);
    }
    if (or_pop == 0) return 0;
    return (and_pop * 100) / or_pop;
}

/* Containment: popcount(query AND node) / popcount(query) */
static inline int bs_contain(const BitStream *query, const BitStream *node) {
    if (query->len < 1 || node->len < 1) return 0;
    int n = query->len < node->len ? query->len : node->len;
    int and_pop = 0, q_pop = 0, full = n / 64;
    for (int i = 0; i < full; i++) {
        and_pop += xyzt_popcnt64(query->w[i] & node->w[i]);
        q_pop   += xyzt_popcnt64(query->w[i]);
    }
    int rem = n % 64;
    if (rem) {
        uint64_t mask = (1ULL << rem) - 1;
        and_pop += xyzt_popcnt64((query->w[full] & node->w[full]) & mask);
        q_pop   += xyzt_popcnt64(query->w[full] & mask);
    }
    int qfull = query->len / 64;
    for (int i = full + (rem ? 1 : 0); i < qfull; i++)
        q_pop += xyzt_popcnt64(query->w[i]);
    if (q_pop == 0) return 0;
    return (and_pop * 100) / q_pop;
}

static inline int bs_mutual_contain(const BitStream *a, const BitStream *b) {
    int ab = bs_contain(a, b);
    int ba = bs_contain(b, a);
    return ab < ba ? ab : ba;
}

/* Hash + Bloom */
static inline uint32_t hash32(const uint8_t *data, int len) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < len; i++) { h ^= data[i]; h *= 16777619u; }
    return h;
}

static inline void bloom_set(BitStream *bs, int base_off, int bloom_len,
                              const uint8_t *data, int data_len, int n_hashes) {
    uint32_t h1 = hash32(data, data_len);
    uint32_t h2 = (h1 >> 16) | (h1 << 16);
    h2 |= 1;
    for (int i = 0; i < n_hashes; i++) {
        int pos = (h1 + i * h2) % bloom_len;
        bs_set(bs, base_off + pos, 1);
    }
}

/* ══════════════════════════════════════════════════════════════
 * SPATIAL COORDINATES — position IS value
 * ══════════════════════════════════════════════════════════════ */
typedef uint32_t Coord;
static inline Coord coord_pack(int x, int y, int z) {
    return ((x & 0x3FF) << 20) | ((y & 0x3FF) << 10) | (z & 0x3FF);
}
static inline int coord_x(Coord c) { return (c >> 20) & 0x3FF; }
static inline int coord_y(Coord c) { return (c >> 10) & 0x3FF; }
static inline int coord_z(Coord c) { return c & 0x3FF; }

/* ══════════════════════════════════════════════════════════════
 * T — the substrate (time)
 * ══════════════════════════════════════════════════════════════ */
typedef struct {
    uint64_t count;
    double   birth_ms;   /* wall clock at init */
} SubstrateT;

static inline void    T_init(SubstrateT *t) { t->count = 0; t->birth_ms = 0; }
static inline uint64_t T_tick(SubstrateT *t) { return ++t->count; }
static inline uint64_t T_now(const SubstrateT *t) { return t->count; }

/* ══════════════════════════════════════════════════════════════
 * ONETWO OBSERVATION
 * ══════════════════════════════════════════════════════════════ */
#define MAX_RUNS 512

typedef struct {
    int lengths[MAX_RUNS];
    int types[MAX_RUNS];
    int count;
} RunList;

typedef struct {
    int ones_runs[MAX_RUNS], zeros_runs[MAX_RUNS];
    int n_ones, n_zeros;
    int ones_deltas[MAX_RUNS], zeros_deltas[MAX_RUNS];
    int n_ones_d, n_zeros_d;
    int predicted_next_one, predicted_next_zero;
    int ones_pattern_type, zeros_pattern_type;
    int density, symmetry, period;
} OneTwoResult;

typedef struct {
    OneTwoResult analysis;
    int32_t      feedback[8];
    int          tick_count;
    int          total_inputs;
    int32_t      prev_match;
} OneTwoSystem;

/* ══════════════════════════════════════════════════════════════
 * NODE, EDGE, GRAPH — the learning structures
 * ══════════════════════════════════════════════════════════════ */
#define MAX_NODES 4096
#define MAX_EDGES 65536
#define NAME_LEN  128
#define GROW_K    6

typedef struct {
    BitStream identity;
    char      name[NAME_LEN];
    uint32_t  created, last_active, hit_count;
    uint8_t   shell_id;
    uint8_t   layer_zero;
    uint8_t   alive, valence;
    Coord     coord;
    double    Z;
    uint16_t  n_incoming;
    uint8_t   crystal_hist[8];
    uint8_t   crystal_n;
    int32_t   val;
    int32_t   prev_val;       /* val at last SUBSTRATE_INT boundary (for coherence) */
    int64_t   accum;
    int32_t   I_energy;       /* magnetic/current energy at node (for XOR detection) */
    int8_t    coherent;       /* +1 coherent, -1 incoherent, 0 unknown */
    int8_t    child_id;
    uint8_t   has_negation;   /* 1 if source text contained negation markers */
    uint8_t   contradicted;  /* 1 if node has inverted incoming edges (disputed) */
} Node;

typedef struct {
    uint16_t src_a, src_b, dst;
    uint8_t  _pad;
    uint8_t  weight, learn_rate, intershell;
    uint32_t created, last_active;
    uint8_t  invert_a, invert_b;
} Edge;

typedef struct {
    Node *nodes; Edge *edges;
    int n_nodes, n_edges;
    uint64_t total_ticks, total_learns, total_grown, total_pruned, total_boundary_crossings;
    int grow_threshold, prune_threshold, learn_strengthen, learn_weaken, learn_rate;
    int auto_grow, auto_prune, auto_learn;
    int grow_mean;
    int grow_interval, prune_interval;
    const uint8_t *retina;    /* zero-copy view into parent's substrate cube */
    int retina_len;            /* positions visible (64 for 4^3 cube) */
} Graph;

/* Shell — graph with identity */
#define MAX_SHELLS 3
typedef struct { Graph g; uint8_t id; char name[32]; } Shell;

/* v9: nesting */
#define MAX_CHILDREN 4

/* ══════════════════════════════════════════════════════════════
 * SENSE — temporal feature extraction result
 * ══════════════════════════════════════════════════════════════ */
#define SENSE_MAX_FEATS 64
typedef struct {
    int node_ids[SENSE_MAX_FEATS];
    int n_features;
} sense_result_t;

/* ══════════════════════════════════════════════════════════════
 * THE ENGINE
 * ══════════════════════════════════════════════════════════════ */
typedef struct {
    Shell shells[MAX_SHELLS];
    int   n_shells;
    Edge *boundary_edges;
    int   n_boundary_edges, max_boundary_edges;
    uint64_t total_ticks;
    int   learn_interval;
    int   boundary_interval;
    OneTwoSystem onetwo;
    Graph child_pool[MAX_CHILDREN];
    int   child_owner[MAX_CHILDREN];
    int   n_children;
    int   low_error_run;
    SubstrateT T;
    sense_result_t last_sense;
} Engine;

/* ══════════════════════════════════════════════════════════════
 * ENGINE API (implemented in engine.c)
 * ══════════════════════════════════════════════════════════════ */

/* Observers: different questions about the same integer */
static inline int obs_bool(int32_t v)        { return v > 0 ? 1 : 0; }
static inline int obs_all(int32_t v, int n)  { return v >= n ? 1 : 0; }
static inline int obs_parity(int32_t v)      { return (v & 1) ? 1 : 0; }
static inline int obs_raw(int32_t v)         { return v; }
/* XOR = energy arrived but voltage cancelled (destructive collision) */
static inline int obs_xor(int32_t I_energy, int32_t val) {
    return (I_energy > 0 && val == 0) ? 1 : 0;
}
/* Z=1: AND = both inputs exceed threshold (energy gating) */
static inline int obs_and(int32_t val, int32_t I_energy, int threshold) {
    return (abs(val) > threshold && I_energy > threshold) ? 1 : 0;
}
/* Z=3: frequency detector — high n_incoming relative to valence = resonant node */
static inline int obs_freq(uint16_t n_incoming, uint8_t valence) {
    return (n_incoming >= 2 && valence > 0 && n_incoming * 255 > valence * 4) ? 1 : 0;
}
/* Z=4: correlation — val and I_energy trend same direction (co-moving) */
static inline int obs_corr(int32_t val, int32_t prev_val, int32_t I_energy) {
    int val_up = (val > prev_val) ? 1 : 0;
    int energy_present = (I_energy > 0) ? 1 : 0;
    return (val_up == energy_present) ? 1 : 0;
}

/* Fresnel physics */
static inline double fresnel_T(double K) {
    double d = K + 1.0; return (4.0 * K) / (d * d);
}
static inline double fresnel_R(double K) {
    double d = (K - 1.0) / (K + 1.0); return d * d;
}
static inline uint8_t apply_fresnel(int raw, double Z_src, double Z_dst) {
    if (Z_src <= 0.0) Z_src = 1.0;
    double K = Z_dst / Z_src;
    double T = fresnel_T(K);
    int t = (int)(raw * T + 0.5);
    return t > 255 ? 255 : (t < 1 ? 1 : (uint8_t)t);
}

/* Crystal histogram */
void crystal_update(Node *n, Edge *edges, int n_edges, int node_id);
int  crystal_strength(const Node *n);
int  crystal_distance(const Node *a, const Node *b);

/* ONETWO functions */
void bs_runs(const BitStream *b, RunList *r);
int  ot_find_period(const BitStream *b);
int  ot_measure_symmetry(const BitStream *b);
int  ot_predict_sequence(const int *vals, int n, int *pattern_type);
OneTwoResult ot_observe(const BitStream *b);
void ot_sys_init(OneTwoSystem *s);
OneTwoResult ot_sys_ingest(OneTwoSystem *s, const uint8_t *data, int len);

/* Graph operations */
void graph_init(Graph *g);
void graph_destroy(Graph *g);
int  graph_find(Graph *g, const char *name);
int  graph_add(Graph *g, const char *name, uint8_t shell_id, const SubstrateT *T);
int  graph_find_edge(Graph *g, int a, int b, int d);
int  graph_wire(Graph *g, int a, int b, int d, uint8_t w, int inter);
int  graph_learn(Graph *g);
int  graph_compute_z(Graph *g);

/* Engine lifecycle */
void engine_init(Engine *eng);
void engine_destroy(Engine *eng);
int  engine_ingest(Engine *eng, const char *name, const BitStream *data);
int  engine_ingest_text(Engine *eng, const char *name, const char *text);
int  engine_ingest_raw(Engine *eng, const char *name, const char *text);
int  engine_ingest_file(Engine *eng, const char *path);
void engine_tick(Engine *eng);
void engine_status(const Engine *eng);

/* Encoders */
void encode_bytes(BitStream *bs, const uint8_t *data, int len);
void encode_raw(BitStream *bs, const char *data, int len);
void encode_text(BitStream *bs, const char *text);
int  encode_file(BitStream *bs, const char *path);

/* Query */
typedef struct {
    int id, shell;
    int raw_corr, crystal, resonance;
    char name[NAME_LEN];
} QueryResult;

int engine_query(const Engine *eng, const BitStream *query,
                 QueryResult *results, int max_results);

/* Name substring search (for short queries where structural fingerprinting is meaningless) */
int engine_query_name(const Engine *eng, const char *substr,
                      QueryResult *results, int max_results);

/* Save / Load */
int engine_save(const Engine *eng, const char *path);
int engine_load(Engine *eng, const char *path);

/* Nesting */
void nest_remove(Engine *eng, int node_id);

/* Wire graph bridge (toolkit ↔ engine) */
int engine_wire_import(Engine *eng, const char *path);
int engine_wire_export(const Engine *eng, const char *path);

/* .xyzt text assembly interpreter */
int engine_exec(Engine *eng, const char *path);

/* Negation-aware edge inversion */
void edge_set_negation_invert(Graph *g, int edge_id);

#endif /* XYZT_ENGINE_H */
