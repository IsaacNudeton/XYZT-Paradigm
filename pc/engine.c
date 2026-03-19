/*
 * engine.c — Unified XYZT Engine (CPU side)
 *
 * Extracted from v9 (xyzt.c). Shells + Fresnel + ONETWO + nesting.
 * No main(). Library code only.
 *
 * Isaac Oravec & Claude, February 2026
 */

#include "engine.h"
#include "onetwo.h"
#include "transducer.h"
#include "sense.h"
#include <math.h>

/* Yee GPU functions — engine.c is pure C, can't include yee.cuh */
#define YEE_GX 64
#define YEE_GY 64
#define YEE_GZ 64
#define YEE_N_VOXELS (YEE_GX * YEE_GY * YEE_GZ)
extern int yee_download_L(float *h_L, int n);
extern int yee_download_acc_raw(float *h_acc, int n);
extern int yee_upload_L(const float *h_L, int n);
extern int yee_upload_acc(const float *h_acc, int n);
extern int yee_is_initialized(void);

#define YEE1_MAGIC 0x59454531  /* "YEE1" */

/* ══════════════════════════════════════════════════════════════
 * ENCODERS
 * ══════════════════════════════════════════════════════════════ */

/* 7-layer fingerprint offsets */
#define L0_OFF   0
#define L0_LEN   256
#define L1_OFF   256
#define L1_LEN   256
#define L2_OFF   512
#define L2_LEN   2048
#define L3_OFF   2560
#define L3_LEN   2048
#define L4_OFF   4608
#define L4_LEN   64
#define L5_OFF   4672
#define L5_LEN   128
#define L6_OFF   4800
#define L6_LEN   512

void encode_bytes(BitStream *bs, const uint8_t *data, int len) {
    bs_init(bs);
    int max = BS_MAXBITS / 8;
    if (len > max) len = max;
    for (int i = 0; i < len; i++)
        for (int b = 0; b < 8; b++)
            bs_push(bs, (data[i] >> b) & 1);
}

void encode_raw(BitStream *bs, const char *data, int len) {
    bs_init(bs);
    for (int i = 0; i < len && bs->len < BS_MAXBITS; i++)
        for (int b = 0; b < 8 && bs->len < BS_MAXBITS; b++)
            bs_push(bs, (data[i] >> b) & 1);
}

void encode_text(BitStream *bs, const char *text) {
    bs_init(bs);
    if (!text || !*text) return;
    int len = (int)strlen(text);
    if (len == 0) return;
    const uint8_t *data = (const uint8_t *)text;

    bs->len = FP_TOTAL;

    /* L0: Byte presence */
    int freq[256] = {0};
    for (int i = 0; i < len; i++) freq[data[i]]++;
    for (int c = 0; c < 256; c++)
        if (freq[c] > 0) bs_set(bs, L0_OFF + c, 1);

    /* L1: Byte frequency */
    double avg_freq = (double)len / 256.0;
    for (int c = 0; c < 256; c++)
        bs_set(bs, L1_OFF + c, freq[c] > avg_freq ? 1 : 0);

    /* L2: Bigram bloom */
    for (int i = 0; i < len - 1; i++) {
        uint8_t pair[2] = { (uint8_t)tolower(data[i]), (uint8_t)tolower(data[i+1]) };
        bloom_set(bs, L2_OFF, L2_LEN, pair, 2, 3);
    }

    /* L3: Token bloom */
    {
        int tok_count = 0, j = 0;
        while (j < len && tok_count < 5000) {
            while (j < len && !isalnum((unsigned char)data[j])) j++;
            int start = j;
            while (j < len && isalnum((unsigned char)data[j])) j++;
            int tlen = j - start;
            if (tlen >= 2 && tlen <= 64) {
                int nh = tok_count < 500 ? 3 : (tok_count < 2000 ? 2 : 1);
                uint8_t tok_lower[64];
                for (int k = 0; k < tlen; k++)
                    tok_lower[k] = (uint8_t)tolower(data[start + k]);
                bloom_set(bs, L3_OFF, L3_LEN, tok_lower, tlen, nh);
                tok_count++;
            }
        }
    }

    /* L4: Entropy profile */
    {
        double global_ent = 0.0;
        for (int c = 0; c < 256; c++) {
            if (freq[c] == 0) continue;
            double p = (double)freq[c] / len;
            global_ent -= p * log2(p);
        }
        int bsz = len >= 512 ? len / 64 : 8;
        if (bsz < 4) bsz = 4;
        int n_blocks = len / bsz;
        if (n_blocks > 64) n_blocks = 64;
        if (n_blocks < 1) n_blocks = 1;
        for (int b = 0; b < n_blocks; b++) {
            int off = b * bsz;
            int blen = (off + bsz <= len) ? bsz : len - off;
            if (blen < 1) continue;
            int bf[256] = {0};
            for (int k = 0; k < blen; k++) bf[data[off + k]]++;
            double bent = 0.0;
            for (int c = 0; c < 256; c++) {
                if (bf[c] == 0) continue;
                double p = (double)bf[c] / blen;
                bent -= p * log2(p);
            }
            bs_set(bs, L4_OFF + b, bent > global_ent ? 1 : 0);
        }
    }

    /* L5: Text structure */
    {
        int n_alpha=0, n_digit=0, n_space=0, n_punct=0, n_upper=0, n_lower=0;
        for (int j = 0; j < len; j++) {
            if (isalpha(data[j])) n_alpha++;
            if (isdigit(data[j])) n_digit++;
            if (isspace(data[j])) n_space++;
            if (ispunct(data[j])) n_punct++;
            if (isupper(data[j])) n_upper++;
            if (islower(data[j])) n_lower++;
        }
        int classes[] = {n_alpha, n_digit, n_space, n_punct, n_upper, n_lower};
        for (int c = 0; c < 6; c++) {
            int on = len > 0 ? (classes[c] * 8) / len : 0;
            if (on > 8) on = 8;
            for (int b = 0; b < 8; b++)
                bs_set(bs, L5_OFF + c * 8 + b, b < on ? 1 : 0);
        }
        int words = 1;
        for (int j = 0; j < len; j++) if (isspace(data[j])) words++;
        int wlog = words > 0 ? (int)(log2(words)) : 0;
        if (wlog > 15) wlog = 15;
        for (int b = 0; b < 16; b++)
            bs_set(bs, L5_OFF + 48 + b, b <= wlog ? 1 : 0);
        int wl_hist[8] = {0}, wl = 0;
        for (int j = 0; j <= len; j++) {
            if (j == len || isspace(data[j])) {
                if (wl > 0) { int bkt = wl > 8 ? 7 : wl - 1; wl_hist[bkt]++; }
                wl = 0;
            } else wl++;
        }
        int wl_max = 1;
        for (int b = 0; b < 8; b++) if (wl_hist[b] > wl_max) wl_max = wl_hist[b];
        for (int b = 0; b < 8; b++) {
            int on = wl_hist[b] > 0 ? 1 + (wl_hist[b] * 7) / wl_max : 0;
            if (on > 8) on = 8;
            for (int bit = 0; bit < 8; bit++)
                bs_set(bs, L5_OFF + 64 + b * 8 + bit, bit < on ? 1 : 0);
        }
    }

    /* L6: Trigram bloom */
    for (int i = 0; i < len - 2; i++) {
        uint8_t tri[3] = { (uint8_t)tolower(data[i]), (uint8_t)tolower(data[i+1]), (uint8_t)tolower(data[i+2]) };
        bloom_set(bs, L6_OFF, L6_LEN, tri, 3, 2);
    }
}

int encode_file(BitStream *bs, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    int max = BS_MAXBITS / 8;
    if (sz > max) sz = max;
    uint8_t *buf = (uint8_t *)malloc(sz);
    size_t rd = fread(buf, 1, sz, f);
    fclose(f);
    encode_bytes(bs, buf, (int)rd);
    free(buf);
    return 0;
}

/* ══════════════════════════════════════════════════════════════
 * ONETWO CORE — bidirectional binary observation
 * ══════════════════════════════════════════════════════════════ */

void bs_runs(const BitStream *b, RunList *r) {
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

int ot_find_period(const BitStream *b) {
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

int ot_measure_symmetry(const BitStream *b) {
    if (b->len < 2) return 100;
    int half = b->len / 2, match = 0;
    for (int i = 0; i < half; i++)
        if (bs_get(b, i) == bs_get(b, half + i)) match++;
    return (match * 100) / half;
}

int ot_predict_sequence(const int *vals, int n, int *pattern_type) {
    *pattern_type = 0;
    if (n < 2) return 0;

    int constant = 1;
    for (int i = 1; i < n; i++)
        if (vals[i] != vals[0]) { constant = 0; break; }
    if (constant) { *pattern_type = 1; return vals[0]; }

    if (n < 3) return vals[n-1];

    int d0 = vals[1] - vals[0], linear = 1;
    for (int i = 2; i < n; i++)
        if (vals[i] - vals[i-1] != d0) { linear = 0; break; }
    if (linear) { *pattern_type = 2; return vals[n-1] + d0; }

    if (n < 4) return vals[n-1] + (vals[n-1] - vals[n-2]);

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

    int deltas[MAX_RUNS];
    for (int i = 1; i < n; i++) deltas[i-1] = vals[i] - vals[i-1];
    if (n > 3) {
        int a0 = deltas[1] - deltas[0], accel = 1;
        for (int i = 2; i < n-1; i++)
            if (deltas[i] - deltas[i-1] != a0) { accel = 0; break; }
        if (accel) {
            *pattern_type = 3;
            return vals[n-1] + deltas[n-2] + a0;
        }
    }
    return vals[n-1] + (vals[n-1] - vals[n-2]);
}

OneTwoResult ot_observe(const BitStream *b) {
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

    r.predicted_next_one = ot_predict_sequence(
        r.ones_runs, r.n_ones, &r.ones_pattern_type);
    r.predicted_next_zero = ot_predict_sequence(
        r.zeros_runs, r.n_zeros, &r.zeros_pattern_type);

    r.density = b->len > 0 ? (bs_popcount(b) * 100) / b->len : 0;
    r.symmetry = ot_measure_symmetry(b);
    r.period = ot_find_period(b);

    return r;
}

void ot_sys_init(OneTwoSystem *s) { memset(s, 0, sizeof(*s)); }

OneTwoResult ot_sys_ingest(OneTwoSystem *s, const uint8_t *data, int len) {
    BitStream stream;
    bs_init(&stream);
    for (int i = 0; i < len && stream.len < BS_MAXBITS; i++)
        for (int bit = 0; bit < 8 && stream.len < BS_MAXBITS; bit++)
            bs_push(&stream, (data[i] >> bit) & 1);
    s->total_inputs++;

    s->analysis = ot_observe(&stream);
    OneTwoResult *a = &s->analysis;

    int32_t density_val = a->density;
    int32_t symmetry_val = a->symmetry;
    int32_t period_val = a->period;
    int32_t pred_one = a->predicted_next_one;
    int32_t pred_zero = a->predicted_next_zero;

    int32_t structural_match = density_val + symmetry_val;
    int32_t structural_diverge = density_val - symmetry_val;
    int32_t pred_consensus = pred_one + pred_zero;
    int32_t pred_mismatch = pred_one - pred_zero;
    int32_t stability = period_val + density_val + 1;
    int32_t total_energy = density_val + symmetry_val + period_val + pred_one + pred_zero;
    int32_t learning_signal = s->feedback[0] + pred_consensus;
    int32_t error = structural_match - s->prev_match;

    s->feedback[0] = structural_match;
    s->feedback[1] = structural_diverge;
    s->feedback[2] = pred_consensus;
    s->feedback[3] = pred_mismatch;
    s->feedback[4] = stability > 0 ? 1 : 0;
    s->feedback[5] = total_energy;
    s->feedback[6] = learning_signal;
    s->feedback[7] = error;

    s->prev_match = structural_match;
    s->tick_count++;
    return s->analysis;
}

/* ══════════════════════════════════════════════════════════════
 * CRYSTAL HISTOGRAM
 * ══════════════════════════════════════════════════════════════ */

void crystal_update(Node *n, Edge *edges, int n_edges, int node_id) {
    memset(n->crystal_hist, 0, 8);
    n->crystal_n = 0;
    for (int e = 0; e < n_edges; e++) {
        if (edges[e].dst == (uint16_t)node_id && edges[e].weight > 0) {
            int bin = edges[e].weight / 32;
            if (bin > 7) bin = 7;
            if (n->crystal_hist[bin] < 255) n->crystal_hist[bin]++;
            n->crystal_n++;
        }
    }
}

int crystal_strength(const Node *n) {
    if (n->crystal_n == 0) return 0;
    int total = 0;
    for (int b = 0; b < 8; b++) total += n->crystal_hist[b] * (b * 32 + 16);
    return total / n->crystal_n;
}

int crystal_distance(const Node *a, const Node *b) {
    int dist = 0;
    for (int i = 0; i < 8; i++) dist += abs((int)a->crystal_hist[i] - (int)b->crystal_hist[i]);
    return dist;
}

/* ══════════════════════════════════════════════════════════════
 * GRAPH OPERATIONS
 * ══════════════════════════════════════════════════════════════ */

void graph_init(Graph *g) {
    memset(g, 0, sizeof(*g));
    g->nodes = (Node *)calloc(MAX_NODES, sizeof(Node));
    g->edges = (Edge *)calloc(MAX_EDGES, sizeof(Edge));
    for (int i = 0; i < MAX_NODES; i++) g->nodes[i].child_id = -1;
    g->grow_threshold = 65;
    g->prune_threshold = PRUNE_FLOOR;
    g->learn_strengthen = 65;
    g->learn_weaken = 40;
    g->learn_rate = 15;
    g->auto_grow = 1;
    g->auto_prune = 1;
    g->auto_learn = 1;
    g->grow_mean = 0;
    g->grow_interval = 10;
    g->prune_interval = 20;
    g->retina = NULL;
    g->retina_len = 0;
}

void graph_destroy(Graph *g) {
    free(g->nodes); free(g->edges);
    free(g->z_edge_idx); free(g->z_node_idx);
    g->nodes = NULL; g->edges = NULL;
    g->z_edge_idx = NULL; g->z_node_idx = NULL;
}

int graph_find(Graph *g, const char *name) {
    for (int i = 0; i < g->n_nodes; i++)
        if (g->nodes[i].alive && strcmp(g->nodes[i].name, name) == 0) return i;
    return -1;
}

int graph_add(Graph *g, const char *name, uint8_t shell_id, const SubstrateT *T) {
    int id = graph_find(g, name);
    if (id >= 0) return id;
    if (g->n_nodes >= MAX_NODES) return -1;
    id = g->n_nodes++;
    Node *n = &g->nodes[id];
    memset(n, 0, sizeof(*n));
    n->alive = 1;
    n->layer_zero = 1;
    n->shell_id = shell_id;
    n->child_id = -1;
    n->plasticity = PLASTICITY_DEFAULT;
    /* Hash name into 3D coords — hash chaining for independent axes */
    uint32_t hx = hash32((const uint8_t *)name, (int)strlen(name));
    uint32_t hy = hash32((const uint8_t *)&hx, sizeof(hx));
    uint32_t hz = hash32((const uint8_t *)&hy, sizeof(hy));
    int vx = hx % 64;
    int vy = hy % 64;
    int vz = hz % 64;
    n->coord = coord_pack(vx, vy, vz);
    n->Z = (shell_id < 3) ? SHELL_Z[shell_id] : 1.0;
    strncpy(n->name, name, NAME_LEN - 1);
    n->created = n->last_active = (uint32_t)T_now(T);
    return id;
}

int graph_find_edge(Graph *g, int a, int b, int d) {
    for (int i = 0; i < g->n_edges; i++)
        if (g->edges[i].src_a == a && g->edges[i].src_b == b && g->edges[i].dst == d)
            return i;
    return -1;
}

/* Conservation: enforce MAX_NODE_WEIGHT budget on all edges touching node_id.
 * If total weight exceeds budget, scale all touching edges proportionally.
 * New edges steal energy from old edges — scarcity drives competition. */
static void node_enforce_conservation(Graph *g, int node_id) {
    int total = 0;
    for (int i = 0; i < g->n_edges; i++) {
        Edge *ed = &g->edges[i];
        if (ed->weight == 0) continue;
        if ((int)ed->src_a == node_id || (int)ed->src_b == node_id ||
            (int)ed->dst == node_id)
            total += ed->weight;
    }
    if (total <= (int)MAX_NODE_WEIGHT) return;

    /* Scale all touching edges down proportionally */
    for (int i = 0; i < g->n_edges; i++) {
        Edge *ed = &g->edges[i];
        if (ed->weight == 0) continue;
        if ((int)ed->src_a == node_id || (int)ed->src_b == node_id ||
            (int)ed->dst == node_id) {
            int scaled = (int)ed->weight * (int)MAX_NODE_WEIGHT / total;
            uint8_t new_w = (uint8_t)(scaled < 1 ? 1 : scaled);
            tline_init_from_weight(&ed->tl, new_w);
            ed->weight = tline_weight(&ed->tl);
        }
    }
}

int graph_wire(Graph *g, int a, int b, int d, uint8_t w, int inter) {
    if (g->n_edges >= MAX_EDGES) return -1;
    if (graph_find_edge(g, a, b, d) >= 0) return -1;
    int id = g->n_edges++;
    Edge *e = &g->edges[id];
    memset(e, 0, sizeof(*e));
    e->src_a = (uint16_t)a;
    e->src_b = (uint16_t)b;
    e->dst = (uint16_t)d;
    e->intershell = inter ? 1 : 0;
    e->learn_rate = (uint8_t)g->learn_rate;
    tline_init_from_weight(&e->tl, w);
    if (inter) {
        tline_set_impedance(&e->tl, g->nodes[a].Z, g->nodes[d].Z);
    }
    e->weight = tline_weight(&e->tl);

    /* Enforce energy conservation on all participating nodes */
    node_enforce_conservation(g, a);
    if (b != a) node_enforce_conservation(g, b);
    if (d != a && d != b) node_enforce_conservation(g, d);

    return id;
}

/* Set invert flags on an edge based on negation asymmetry.
 * Only fires when exactly one of src_a/src_b is negated (XOR).
 * Double negation = agreement, so both negated → no invert. */
void edge_set_negation_invert(Graph *g, int edge_id) {
    if (edge_id < 0 || edge_id >= g->n_edges) return;
    Edge *e = &g->edges[edge_id];
    Node *na = &g->nodes[e->src_a];
    Node *nb = &g->nodes[e->src_b];
    if (na->has_negation != nb->has_negation) {
        /* Asymmetric: exactly one is negated */
        if (na->has_negation) e->invert_a = 1;
        if (nb->has_negation) e->invert_b = 1;
        /* Contradiction detected: mark and crack the non-negated node.
         * The contradicted node loses crystallization and gets flagged so
         * all valence++ paths skip it. Without this flag, three separate
         * paths (resolve, feedback, boredom) re-crystallize within ticks. */
        Node *target = na->has_negation ? nb : na;
        target->contradicted = 1;
        if (target->valence > 0) {
            target->valence /= 2;
            target->coherent = -1;
        }
    }
}

/* ══════════════════════════════════════════════════════════
 * PREDICT POLARITY — query cross-wiring bridge (step 4b)
 *
 * Walk edges of node_id, find connections to sense nodes.
 * Sum weights to burst features on pass 4 (s:B*.4).
 * High ratio → node is associated with contradiction/inversion.
 * Returns 1 if predicted inverted, 0 otherwise.
 * ══════════════════════════════════════════════════════════ */

/* Pure observer: reads graph state directly, never writes topology.
 * Counts inverted edges touching THIS node — direct measurement of
 * how much contradiction this specific node participates in. */
int engine_predict_polarity(Engine *eng, int node_id, const char *label) {
    Graph *g = &eng->shells[0].g;
    if (node_id < 0 || node_id >= g->n_nodes || !g->nodes[node_id].alive)
        return 0;

    /* Count inverted edges that involve this node (per-node, not global) */
    int my_inverts = 0, my_total = 0;
    for (int e = 0; e < g->n_edges; e++) {
        Edge *ed = &g->edges[e];
        if (ed->weight == 0) continue;
        int sa = (int)ed->src_a, sb = (int)ed->src_b, d = (int)ed->dst;
        if (sa != node_id && sb != node_id && d != node_id) continue;
        my_total++;
        if (ed->invert_a || ed->invert_b)
            my_inverts++;
    }

    float ratio = (my_total > 0) ? (float)my_inverts / (float)my_total : 0.0f;
    int predict = (my_inverts > 0 && ratio > 0.5f) ? 1 : 0;

    printf("  [predict] node[%d] '%s': inv_edges=%d/%d ratio=%.3f%s\n",
           node_id, label ? label : "?",
           my_inverts, my_total, ratio,
           predict ? " → INVERT" : "");

    return predict;
}

void engine_polarity_summary(const Engine *eng) {
    printf("[polarity summary] bridge: %d/%d invert  kw: %d/%d invert\n",
           eng->pol_bridge_invert, eng->pol_bridge_total,
           eng->pol_kw_invert, eng->pol_kw_total);
}

void engine_polarity_reset(Engine *eng) {
    eng->pol_bridge_total = eng->pol_bridge_invert = 0;
    eng->pol_kw_total = eng->pol_kw_invert = 0;
}

int graph_learn(Graph *g, int32_t structural_match) {
    int changed = 0;
    /* ONETWO feedback gating: structural_match controls learning rate.
     * Baseline 100 = normal (1.0x), <30 = near freeze (0.1-0.3x), >150 = accelerated (1.5-2.0x).
     * This is the thermodynamic clutch: low match = slipping (don't lock bad topology),
     * high match = engaged (rapidly crystallize good configuration). */
    double match_gate = (double)structural_match / 100.0;
    if (match_gate < 0.1) match_gate = 0.1;
    if (match_gate > 2.0) match_gate = 2.0;

    for (int i = 0; i < g->n_edges; i++) {
        Edge *e = &g->edges[i];
        if (e->tl.n_cells == 0) continue;
        int n = g->nodes[e->src_a].identity.len < g->nodes[e->src_b].identity.len
              ? g->nodes[e->src_a].identity.len : g->nodes[e->src_b].identity.len;
        if (n < 20) continue;
        int corr = bs_contain(&g->nodes[e->src_a].identity, &g->nodes[e->src_b].identity);
        /* Target Lc: high correlation → low Lc (fast). Low correlation → high Lc (slow). */
        double target_lc = e->tl.L_base * (200.0 - (double)corr) / 100.0;
        if (target_lc < 0.1) target_lc = 0.1;
        if (target_lc > 10.0) target_lc = 10.0;
        /* Bidirectional plasticity: hottest participant drives learning rate.
         * Feed-forward edges (src_a==src_b) put zone nodes as sources — without
         * this, hot emitters pump signal with no thermodynamic return. */
        double rate = (double)MISMATCH_TAX_NUM / (double)MISMATCH_TAX_DEN;
        double plast_dst = (double)g->nodes[e->dst].plasticity;
        double plast_src = (double)g->nodes[e->src_a].plasticity;
        /* Product, not max: coupled thermodynamic state of both endpoints.
         * Hot×Hot (1.5×1.5=2.25) vs Cold×Cold (0.8×0.8=0.64) = 3.5x spread.
         * max() only gave 1% difference — a low-pass filter on temperature. */
        double plast = plast_dst * plast_src;
        if (plast < (double)PLASTICITY_MIN) plast = (double)PLASTICITY_MIN;
        /* Apply ONETWO gating multiplier: plasticity × structural match. */
        rate *= plast * match_gate;
        for (int c = 0; c < e->tl.n_cells; c++) {
            double error = target_lc - e->tl.Lc[c];
            e->tl.Lc[c] += error * rate;
        }
        e->weight = tline_weight(&e->tl);
        changed++;
    }
    for (int i = 0; i < g->n_nodes; i++)
        if (g->nodes[i].alive) crystal_update(&g->nodes[i], g->edges, g->n_edges, i);
    g->total_learns += changed;
    return changed;
}

/* TYXZT Coordinate Mapping: Position IS Meaning
 * Y = Sequential distinction (topological depth along path)
 * X = Parallel distinction (independent signal lane)
 * Z = Abstraction level (shell nesting depth passed as arg) 
 */
int graph_compute_topology(Graph *g, int z_depth) {
    if (g->n_nodes == 0) return 0;
    
    int y_level[MAX_NODES] = {0};
    int x_lane[MAX_NODES];
    for (int i = 0; i < MAX_NODES; i++) x_lane[i] = -1;

    int current_lane = 0;
    int max_y = 0;

    /* 1. Identify roots (injection points with 0 incoming active edges) */
    for (int i = 0; i < g->n_nodes; i++) {
        if (!g->nodes[i].alive) continue;
        int n_in = 0;
        for (int e = 0; e < g->n_edges; e++) {
            if (g->edges[e].dst == i && g->edges[e].weight > 0 && g->nodes[g->edges[e].src_a].alive) {
                n_in++;
            }
        }
        if (n_in == 0) {
            x_lane[i] = current_lane++;
            y_level[i] = 0; /* Sequence starts at 0 */
        }
    }

    /* 2. Iterative relaxation to propagate Y (sequence) and X (lane) */
    int changed = 1;
    int iters = 0;
    while (changed && iters < g->n_nodes) {
        changed = 0;
        iters++;
        for (int e = 0; e < g->n_edges; e++) {
            Edge *ed = &g->edges[e];
            if (ed->weight == 0) continue;
            int s = ed->src_a;
            int d = ed->dst;
            if (!g->nodes[s].alive || !g->nodes[d].alive) continue;
            if (s == d) continue; /* skip self-loops */

            /* Propagate Sequence (Y): Max depth along the path */
            if (y_level[d] < y_level[s] + 1) {
                y_level[d] = y_level[s] + 1;
                changed = 1;
            }

            /* Propagate Parallel Lane (X): Inherit lane from source */
            if (x_lane[s] != -1 && x_lane[d] == -1) {
                x_lane[d] = x_lane[s];
                changed = 1;
            }
        }
    }

    /* 3. Handle isolated cycles (nodes with no connection to roots) */
    for (int i = 0; i < g->n_nodes; i++) {
        if (g->nodes[i].alive && x_lane[i] == -1) {
            x_lane[i] = current_lane++;
        }
    }

    /* 4. Pack the strictly structural coordinates */
    for (int i = 0; i < g->n_nodes; i++) {
        if (g->nodes[i].alive) {
            uint32_t x = (uint32_t)(x_lane[i] & 0x3FF);
            uint32_t y = (uint32_t)(y_level[i] & 0x3FF);
            uint32_t z = (uint32_t)(z_depth & 0x3FF);
            g->nodes[i].coord = coord_pack(x, y, z);
            if (y_level[i] > max_y) max_y = y_level[i];
        }
    }
    
    return max_y;
}

int graph_zone_coherence(const Graph *g, const int *node_ids, int n_ids, int *n_alive) {
    int alive = 0, incoh = 0;
    for (int k = 0; k < n_ids; k++) {
        int i = node_ids[k];
        if (i < 0 || i >= g->n_nodes) continue;
        const Node *n = &g->nodes[i];
        if (!n->alive || n->layer_zero) continue;
        alive++;
        if (n->coherent < 0) incoh++;
    }
    if (n_alive) *n_alive = alive;
    return incoh;
}

/* ══════════════════════════════════════════════════════════════
 * NESTING — systems containing systems
 * ══════════════════════════════════════════════════════════════ */

int child_tick_once(Graph *g) {
    /* ══════════════════════════════════════════════════════════════
     * NEW: Child Hebbian Learning (Retinal Co-activation)
     * Direct wiring between co-active retina nodes
     * ══════════════════════════════════════════════════════════════ */
    int new_edges_grown = 0;
    
    for (int i = 0; i < g->n_nodes; i++) {
        Node *a = &g->nodes[i];
        if (!a->alive || a->val < 128) continue;

        for (int j = i + 1; j < g->n_nodes; j++) {
            Node *b = &g->nodes[j];
            if (!b->alive || b->val < 128) continue;

            /* Find a hidden node as destination (same logic as grow block).
             * Retina nodes (0-7) are inputs — never wire TO them.
             * Hidden nodes are 8..out-1, output is last. */
            int out = g->n_nodes - 1;
            int dst = -1;
            for (int h = 8; h < out; h++) {
                if (h == i || h == j) continue;
                if (graph_find_edge(g, i, j, h) < 0) { dst = h; break; }
            }
            if (dst < 0 && graph_find_edge(g, i, j, out) < 0)
                dst = out;

            if (dst >= 0) {
                int e_idx = graph_find_edge(g, i, j, dst);
                if (e_idx >= 0) {
                    /* Edge exists to this dst: strengthen */
                    int nw = g->edges[e_idx].weight + 2;
                    g->edges[e_idx].weight = (nw > 255) ? 255 : (uint8_t)nw;
                    if (a->valence < 255) a->valence++;
                    if (b->valence < 255) b->valence++;
                } else if (new_edges_grown < 10) {
                    graph_wire(g, i, j, dst, 16, 0);
                    new_edges_grown++;
                }
            }
        }
    }

    /* ══════════════════════════════════════════════════════════════
     * Original TLine propagation
     * ══════════════════════════════════════════════════════════════ */
    int changed = 0;
    g->total_ticks++;
    for (int i = 0; i < g->n_edges; i++) {
        Edge *e = &g->edges[i];
        if (e->tl.n_cells == 0 || e->weight == 0) continue;
        Node *na = &g->nodes[e->src_a], *nb = &g->nodes[e->src_b], *nd = &g->nodes[e->dst];
        if (!na->alive || na->layer_zero || na->identity.len < 1) continue;
        if (!nb->alive || nb->layer_zero || nb->identity.len < 1) continue;
        int32_t va = e->invert_a ? -na->val : na->val;
        int32_t vb = e->invert_b ? -nb->val : nb->val;
        double input = (double)(va + vb) / (double)VAL_CEILING;
        tline_inject(&e->tl, input);
        tline_step(&e->tl);
        double output = tline_read(&e->tl);
        if (fabs(output) > 1e-10) {
            int64_t out_scaled = (int64_t)(output * VAL_CEILING);
            if (out_scaled > INT32_MAX/2) out_scaled = INT32_MAX/2;
            if (out_scaled < INT32_MIN/2) out_scaled = INT32_MIN/2;
            nd->accum += out_scaled;
            nd->I_energy += (int32_t)(fabs(output) * VAL_CEILING);
            nd->n_incoming++;
        }
    }
    for (int i = 0; i < g->n_nodes; i++) {
        Node *n = &g->nodes[i];
        if (!n->alive || n->layer_zero) continue;
        if (n->n_incoming == 0 && n->accum == 0) continue;
        int32_t old_val = n->val;
        if (n->n_incoming <= 1) {
            n->val = (int32_t)n->accum;
        } else {
            int64_t total = n->accum + (int64_t)n->val;
            int N = n->n_incoming + 1;
            int64_t resolved = 2 * total / N - old_val;
            if (resolved > INT32_MAX/2) resolved = INT32_MAX/2;
            if (resolved < INT32_MIN/2) resolved = INT32_MIN/2;
            n->val = (int32_t)resolved;
        }
        if (n->valence > 0) {
            n->val = (int32_t)((int64_t)old_val * n->valence +
                               (int64_t)n->val * (255 - n->valence)) / 255;
        }
        int64_t cap64 = (int64_t)abs(old_val) + llabs(n->accum);
        if (cap64 > INT32_MAX/2) cap64 = INT32_MAX/2;
        int32_t cap = (int32_t)cap64;
        if (abs(n->val) > cap) n->val = n->val > 0 ? cap : -cap;
        if (n->val > VAL_CEILING) n->val = VAL_CEILING;
        if (n->val < -VAL_CEILING) n->val = -VAL_CEILING;
        /* I_energy: collision remainder after voltage resolution */
        if (n->n_incoming >= 2) {
            int32_t val_energy = abs(n->val);
            n->I_energy = (n->I_energy > val_energy) ?
                n->I_energy - val_energy : 0;
        } else {
            n->I_energy = 0;
        }
        if (n->val != old_val) changed = 1;
        n->n_incoming = 0;
        n->accum = 0;
    }

    /* ── Hebbian: co-active retina nodes strengthen, inactive weaken ── */
    for (int i = 0; i < g->n_edges; i++) {
        Edge *e = &g->edges[i];
        if (e->weight == 0) continue;
        Node *na = &g->nodes[e->src_a];
        Node *nb = &g->nodes[e->src_b];
        if (!na->alive || !nb->alive) continue;
        /* Both sources active (nonzero val) → strengthen */
        if (na->val != 0 && nb->val != 0) {
            tline_strengthen(&e->tl, 0.01);
            e->weight = tline_weight(&e->tl);
            g->total_learns++;
        }
        /* One active, one silent → weaken */
        else if ((na->val != 0) != (nb->val != 0)) {
            tline_weaken(&e->tl, 0.01);
            e->weight = tline_weight(&e->tl);
        }
    }

    /* ── Prune: remove edges conservation and Hebbian starved ──
     * Same pattern as parent S6. Conservation crushes overloaded edges
     * to weight 1-3. Hebbian weakens inactive edges. Without prune,
     * dead edges persist forever (343 on 13 nodes = permanent hairball).
     * No arithmetic filter needed — topology already did the sorting. */
    {
        int w = 0;
        for (int e = 0; e < g->n_edges; e++) {
            if (g->edges[e].weight >= (uint8_t)PRUNE_FLOOR) {
                if (w != e) g->edges[w] = g->edges[e];
                w++;
            } else {
                g->total_pruned++;
            }
        }
        g->n_edges = w;
    }

    /* ── Grow: wire co-active pairs to downstream nodes ── */
    if (g->grow_interval > 0 && (g->total_ticks % (unsigned)g->grow_interval == 0)) {
        int out = g->n_nodes - 1;  /* output node is always last */
        for (int i = 0; i < g->n_nodes; i++) {
            if (!g->nodes[i].alive || g->nodes[i].val == 0) continue;
            for (int j = i + 1; j < g->n_nodes; j++) {
                if (!g->nodes[j].alive || g->nodes[j].val == 0) continue;
                /* Find a downstream target: prefer hidden nodes (8..out-1),
                 * fall back to output. Don't wire to retina (0-7). */
                int dst = -1;
                for (int h = 8; h < out; h++) {
                    if (h == i || h == j) continue;
                    if (graph_find_edge(g, i, j, h) < 0) { dst = h; break; }
                }
                if (dst < 0 && graph_find_edge(g, i, j, out) < 0)
                    dst = out;
                if (dst >= 0) {
                    graph_wire(g, i, j, dst, 32, 0);
                    g->total_grown++;
                }
            }
        }
    }

    /* ── Inner T: local heartbeat at SUBSTRATE_INT/4 interval ── */
    if (g->total_ticks > 0 && g->total_ticks % (SUBSTRATE_INT / 4) == 0) {
        int out_idx = g->n_nodes - 1;
        if (out_idx >= 0 && g->nodes[out_idx].alive) {
            int32_t output_val = g->nodes[out_idx].val;
            int32_t delta = output_val - g->prev_output;
            int32_t abs_delta = delta < 0 ? -delta : delta;
            g->prev_output = output_val;

            /* SPRT-style accumulation — not single-tick threshold */
            g->error_accum += abs_delta;
            /* Decay: old evidence fades. Half-life ~5 heartbeats. */
            g->error_accum = g->error_accum * 7 / 8;

            /* Classify drive state */
            int32_t frust_thresh = (int32_t)(SUBSTRATE_INT / 8);
            int32_t bore_thresh  = (int32_t)(MISMATCH_TAX_NUM / 2);

            if (g->error_accum > frust_thresh) {
                g->drive = 1;  /* frustration — grow faster */
                g->grow_interval = g->grow_interval * 2 / 3;
                if (g->grow_interval < 2) g->grow_interval = 2;

                /* Fractal thermodynamics: heat and cleave child nodes */
                for (int i = 0; i < g->n_nodes; i++) {
                    Node *nh = &g->nodes[i];
                    if (!nh->alive) continue;

                    nh->plasticity += PLASTICITY_HEAT;

                    /* Phase transition: structural cleaving in child */
                    if (nh->plasticity > PLASTICITY_MAX) {
                        int worst_edge = -1;
                        double max_lc = -1.0;
                        int incoming_count = 0;

                        /* Find most resistive incoming edge & count total */
                        for (int e = 0; e < g->n_edges; e++) {
                            Edge *ed = &g->edges[e];
                            if (ed->dst == (uint16_t)i && ed->weight > 0 && ed->tl.n_cells > 0) {
                                incoming_count++;
                                if (ed->tl.Lc[0] > max_lc) {
                                    max_lc = ed->tl.Lc[0];
                                    worst_edge = e;
                                }
                            }
                        }

                        /* Sever only if node has a backup path (survival floor) */
                        if (worst_edge >= 0 && incoming_count > 1) {
                            g->edges[worst_edge].weight = 0;
                            g->edges[worst_edge].tl.n_cells = 0;
                            nh->plasticity = PLASTICITY_DEFAULT;  /* consume heat */
                        } else {
                            nh->plasticity = PLASTICITY_MAX;  /* cap — can't cleave */
                        }
                    }
                }
            } else if (g->error_accum < bore_thresh) {
                g->drive = 2;  /* boredom — crystallize */

                /* Cool child nodes */
                for (int i = 0; i < g->n_nodes; i++) {
                    Node *nc = &g->nodes[i];
                    if (nc->alive && nc->plasticity > PLASTICITY_MIN) {
                        nc->plasticity -= PLASTICITY_COOL;
                        if (nc->plasticity < PLASTICITY_MIN) nc->plasticity = PLASTICITY_MIN;
                    }
                }

                /* Strengthen surviving edges scaled by plasticity */
                for (int e = 0; e < g->n_edges; e++) {
                    if (g->edges[e].weight > 0 && g->edges[e].tl.n_cells > 0) {
                        double plast = (double)g->nodes[g->edges[e].dst].plasticity;
                        if (plast < (double)PLASTICITY_MIN) plast = (double)PLASTICITY_MIN;
                        tline_strengthen(&g->edges[e].tl, 0.01 * plast);
                        g->edges[e].weight = tline_weight(&g->edges[e].tl);
                    }
                }
                g->grow_interval = g->grow_interval * 3 / 2;
                if (g->grow_interval > 50) g->grow_interval = 50;
            } else {
                g->drive = 0;  /* curiosity — do nothing */
            }
            g->local_heartbeat++;
        }
    }

    return changed;
}

static int nest_spawn(Engine *eng, int node_id) {
    if (eng->n_children >= MAX_CHILDREN) return -1;
    Graph *g0 = &eng->shells[0].g;
    if (node_id < 0 || node_id >= g0->n_nodes) return -1;
    Node *owner = &g0->nodes[node_id];
    if (owner->child_id >= 0) return owner->child_id;
    int slot = -1;
    for (int i = 0; i < MAX_CHILDREN; i++)
        if (eng->child_owner[i] < 0) { slot = i; break; }
    if (slot < 0) return -1;
    graph_init(&eng->child_pool[slot]);
    Graph *child = &eng->child_pool[slot];

    /* 8 retina input nodes (one per octant of parent's 4^3 cube).
     * Each retina node reads spatial substrate from its octant.
     * Replaces scalar int32_t passthrough — children see parent's
     * spatial structure directly via zero-copy retina pointer. */
    char rname[32];
    for (int r = 0; r < 8; r++) {
        snprintf(rname, 32, "retina_%d", r);
        int rid = graph_add(child, rname, 0, &eng->T);
        if (rid >= 0) {
            child->nodes[rid].alive = 1;
            child->nodes[rid].layer_zero = 0;  /* NOT source — receives from retina */
            child->nodes[rid].identity.len = 64;
            child->nodes[rid].Z = owner->Z;
            child->nodes[rid].plasticity = PLASTICITY_DEFAULT;
        }
    }
    /* 4 hidden nodes — intermediate processing layer.
     * Without these, all edges target output directly (star topology, depth=1).
     * Hidden nodes allow retina → hidden → hidden → output chains (depth>1). */
    for (int h = 0; h < 4; h++) {
        char hname[32];
        snprintf(hname, 32, "hidden_%d", h);
        int hid = graph_add(child, hname, 0, &eng->T);
        if (hid >= 0) {
            child->nodes[hid].alive = 1;
            child->nodes[hid].layer_zero = 0;
            child->nodes[hid].identity.len = 64;
            child->nodes[hid].Z = owner->Z;
            child->nodes[hid].plasticity = PLASTICITY_DEFAULT;
        }
    }
    int out = graph_add(child, "output", 0, &eng->T);
    if (out >= 0) {
        child->nodes[out].alive = 1;
        child->nodes[out].layer_zero = 0;
        child->nodes[out].identity.len = 64;
        child->nodes[out].Z = owner->Z;
        child->nodes[out].plasticity = PLASTICITY_DEFAULT;
    }

    /* Wire retina pairs to hidden nodes, not directly to output.
     * (r0,r1)→hidden_0, (r2,r3)→hidden_1, (r4,r5)→hidden_2, (r6,r7)→hidden_3.
     * Hidden nodes are at indices 8,9,10,11. Output is at index 12. */
    for (int r = 0; r < 8; r += 2) {
        int hidden_target = 8 + r / 2;
        graph_wire(child, r, r + 1, hidden_target, 128, 0);
    }
    /* Wire hidden nodes to output: each hidden feeds forward */
    {
        int out_idx = child->n_nodes - 1;  /* output is last node = 12 */
        for (int h = 0; h < 4; h++) {
            int h1 = 8 + h;
            int h2 = 8 + ((h + 1) % 4);
            graph_wire(child, h1, h2, out_idx, 64, 0);
        }
    }

    eng->child_owner[slot] = node_id;
    eng->n_children++;
    owner->child_id = (int8_t)slot;
    return slot;
}

void nest_remove(Engine *eng, int node_id) {
    Graph *g0 = &eng->shells[0].g;
    if (node_id < 0 || node_id >= g0->n_nodes) return;
    Node *owner = &g0->nodes[node_id];
    if (owner->child_id < 0) return;
    int slot = owner->child_id;
    graph_destroy(&eng->child_pool[slot]);
    eng->child_owner[slot] = -1;
    eng->n_children--;
    owner->child_id = -1;
}

/* nest_check: shell 0 only. Shell 1 mirrors would create duplicates.
 * Shell 2 re-spawn after shell 0 lysis is a future design decision. */
static void nest_check(Engine *eng) {
    if (eng->n_children >= MAX_CHILDREN) return;
    Graph *g0 = &eng->shells[0].g;
    for (int i = 0; i < g0->n_nodes && eng->n_children < MAX_CHILDREN; i++) {
        Node *n = &g0->nodes[i];
        if (!n->alive || n->layer_zero || n->child_id >= 0) continue;
        if (n->valence >= 200) {
            int slot = nest_spawn(eng, i);
            if (slot >= 0)
                printf("  [nest] node[%d] '%s' crystallized -> child[%d]\n",
                       i, n->name, slot);
        }
    }
}

static int adaptive_interval(int current, int activity, int min_val, int max_val) {
    int next;
    if (activity > 0)
        next = current * 2 / 3;
    else
        next = current * 3 / 2;
    if (next < min_val) next = min_val;
    if (next > max_val) next = max_val;
    return next;
}

/* ══════════════════════════════════════════════════════════════
 * ENGINE LIFECYCLE
 * ══════════════════════════════════════════════════════════════ */

void engine_init(Engine *eng) {
    memset(eng, 0, sizeof(*eng));
    T_init(&eng->T);
    graph_init(&eng->shells[0].g); eng->shells[0].id = 0; strncpy(eng->shells[0].name, "carbon", 31);
    graph_init(&eng->shells[1].g); eng->shells[1].id = 1; strncpy(eng->shells[1].name, "silicon", 31);
    graph_init(&eng->shells[2].g); eng->shells[2].id = 2; strncpy(eng->shells[2].name, "verifier", 31);
    eng->n_shells = 3;
    eng->learn_interval = SUBSTRATE_INT;
    eng->boundary_interval = 50;
    eng->max_boundary_edges = 16384;
    eng->boundary_edges = (Edge *)calloc(eng->max_boundary_edges, sizeof(Edge));
    ot_sys_init(&eng->onetwo);
    eng->n_children = 0;
    for (int i = 0; i < MAX_CHILDREN; i++) {
        memset(&eng->child_pool[i], 0, sizeof(Graph));
        eng->child_owner[i] = -1;
    }
}

void engine_destroy(Engine *eng) {
    for (int i = 0; i < eng->n_shells; i++) graph_destroy(&eng->shells[i].g);
    for (int i = 0; i < MAX_CHILDREN; i++)
        if (eng->child_owner[i] >= 0) graph_destroy(&eng->child_pool[i]);
    free(eng->boundary_edges);
}

/* ONETWO-driven shell routing */
static void engine_analyze(Engine *eng, const BitStream *data,
                           const OneTwoResult *otr,
                           int *recommended_shell, int *temp_grow) {
    if (data->len < 16) { *recommended_shell = 1; *temp_grow = 68; return; }
    Graph *g0 = &eng->shells[0].g;
    int max_corr = 0;
    for (int i = 0; i < g0->n_nodes; i++) {
        if (g0->nodes[i].alive && g0->nodes[i].identity.len >= 16) {
            int c = bs_corr(data, &g0->nodes[i].identity);
            if (c > max_corr) max_corr = c;
        }
    }
    int novelty = 100 - max_corr;
    int is_structured = (otr->period > 0 || otr->symmetry > 50) ? 1 : 0;
    if (novelty > 60) {
        *recommended_shell = 2; *temp_grow = 53;
    } else if (otr->density > 70 && is_structured) {
        *recommended_shell = 0; *temp_grow = 36;
    } else {
        *recommended_shell = 1; *temp_grow = 45;
    }
}

int engine_ingest(Engine *eng, const char *name, const BitStream *data) {
    Graph *g0 = &eng->shells[0].g, *g1 = &eng->shells[1].g;

    OneTwoResult otr = ot_observe(data);
    int32_t onetwo_val = otr.density + otr.symmetry + otr.period
                       + otr.predicted_next_one + otr.predicted_next_zero;

    int recommended_shell = 1, temp_grow = 65;
    engine_analyze(eng, data, &otr, &recommended_shell, &temp_grow);

    int id0 = graph_add(g0, name, 0, &eng->T);
    if (id0 < 0) return -1;
    g0->nodes[id0].identity = *data;
    g0->nodes[id0].hit_count++;
    g0->nodes[id0].last_active = (uint32_t)T_now(&eng->T);
    g0->nodes[id0].val = onetwo_val;

    /* 3-tier semantic coordinates: 4-byte windows → X=type, Y=sub-type, Z=instance.
     * Bytes 0-3 encode the category ("  Da", "  So", "Even", "for ", "#inc").
     * Bytes 4-7 encode the sub-category ("te: ", "urce", "t[0]", "(int").
     * Bytes 8-11 encode the specific instance ("2025", "Even", "i = ").
     * 100% type clustering across log, HTTP, JSON, CSV, and code formats.
     * No training. No embedding model. Byte alignment IS the semantic structure. */
    if (data->len >= 32) {  /* at least 4 bytes */
        uint8_t b0[4] = {0}, b1[4] = {0}, b2[4] = {0};
        for (int i = 0; i < 4; i++) {
            for (int b = 0; b < 8; b++) {
                if (i * 8 + b < data->len && bs_get(data, i * 8 + b))
                    b0[i] |= (1 << b);
                if ((i + 4) * 8 + b < data->len && bs_get(data, (i + 4) * 8 + b))
                    b1[i] |= (1 << b);
                if ((i + 8) * 8 + b < data->len && bs_get(data, (i + 8) * 8 + b))
                    b2[i] |= (1 << b);
            }
        }
        uint32_t sx = hash32(b0, 4) % 64;
        uint32_t sy = hash32(b1, 4) % 64;
        uint32_t sz = hash32(b2, 4) % 64;
        g0->nodes[id0].coord = coord_pack(sx, sy, sz);
        g0->z_cache_n_nodes = -1;
    }

    /* Auto-wire: top-K by mutual containment.
     * Spatial locality: only check nodes at same X coordinate (same type prefix).
     * O(n/k) instead of O(n) where k = number of type clusters. */
    if (g0->auto_grow) {
        int thresh = temp_grow;
        int new_x = coord_x(g0->nodes[id0].coord) % 64;
        int top_j[GROW_K], top_c[GROW_K], n_top = 0;
        for (int i = 0; i < g0->n_nodes; i++) {
            if (i == id0 || !g0->nodes[i].alive || g0->nodes[i].identity.len < 16) continue;
            /* Spatial filter: same X-plane (same type prefix) or adjacent */
            int ix = coord_x(g0->nodes[i].coord) % 64;
            int dx = ix - new_x;
            if (dx < 0) dx = -dx;
            if (dx > 2 && dx < 62) continue;  /* allow wrap-around adjacency */
            int corr = bs_contain(data, &g0->nodes[i].identity);
            if (corr <= thresh) continue;
            int rev_corr = bs_contain(&g0->nodes[i].identity, data);
            if (rev_corr > corr + Z_ASYM_MARGIN) continue;
            if (n_top < GROW_K) {
                top_j[n_top] = i; top_c[n_top] = corr; n_top++;
            } else {
                int min_k = 0;
                for (int k = 1; k < GROW_K; k++)
                    if (top_c[k] < top_c[min_k]) min_k = k;
                if (corr > top_c[min_k]) { top_j[min_k] = i; top_c[min_k] = corr; }
            }
        }
        int has_invert = 0;
        int64_t target_val_sum = 0;
        int target_val_count = 0;
        for (int k = 0; k < n_top; k++) {
            int eid1 = graph_wire(g0, id0, top_j[k], id0, (uint8_t)(top_c[k] * 255 / 100), 0);
            /* No reverse wire. Reverse forms when existing node evaluates
             * bs_contain(existing, new) in its own grow cycle. */
            edge_set_negation_invert(g0, eid1);
            if (eid1 >= 0 && (g0->edges[eid1].invert_a || g0->edges[eid1].invert_b)) {
                has_invert = 1;
                target_val_sum += (int64_t)abs(g0->nodes[top_j[k]].val);
                target_val_count++;
            }
            g0->total_grown += 1;
        }
        /* Contradiction bootstrap: if this node wired with invert flags,
         * its val must be competitive with its targets' val or the inverted
         * signal will be invisible. Set val to targets' mean so destructive
         * interference can actually occur in accumulation. */
        if (has_invert && target_val_count > 0) {
            int32_t mean_target = (int32_t)(target_val_sum / target_val_count);
            if (abs(g0->nodes[id0].val) < mean_target)
                g0->nodes[id0].val = mean_target;
        }
    }

    /* Mirror to shell 1 */
    int id1 = graph_add(g1, name, 1, &eng->T);
    if (id1 >= 0) {
        g1->nodes[id1].identity = *data;
        g1->nodes[id1].hit_count++;
        g1->nodes[id1].val = g0->nodes[id0].val;  /* inherit boosted val if any */
        if (eng->n_boundary_edges < eng->max_boundary_edges) {
            Edge *be = &eng->boundary_edges[eng->n_boundary_edges++];
            memset(be, 0, sizeof(*be));
            be->src_a = (uint16_t)id0;
            be->src_b = (uint16_t)id0;
            be->dst = (uint16_t)id1;
            be->intershell = 1;
            tline_init_from_weight(&be->tl, 255);
            tline_set_impedance(&be->tl, g0->nodes[id0].Z, g1->nodes[id1].Z);
            be->weight = tline_weight(&be->tl);
        }
        g0->nodes[id0].layer_zero = 0;
        g1->nodes[id1].layer_zero = 0;
        g1->nodes[id1].has_negation = g0->nodes[id0].has_negation;
        g0->total_boundary_crossings++;
        /* Valence growth moved to resolve phase: collision-only (n_incoming >= 2) */

        /* Auto-wire within shell 1 */
        if (g1->auto_grow) {
            int top_j1[GROW_K], top_c1[GROW_K], n_top1 = 0;
            for (int i = 0; i < g1->n_nodes; i++) {
                if (i == id1 || !g1->nodes[i].alive || g1->nodes[i].identity.len < 16) continue;
                int corr = bs_contain(data, &g1->nodes[i].identity);
                if (corr <= g1->grow_mean) continue;
                int rev_corr = bs_contain(&g1->nodes[i].identity, data);
                if (rev_corr > corr + Z_ASYM_MARGIN) continue;
                if (n_top1 < GROW_K) {
                    top_j1[n_top1] = i; top_c1[n_top1] = corr; n_top1++;
                } else {
                    int min_k = 0;
                    for (int k = 1; k < GROW_K; k++)
                        if (top_c1[k] < top_c1[min_k]) min_k = k;
                    if (corr > top_c1[min_k]) { top_j1[min_k] = i; top_c1[min_k] = corr; }
                }
            }
            for (int k = 0; k < n_top1; k++) {
                int s1eid1 = graph_wire(g1, id1, top_j1[k], id1, (uint8_t)(top_c1[k] * 255 / 100), 0);
                /* No reverse wire. */
                edge_set_negation_invert(g1, s1eid1);
                g1->total_grown += 1;
            }
        }
    }

    ot_sys_ingest(&eng->onetwo, (const uint8_t *)data->w, data->len / 8);

    /* Bridge vote: topology-based polarity prediction (protocol-agnostic) */
    int bridge_vote = engine_predict_polarity(eng, id0, name);
    printf("[bridge] node[%d] vote=%d '%s'\n", id0, bridge_vote, name);
    eng->pol_bridge_total++;
    if (bridge_vote) eng->pol_bridge_invert++;

    return id0;
}

/* Whole-word negation scan: returns 1 if text contains a negation marker as
 * a complete word (not a substring of a longer word like "nothing", "note"). */
static int text_has_negation(const char *text) {
    static const char *markers[] = {
        "never", "not", "no", "doesn't", "don't", "won't", "isn't",
        "aren't", "without", "neither", "nor", "cannot", "can't", NULL
    };
    int tlen = (int)strlen(text);
    for (int m = 0; markers[m]; m++) {
        int mlen = (int)strlen(markers[m]);
        const char *p = text;
        while ((p = strstr(p, markers[m])) != NULL) {
            int pos = (int)(p - text);
            /* Check left boundary: start of string or non-alpha */
            int left_ok = (pos == 0) || !isalpha((unsigned char)p[-1]);
            /* Check right boundary: end of string or non-alpha */
            int right_ok = (pos + mlen >= tlen) || !isalpha((unsigned char)p[mlen]);
            if (left_ok && right_ok) return 1;
            p += mlen;
        }
    }
    return 0;
}

int engine_ingest_text(Engine *eng, const char *name, const char *text) {
    /* Keyword scanner vote (text-specific) */
    int kw_vote = text_has_negation(text);

    BitStream bs;
    encode_bytes(&bs, (const uint8_t *)text, (int)strlen(text));
    /* Pre-create the node so has_negation is set BEFORE engine_ingest auto-wires.
     * engine_ingest's graph_add will find the existing node and skip creation. */
    int pre_id = graph_add(&eng->shells[0].g, name, 0, &eng->T);
    if (pre_id >= 0)
        eng->shells[0].g.nodes[pre_id].has_negation = (uint8_t)kw_vote;
    int id = engine_ingest(eng, name, &bs);  /* bridge fires inside */

    printf("[kw] node[%d] vote=%d '%s'\n", id, kw_vote, name);
    eng->pol_kw_total++;
    if (kw_vote) eng->pol_kw_invert++;

    return id;
}

int engine_ingest_raw(Engine *eng, const char *name, const char *text) {
    BitStream bs;
    encode_raw(&bs, text, (int)strlen(text));
    return engine_ingest(eng, name, &bs);
}

int engine_ingest_file(Engine *eng, const char *path) {
    BitStream bs;
    int bytes_read = transducer_file(path, &bs);  /* uses onetwo_parse -> structural rules */
    if (bytes_read <= 0) return -1;

    /* Clean basename for node name */
    const char *name = strrchr(path, '\\');
    if (!name) name = strrchr(path, '/');
    if (!name) name = path;
    else name++;

    char node_name[128];
    snprintf(node_name, sizeof(node_name), "file_%s", name);

    return engine_ingest(eng, node_name, &bs);
}

/* ══════════════════════════════════════════════════════════════
 * ENGINE TICK — the full loop
 * ══════════════════════════════════════════════════════════════ */

/* ═══════════════════════════════════════════════════════════════
 * ENGINE TICK PIPELINE — 10 stages, order is load-bearing.
 *
 * S1  Nesting check       reads: valence.           Writes: child_pool.
 * S2  Boundary propagation reads: val, weight.       Writes: accum, n_incoming.
 * S3  Per-shell propagate  reads: val, weight, Z.    Writes: accum, n_incoming, I_energy.
 * S4  Per-shell resolve    reads: accum, n_incoming. Writes: val. Clears: accum, n_incoming.
 * S5  Grow                 reads: identity.          Writes: edges (new).
 * S6  Prune                reads: weight.            Writes: edges (removed).
 * S7  T-driven decay       reads: T.count.           Writes: weight.
 * S8  Intershell wiring    reads: identity, Z.       Writes: boundary_edges.
 * S9  Hebbian + Z          reads: identity.          Writes: weight, valence, Z.
 * S10 ONETWO + lysis       reads: feedback, valence. Writes: valence, child (lysis).
 * ═══════════════════════════════════════════════════════════════ */
void engine_tick(Engine *eng) {
    T_tick(&eng->T);
    eng->total_ticks++;

    /* ── S1: Nesting check ───────────────────── */
    if (eng->total_ticks % SUBSTRATE_INT == 0)
        nest_check(eng);

    /* ── S2: Boundary propagation (FDTD) ────── */
    for (int i = 0; i < eng->n_boundary_edges; i++) {
        Edge *be = &eng->boundary_edges[i];
        if (be->tl.n_cells == 0 || be->weight == 0) continue;
        Graph *g0 = &eng->shells[0].g, *g1 = &eng->shells[1].g;
        if (be->src_a >= (uint16_t)g0->n_nodes || be->dst >= (uint16_t)g1->n_nodes) continue;
        Node *src = &g0->nodes[be->src_a], *dst = &g1->nodes[be->dst];
        if (src->layer_zero || src->identity.len < 1) continue;

        double input = (double)src->val / (double)VAL_CEILING;
        tline_inject(&be->tl, input);
        tline_step(&be->tl);
        double output = tline_read(&be->tl);
        if (fabs(output) > 1e-10) {
            dst->accum += (int64_t)(output * VAL_CEILING);
            dst->n_incoming++;
            dst->layer_zero = 0;
            dst->last_active = (uint32_t)T_now(&eng->T);
            g0->total_boundary_crossings++;
        }
    }

    /* ── S3: Per-shell propagate ────────────── */
    for (int s = 0; s < eng->n_shells; s++) {
        Graph *g = &eng->shells[s].g;
        g->total_ticks++;

        /* Z-ordered propagate + resolve using cached buckets.
         * O(n_edges + n_nodes) per tick. Cache rebuilt when topology changes. */

        /* Rebuild z-bucket cache if topology changed */
        if (g->z_cache_n_nodes != g->n_nodes || g->z_cache_n_edges != g->n_edges) {
            free(g->z_edge_idx); g->z_edge_idx = NULL;
            free(g->z_node_idx); g->z_node_idx = NULL;

            g->z_max = 0;
            for (int i = 0; i < g->n_nodes; i++)
                if (g->nodes[i].alive) {
                    int nz = coord_z(g->nodes[i].coord);
                    if (nz > g->z_max) g->z_max = nz;
                }
            if (g->z_max > 63) g->z_max = 63;

            /* Edge buckets by dest z */
            int z_edge_count[64] = {0};
            for (int i = 0; i < g->n_edges; i++) {
                Edge *e = &g->edges[i];
                if (e->tl.n_cells == 0 || e->weight == 0) continue;
                int dz = coord_z(g->nodes[e->dst].coord);
                if (dz < 64) z_edge_count[dz]++;
            }
            memset(g->z_edge_off, 0, sizeof(g->z_edge_off));
            for (int z = 0; z < 64; z++) g->z_edge_off[z + 1] = g->z_edge_off[z] + z_edge_count[z];
            g->z_edge_idx = (int *)malloc((g->z_edge_off[64] + 1) * sizeof(int));
            int z_cur[64] = {0};
            for (int i = 0; i < g->n_edges; i++) {
                Edge *e = &g->edges[i];
                if (e->tl.n_cells == 0 || e->weight == 0) continue;
                int dz = coord_z(g->nodes[e->dst].coord);
                if (dz < 64) {
                    g->z_edge_idx[g->z_edge_off[dz] + z_cur[dz]] = i;
                    z_cur[dz]++;
                }
            }

            /* Node buckets by z */
            int z_node_count[64] = {0};
            for (int i = 0; i < g->n_nodes; i++)
                if (g->nodes[i].alive) {
                    int nz = coord_z(g->nodes[i].coord);
                    if (nz < 64) z_node_count[nz]++;
                }
            memset(g->z_node_off, 0, sizeof(g->z_node_off));
            for (int z = 0; z < 64; z++) g->z_node_off[z + 1] = g->z_node_off[z] + z_node_count[z];
            g->z_node_idx = (int *)malloc((g->z_node_off[64] + 1) * sizeof(int));
            memset(z_cur, 0, sizeof(z_cur));
            for (int i = 0; i < g->n_nodes; i++)
                if (g->nodes[i].alive) {
                    int nz = coord_z(g->nodes[i].coord);
                    if (nz < 64) {
                        g->z_node_idx[g->z_node_off[nz] + z_cur[nz]] = i;
                        z_cur[nz]++;
                    }
                }

            g->z_cache_n_nodes = g->n_nodes;
            g->z_cache_n_edges = g->n_edges;
        }

        for (int zl = 0; zl <= g->z_max; zl++) {
            /* Propagate: only edges targeting this z-level */
            for (int j = g->z_edge_off[zl]; j < g->z_edge_off[zl + 1]; j++) {
                int i = g->z_edge_idx[j];
                Edge *e = &g->edges[i];
                Node *na = &g->nodes[e->src_a], *nb = &g->nodes[e->src_b], *nd = &g->nodes[e->dst];
                if (na->layer_zero || nb->layer_zero) continue;
                if (na->identity.len < 1 || nb->identity.len < 1) continue;

                int32_t va = e->invert_a ? -na->val : na->val;
                int32_t vb = e->invert_b ? -nb->val : nb->val;
                double input = (double)(va + vb) / (double)VAL_CEILING;
                tline_inject(&e->tl, input);
                tline_step(&e->tl);

                double output = tline_read(&e->tl);
                if (fabs(output) > 1e-10) {
                    int64_t out_scaled = (int64_t)(output * VAL_CEILING);
                    if (out_scaled > INT32_MAX/2) out_scaled = INT32_MAX/2;
                    if (out_scaled < INT32_MIN/2) out_scaled = INT32_MIN/2;
                    nd->accum += out_scaled;
                    nd->I_energy += (int32_t)(fabs(output) * VAL_CEILING);
                    nd->n_incoming++;
                }
            }

            /* ── S4: Per-shell resolve — only nodes at this z-level ── */
            for (int j = g->z_node_off[zl]; j < g->z_node_off[zl + 1]; j++) {
                int i = g->z_node_idx[j];
                Node *n = &g->nodes[i];
                if (!n->alive || n->layer_zero) continue;
                /* Nodes with children must always enter resolve so child ticks.
                 * With directed edges, a node may have only outgoing edges. */
                int has_child = (n->child_id >= 0 && n->child_id < MAX_CHILDREN
                                 && eng->child_owner[n->child_id] == i);
                if (n->n_incoming == 0 && n->accum == 0 && !has_child) continue;

                /* Nesting delegation — retina reads parent's substrate */
                if (n->child_id >= 0 && n->child_id < MAX_CHILDREN
                    && eng->child_owner[n->child_id] == i) {
                    Graph *child = &eng->child_pool[n->child_id];

                    if (child->retina && child->retina_len >= 64) {
                        /* Inject retina: 8 octant nodes read spatial substrate.
                         * Octant r = (ox,oy,oz) where ox=r&1, oy=(r>>1)&1, oz=(r>>2)&1.
                         * Each octant covers 2x2x2 = 8 substrate positions. */
                        for (int r = 0; r < 8 && r < child->n_nodes; r++) {
                            int ox = r & 1, oy = (r >> 1) & 1, oz = (r >> 2) & 1;
                            int32_t octant_val = 0;
                            for (int lz = oz*2; lz < oz*2+2; lz++)
                                for (int ly = oy*2; ly < oy*2+2; ly++)
                                    for (int lx = ox*2; lx < ox*2+2; lx++)
                                        octant_val += child->retina[lx + ly*4 + lz*16];
                            child->nodes[r].val = octant_val;
                        }
                    } else {
                        /* Fallback: distribute accum across retina nodes */
                        int n_inp = child->n_nodes > 1 ? child->n_nodes - 1 : 1;
                        for (int r = 0; r < n_inp && r < child->n_nodes; r++)
                            child->nodes[r].val = (int32_t)(n->accum / n_inp);
                    }

                    for (int ct = 0; ct < 64; ct++)
                        if (!child_tick_once(child)) break;

                    /* Read output from last node */
                    int out_idx = child->n_nodes - 1;
                    if (out_idx >= 0 && child->nodes[out_idx].alive)
                        n->val = child->nodes[out_idx].val;

                    n->last_active = (uint32_t)T_now(&eng->T);
                    n->n_incoming = 0; n->accum = 0;
                    continue;
                }

                int32_t old_val = n->val;
                if (n->n_incoming <= 1) {
                    n->val = (int32_t)n->accum;
                } else {
                    int64_t total = n->accum + (int64_t)n->val;
                    int N = n->n_incoming + 1;
                    int64_t resolved = 2 * total / N - old_val;
                    if (resolved > INT32_MAX/2) resolved = INT32_MAX/2;
                    if (resolved < INT32_MIN/2) resolved = INT32_MIN/2;
                    n->val = (int32_t)resolved;
                }
                if (n->valence > 0) {
                    n->val = (int32_t)((int64_t)old_val * n->valence +
                                       (int64_t)n->val * (255 - n->valence)) / 255;
                }
                int64_t cap64 = (int64_t)abs(old_val) + llabs(n->accum);
                if (cap64 > INT32_MAX/2) cap64 = INT32_MAX/2;
                int32_t cap = (int32_t)cap64;
                if (abs(n->val) > cap) n->val = n->val > 0 ? cap : -cap;

                /* Hard ceiling: prevents unbounded accumulation */
                if (n->val > VAL_CEILING) n->val = VAL_CEILING;
                if (n->val < -VAL_CEILING) n->val = -VAL_CEILING;

                /* I_energy: collision energy in current when voltage cancelled.
                 * I_energy was accumulated as abs(va)+abs(vb) during propagation.
                 * Subtract the resolved voltage energy to get the magnetic remainder.
                 * Large I_energy + small |val| = destructive collision = XOR. */
                if (n->n_incoming >= 2) {
                    int32_t val_energy = abs(n->val);
                    n->I_energy = (n->I_energy > val_energy) ?
                        n->I_energy - val_energy : 0;
                    /* Collision-only valence: mass forms at interaction vertices.
                     * Skip contradicted nodes — disputed nodes don't harden. */
                    if (n->valence < 255 && !n->contradicted) {
                        n->valence++;
                        /* Source-side valence: reward upstream nodes that fed this collision.
                         * Feed-forward edges (i,i,j) make j the collision site but i the emitter.
                         * Without this, emitters never crystallize — cut off from collision valence. */
                        for (int e_back = 0; e_back < g->n_edges; e_back++) {
                            Edge *ed = &g->edges[e_back];
                            if (ed->dst == (uint16_t)i && ed->weight > 0) {
                                Node *src = &g->nodes[ed->src_a];
                                if (src->alive && !src->contradicted && src->valence < 253) {
                                    src->valence += 3;  /* upstream reward: +3 per collision fed */
                                    if (src->valence > 255) src->valence = 255;
                                }
                            }
                        }
                    }
                } else {
                    n->I_energy = 0;
                }

                n->last_active = (uint32_t)T_now(&eng->T);
                n->n_incoming = 0; n->accum = 0;
            }
        }

        /* S4 cont: Safety clear — catch nodes missed by z-layer iteration */
        for (int i = 0; i < g->n_nodes; i++) {
            if (g->nodes[i].n_incoming > 0 || g->nodes[i].accum != 0) {
                g->nodes[i].n_incoming = 0;
                g->nodes[i].accum = 0;
            }
        }

        /* ── S5: Grow ────────────────────────── */
        if (g->auto_grow && g->grow_interval > 0 && (g->total_ticks % (unsigned)g->grow_interval == 0)) {
            long long mc_sum = 0; int mc_count = 0, total_grown = 0;
            /* Popcount precompute: skip pairs with low min/max ratio */
            int id_pop[MAX_NODES];
            for (int i = 0; i < g->n_nodes; i++) {
                if (!g->nodes[i].alive || g->nodes[i].layer_zero || g->nodes[i].identity.len < 16)
                    id_pop[i] = -1;
                else
                    id_pop[i] = bs_popcount(&g->nodes[i].identity);
            }
            /* Incoming edge count: opportunity scoring prefers wiring to relays
             * (n_in<=1 → collision), not saturated nodes (n_in>=4). */
            int n_in[MAX_NODES];
            memset(n_in, 0, g->n_nodes * sizeof(int));
            for (int e = 0; e < g->n_edges; e++)
                if (g->edges[e].weight > 0) n_in[g->edges[e].dst]++;
            for (int i = 0; i < g->n_nodes; i++) {
                if (id_pop[i] < 0) continue;
                /* Per-node local grow threshold (MDL-style):
                 * Dense coherent nodes → higher threshold (already well-wired).
                 * Sparse or incoherent nodes → lower threshold (need connections).
                 * Base: grow_mean. Scale by local edge density. */
                int local_thresh = g->grow_mean;
                if (n_in[i] >= 4) {
                    /* Dense: scale up by density ratio. 4 edges → 4/3x, 8 → 8/3x */
                    local_thresh = g->grow_mean * (3 + n_in[i]) / 3;
                    if (local_thresh > 95) local_thresh = 95;
                }
                if (g->nodes[i].coherent < 0) {
                    /* Incoherent/frustrated: lower threshold, easier to wire */
                    local_thresh = local_thresh * 2 / 3;
                }
                int top_j[GROW_K], top_c[GROW_K], top_raw[GROW_K], n_top = 0;
                int grow_x = coord_x(g->nodes[i].coord) % 64;
                for (int j = 0; j < g->n_nodes; j++) {
                    if (i == j || id_pop[j] < 0) continue;
                    /* Spatial filter: same type prefix (X-plane) or adjacent */
                    int jx = coord_x(g->nodes[j].coord) % 64;
                    int gdx = jx - grow_x;
                    if (gdx < 0) gdx = -gdx;
                    if (gdx > 2 && gdx < 62) continue;
                    /* Popcount ratio filter: mutual_contain bounded by min/max */
                    int mn = id_pop[i] < id_pop[j] ? id_pop[i] : id_pop[j];
                    int mx = id_pop[i] > id_pop[j] ? id_pop[i] : id_pop[j];
                    if (mx > 0 && mn * 100 / mx < local_thresh) continue;
                    int corr = bs_contain(&g->nodes[i].identity, &g->nodes[j].identity);
                    /* Directional gate: skip if reverse containment dominates */
                    int rev_corr = bs_contain(&g->nodes[j].identity, &g->nodes[i].identity);
                    if (rev_corr > corr + Z_ASYM_MARGIN) continue;
                    mc_sum += corr; mc_count++;
                    /* Opportunity scoring: prefer creating collision points.
                     * relay→collision is highest leverage edge. */
                    int opp;
                    if (n_in[j] <= 1) opp = 3;      /* relay → collision */
                    else if (n_in[j] <= 3) opp = 2;  /* active vertex */
                    else opp = 1;                      /* saturated */
                    int eff_corr = corr * opp / 3;
                    if (eff_corr <= local_thresh) continue;
                    if (n_top < GROW_K) {
                        top_j[n_top] = j; top_c[n_top] = eff_corr;
                        top_raw[n_top] = corr; n_top++;
                    } else {
                        int min_k = 0;
                        for (int k = 1; k < GROW_K; k++)
                            if (top_c[k] < top_c[min_k]) min_k = k;
                        if (eff_corr > top_c[min_k]) {
                            top_j[min_k] = j; top_c[min_k] = eff_corr;
                            top_raw[min_k] = corr;
                        }
                    }
                }
                for (int k = 0; k < n_top; k++) {
                    int j = top_j[k];
                    int geid1;
                    if (n_in[j] <= 1) {
                        /* j is a relay (low fanin). Wire feed-forward: i → j.
                         * src_a=i, src_b=i, dst=j. Creates a chain — graph_compute_z
                         * sees sa!=d && sb!=d, so Z increments. */
                        if (graph_find_edge(g, i, i, j) >= 0) continue;
                        geid1 = graph_wire(g, i, i, j, (uint8_t)(top_raw[k] * 255 / 100), 0);
                    } else {
                        /* j is saturated. Standard listen-edge: j feeds into i. */
                        if (graph_find_edge(g, i, j, i) >= 0) continue;
                        geid1 = graph_wire(g, i, j, i, (uint8_t)(top_raw[k] * 255 / 100), 0);
                    }
                    edge_set_negation_invert(g, geid1);
                    g->total_grown += 1; total_grown += 1;
                }
            }
            if (mc_count > 0) g->grow_mean = (int)(mc_sum / mc_count);
            g->grow_interval = adaptive_interval(g->grow_interval, total_grown, 2, 200);
        }

        /* ── S6: Prune ───────────────────────── */
        if (g->auto_prune && g->prune_interval > 0 && (g->total_ticks % (unsigned)g->prune_interval == 0)) {
            int pre = g->n_edges, w = 0;
            for (int i = 0; i < g->n_edges; i++) {
                if (g->edges[i].weight >= (uint8_t)g->prune_threshold) {
                    if (w != i) g->edges[w] = g->edges[i];
                    w++;
                } else g->total_pruned++;
            }
            g->n_edges = w;
            g->prune_interval = adaptive_interval(g->prune_interval, pre - w, 4, 400);
        }

        /* ── S7: T-driven decay ──────────────── */
        for (int i = 0; i < g->n_nodes; i++) {
            Node *n = &g->nodes[i];
            if (!n->alive || n->layer_zero) continue;
            uint32_t age = (uint32_t)T_now(&eng->T) - n->last_active;
            if (age > SUBSTRATE_INT * 3) {
                if (n->valence > 0) n->valence--;
                for (int e = 0; e < g->n_edges; e++)
                    if (g->edges[e].src_a == i || g->edges[e].src_b == i) {
                        tline_weaken(&g->edges[e].tl, 0.01 * n->plasticity);
                        g->edges[e].weight = tline_weight(&g->edges[e].tl);
                    }
            }
        }
    }

    /* ── S8: Intershell wiring ──────────────── */
    if (eng->boundary_interval > 0 && eng->total_ticks % (unsigned)eng->boundary_interval == 0 && eng->n_shells >= 2) {
        int before = eng->n_boundary_edges;
        Graph *g0 = &eng->shells[0].g, *g1 = &eng->shells[1].g;
        /* Bloom filter for O(1) exists-check instead of O(b) scan */
        #define BMAP_BITS 65536
        uint8_t bmap[BMAP_BITS / 8];
        memset(bmap, 0, sizeof(bmap));
        for (int b = 0; b < eng->n_boundary_edges; b++) {
            uint32_t key = ((uint32_t)eng->boundary_edges[b].src_a << 16)
                          | (uint32_t)eng->boundary_edges[b].dst;
            uint32_t h = key * 2654435761u;
            bmap[(h >> 16) / 8] |= 1 << ((h >> 16) % 8);
        }
        for (int i = 0; i < g0->n_nodes; i++) {
            if (!g0->nodes[i].alive || g0->nodes[i].layer_zero || g0->nodes[i].identity.len < 16) continue;
            for (int j = 0; j < g1->n_nodes; j++) {
                if (!g1->nodes[j].alive || g1->nodes[j].layer_zero || g1->nodes[j].identity.len < 16) continue;
                /* Bloom check + linear fallback on hit */
                uint32_t key = ((uint32_t)i << 16) | (uint32_t)j;
                uint32_t h = key * 2654435761u;
                int bloom_hit = (bmap[(h >> 16) / 8] >> ((h >> 16) % 8)) & 1;
                int exists = 0;
                if (bloom_hit) {
                    for (int b = 0; b < eng->n_boundary_edges; b++) {
                        if (eng->boundary_edges[b].src_a == (uint16_t)i &&
                            eng->boundary_edges[b].dst == (uint16_t)j) { exists = 1; break; }
                    }
                }
                if (exists) continue;
                int corr = bs_contain(&g0->nodes[i].identity, &g1->nodes[j].identity);
                if (corr > g0->grow_mean && eng->n_boundary_edges < eng->max_boundary_edges) {
                    Edge *be = &eng->boundary_edges[eng->n_boundary_edges++];
                    memset(be, 0, sizeof(*be));
                    be->src_a = (uint16_t)i; be->src_b = (uint16_t)i; be->dst = (uint16_t)j;
                    be->intershell = 1;
                    tline_init_from_weight(&be->tl, (uint8_t)(corr * 255 / 100));
                    tline_set_impedance(&be->tl, g0->nodes[i].Z, g1->nodes[j].Z);
                    be->weight = tline_weight(&be->tl);
                }
            }
        }
        eng->boundary_interval = adaptive_interval(eng->boundary_interval, eng->n_boundary_edges - before, 10, 500);
    }

    /* ── S9: Hebbian + topology ─────────────────── */
    if (eng->learn_interval > 0 && eng->total_ticks % (unsigned)eng->learn_interval == 0) {
        for (int s = 0; s < eng->n_shells; s++) {
            graph_learn(&eng->shells[s].g, eng->onetwo.feedback[0]);
            graph_compute_topology(&eng->shells[s].g, s);
        }
    }

    /* ── S10: ONETWO + lysis ────────────── */
    if (eng->total_ticks % SUBSTRATE_INT == 0) {
        uint8_t state_buf[1024];
        int pos = 0;

        /* Multi-pass state observation with region tracking for windowed sense.
         * All shells contribute — shell 2 (verifier) was invisible in single-pass
         * because shell 0 edges often filled the 400-byte budget alone. */
        sense_region_t regions[SENSE_MAX_REGIONS];
        int n_regions = 0;

        /* Pass 1: edge weights from ALL shells */
        int pass1_start = pos;
        for (int s = 0; s < eng->n_shells; s++) {
            Graph *g = &eng->shells[s].g;
            for (int e = 0; e < g->n_edges && pos < 400; e++)
                state_buf[pos++] = g->edges[e].weight;
        }
        if (pos > pass1_start) {
            regions[n_regions].offset = pass1_start;
            regions[n_regions].length = pos - pass1_start;
            regions[n_regions].pass_id = 1;
            n_regions++;
        }

        /* Pass 2: node state from ALL shells.
         * 4 bytes per node: valence, crystal_strength, val_sign, I_energy_sat. */
        int pass2_start = pos;
        for (int s = 0; s < eng->n_shells; s++) {
            Graph *g = &eng->shells[s].g;
            for (int i = 0; i < g->n_nodes && pos < 500; i++) {
                if (g->nodes[i].alive && !g->nodes[i].layer_zero) {
                    state_buf[pos++] = g->nodes[i].valence;
                    if (pos < 500)
                        state_buf[pos++] = (uint8_t)crystal_strength(&g->nodes[i]);
                    if (pos < 500)
                        state_buf[pos++] = g->nodes[i].val > 0 ? 255
                                         : g->nodes[i].val < 0 ? 0 : 128;
                    if (pos < 500) {
                        int32_t ie = g->nodes[i].I_energy;
                        state_buf[pos++] = (uint8_t)(ie < 0 ? 0 : (ie >> 23) > 255 ? 255 : (ie >> 23));
                    }
                }
            }
        }
        if (pos > pass2_start) {
            regions[n_regions].offset = pass2_start;
            regions[n_regions].length = pos - pass2_start;
            regions[n_regions].pass_id = 2;
            n_regions++;
        }

        /* Precompute incoming edge count for shell 0 (used by coherence + profile) */
        Graph *g0_s10 = &eng->shells[0].g;
        int s10_n_in[MAX_NODES];
        memset(s10_n_in, 0, g0_s10->n_nodes * sizeof(int));
        for (int e = 0; e < g0_s10->n_edges; e++)
            if (g0_s10->edges[e].weight > 0) s10_n_in[g0_s10->edges[e].dst]++;

        /* Pass 3: Computational profile — 3 bytes summarizing topology.
         * Too small for sense (below ACTIVE_THRESH), skipped for regions. */
        {
            int n_relay = 0, n_collision = 0, n_crystal = 0;
            for (int i = 0; i < g0_s10->n_nodes; i++) {
                Node *np = &g0_s10->nodes[i];
                if (!np->alive || np->layer_zero) continue;
                if (np->valence >= 200) n_crystal++;
                else if (s10_n_in[i] >= 2) n_collision++;
                else n_relay++;
            }
            if (pos < 506) {
                state_buf[pos++] = (uint8_t)(n_relay > 255 ? 255 : n_relay);
                state_buf[pos++] = (uint8_t)(n_collision > 255 ? 255 : n_collision);
                state_buf[pos++] = (uint8_t)(n_crystal > 255 ? 255 : n_crystal);
            }
        }
        /* Pass 3 NOT added to regions — only 3 bytes, below ACTIVE_THRESH */

        /* Pass 4: Edge inversion byte — ALL edges, one byte each.
         * Encodes (weight >> 1) | (invert << 7).
         * Normal edge: high bit 0, value = weight/2.
         * Inverted edge: high bit 1, value = weight/2 + 128.
         * Per-pass extraction makes channel 7 (inversion bit) visible. */
        int pass4_start = pos;
        for (int s = 0; s < eng->n_shells; s++) {
            Graph *g = &eng->shells[s].g;
            for (int e = 0; e < g->n_edges && pos < 800; e++) {
                if (g->edges[e].weight == 0) continue;
                int inv = (g->edges[e].invert_a || g->edges[e].invert_b) ? 1 : 0;
                state_buf[pos++] = (uint8_t)((g->edges[e].weight >> 1) | (inv << 7));
            }
        }
        if (pos > pass4_start) {
            regions[n_regions].offset = pass4_start;
            regions[n_regions].length = pos - pass4_start;
            regions[n_regions].pass_id = 4;
            n_regions++;
        }

        /* ONETWO still gets full buffer unchanged */
        if (pos > 0) ot_sys_ingest(&eng->onetwo, state_buf, pos);

        /* Windowed sense: per-region extraction + cross-wire + single decay */
        if (n_regions > 0)
            sense_pass_windowed(eng, state_buf, regions, n_regions, &eng->last_sense);

        /* Snapshot for next cycle's observer comparison */
        eng->prev_sense = eng->last_sense;

        /* Coherence: two signals, either can trigger incoherence.
         * 1. Val delta vs neighbors (original) — catches value disagreement.
         * 2. Edge weight instability — catches competitive S3 pressure when
         *    val is saturated at ceiling. If edge weights churning, the node
         *    is under structural conflict even if val doesn't move. */
        for (int i = 0; i < g0_s10->n_nodes; i++) {
            Node *n = &g0_s10->nodes[i];
            if (!n->alive || n->layer_zero) continue;
            int32_t my_delta = abs(n->val - n->prev_val);

            /* Compute current incoming edge weight sum + neighbor val delta */
            int64_t nbr_sum = 0; int nbr_count = 0;
            int32_t edge_sum = 0;
            for (int e = 0; e < g0_s10->n_edges; e++) {
                Edge *ed = &g0_s10->edges[e];
                if (ed->weight == 0) continue;
                int nbr = -1;
                if (ed->src_a == (uint16_t)i) nbr = ed->dst;
                else if (ed->dst == (uint16_t)i) { nbr = ed->src_a; edge_sum += ed->weight; }
                if (nbr >= 0 && nbr < g0_s10->n_nodes && g0_s10->nodes[nbr].alive) {
                    nbr_sum += abs(g0_s10->nodes[nbr].val - g0_s10->nodes[nbr].prev_val);
                    nbr_count++;
                }
            }

            int32_t scale = nbr_count > 0 ? (int32_t)(nbr_sum / nbr_count) : 0;
            int divisor;
            if (n->valence >= 200)     divisor = 4;
            else if (s10_n_in[i] >= 2) divisor = 3;
            else                        divisor = 2;

            /* Signal 1: val delta vs neighbors */
            int val_incoherent = (scale >= 2 && my_delta > scale / divisor) ? 1 : 0;

            /* Signal 2: edge weight instability.
             * If incoming edge weight sum changed by >25% since last cycle,
             * competitive S3 is reshuffling this node's connections. */
            int32_t edge_delta = abs(edge_sum - n->prev_edge_sum);
            int edge_incoherent = 0;
            if (n->prev_edge_sum > 0 && edge_delta * 4 > n->prev_edge_sum)
                edge_incoherent = 1;

            if (val_incoherent || edge_incoherent)
                n->coherent = -1;
            else if (scale < 2 && !edge_incoherent)
                n->coherent = 1;   /* nothing moved */
            else
                n->coherent = 1;   /* coherent */

            n->prev_val = n->val;
            n->prev_edge_sum = edge_sum;
        }

        /* Direct graph introspection: count incoherent nodes.
         * This is the thermometer, not the barometer.
         * feedback[7] measures fingerprint delta (rate of change of a proxy).
         * graph_error measures structural conflict directly. */
        {
            int n_incoh = 0, n_alive = 0;
            for (int i = 0; i < g0_s10->n_nodes; i++) {
                Node *n = &g0_s10->nodes[i];
                if (n->alive && !n->layer_zero) {
                    n_alive++;
                    if (n->coherent < 0) n_incoh++;
                }
            }
            /* Ratio, not count. 0-100 percentage. Scales with graph size.
             * Below 20 nodes, learning noise dominates — percentage is
             * meaningless. Fingerprint delta is the primary signal there. */
            if (n_alive >= 30)
                eng->graph_error = (int32_t)(n_incoh * 100 / n_alive);
            else
                eng->graph_error = 0;
        }

        /* TYXZT: Parent SPRT accumulation — inner T for root graph.
         * Accumulate graph_error delta across heartbeats instead of
         * thresholding instantaneously. Decay: half-life ~5 heartbeats
         * (same 7/8 ratio as child inner T). */
        {
            Graph *g0_acc = &eng->shells[0].g;
            int32_t delta = eng->graph_error - g0_acc->prev_output;
            g0_acc->error_accum += delta;
            g0_acc->error_accum = g0_acc->error_accum * 7 / 8;
            g0_acc->prev_output = eng->graph_error;
            g0_acc->local_heartbeat++;
        }

        int32_t error = eng->onetwo.feedback[7];
        int32_t stability = eng->onetwo.feedback[4];
        int32_t energy = eng->onetwo.feedback[5];
        /* The S10 error block uses error_mag vs energy/3.
         * Keep this driven by fingerprint delta only — it's calibrated
         * for that signal. graph_error drives the close-loop block
         * which has its own threshold (SUBSTRATE_INT/4). */
        int error_mag = abs(error);

        if (error_mag > energy / 3 && energy > 0) {
            for (int s = 0; s < eng->n_shells; s++) {
                Graph *g = &eng->shells[s].g;
                if (g->auto_grow) {
                    int bias = g->grow_mean * error_mag / (energy + 1);
                    g->grow_mean -= bias;
                    if (g->grow_mean < 0) g->grow_mean = 0;
                    g->grow_interval = adaptive_interval(g->grow_interval, error_mag, 2, 200);
                }
            }
            /* Targeted valence decay: only incoherent nodes lose confidence.
             * Coherent nodes survive error — localization, not carpet bombing. */
            for (int s = 0; s < eng->n_shells; s++) {
                Graph *gv = &eng->shells[s].g;
                for (int i = 0; i < gv->n_nodes; i++) {
                    Node *n = &gv->nodes[i];
                    if (!n->alive || n->layer_zero) continue;
                    if (n->coherent >= 0) continue;  /* coherent or unknown: skip */
                    if (n->valence > VALENCE_DECAY_RATE)
                        n->valence -= VALENCE_DECAY_RATE;
                    else
                        n->valence = 0;
                }
            }
            eng->low_error_run = 0;
        } else {
            eng->low_error_run++;
        }

        if (stability && error_mag < energy / 4) {
            for (int s = 0; s < eng->n_shells; s++) {
                Graph *g = &eng->shells[s].g;
                for (int i = 0; i < g->n_nodes; i++) {
                    Node *n = &g->nodes[i];
                    if (!n->alive || n->layer_zero || n->contradicted) continue;
                    uint32_t age = (uint32_t)T_now(&eng->T) - n->last_active;
                    if (age < SUBSTRATE_INT) {
                        int nv = (int)n->valence + 1;
                        n->valence = nv > 255 ? 255 : (uint8_t)nv;
                    }
                }
            }
        }

        if (eng->n_shells >= 2) {
            Graph *g0 = &eng->shells[0].g, *g1 = &eng->shells[1].g;
            int s0 = 0, s1 = 0;
            for (int e = 0; e < g0->n_edges; e++)
                if (g0->edges[e].weight > PRUNE_FLOOR) s0++;
            for (int e = 0; e < g1->n_edges; e++)
                if (g1->edges[e].weight > PRUNE_FLOOR) s1++;
            int agree = (s0 > 0 && s1 > 0 && abs(s0 - s1) < (s0 + s1) / 3);
            for (int b = 0; b < eng->n_boundary_edges; b++) {
                Edge *be = &eng->boundary_edges[b];
                if (be->weight == 0) continue;
                if (agree) {
                    tline_strengthen(&be->tl, 0.01);
                    be->weight = tline_weight(&be->tl);
                } else {
                    tline_weaken(&be->tl, 0.01);
                    be->weight = tline_weight(&be->tl);
                }
            }
        }

        if (eng->low_error_run >= (int)SUBSTRATE_INT) {
            for (int s = 0; s < eng->n_shells; s++) {
                Graph *g = &eng->shells[s].g;
                if (g->auto_prune) {
                    int raised = g->prune_threshold + 2;
                    int w = 0;
                    for (int i = 0; i < g->n_edges; i++) {
                        if (g->edges[i].weight >= raised) {
                            if (w != i) g->edges[w] = g->edges[i];
                            w++;
                        } else g->total_pruned++;
                    }
                    g->n_edges = w;
                }
            }
            eng->low_error_run = 0;
        }

        /* Lysis: apoptosis for nodes whose valence decayed below threshold */
        {
            Graph *g0 = &eng->shells[0].g;
            for (int i = 0; i < g0->n_nodes; i++) {
                Node *n = &g0->nodes[i];
                if (n->child_id >= 0 && n->valence <= LYSIS_THRESHOLD) {
                    printf("  [lysis] node[%d] '%s' valence=%d -> apoptosis\n",
                           i, n->name, n->valence);
                    nest_remove(eng, i);
                }
            }
        }
    }

    /* ═══ FEEDBACK → TOPOLOGY: the loop closes ═══
     *
     * Runs EVERY TICK with potentially stale feedback — intentional.
     * feedback[] only updates at SUBSTRATE_INT boundaries. Between boundaries,
     * this block fires with the same values: a 137-tick heartbeat of sustained
     * drive response between observations. The staleness IS the feature.
     *
     * Three drive states. Three responses. Modulating existing machinery.
     * No new phases, no new outputs. The organism steers itself through
     * the machinery it already has.
     */
    {
        int32_t fp_error = eng->onetwo.feedback[7];
        int32_t abs_fp = fp_error < 0 ? -fp_error : fp_error;
        int32_t stability = eng->onetwo.feedback[4];

        /* TYXZT: Drive state from SPRT accumulation, not single-tick snapshot.
         * error_accum integrates graph_error deltas over multiple heartbeats.
         * Positive accumulation = structure dissolving = frustration.
         * Near-zero = stable = curiosity. Negative = converging = boredom. */
        Graph *g0_drive = &eng->shells[0].g;
        int32_t accumulated = g0_drive->error_accum;
        int32_t abs_accum = accumulated < 0 ? -accumulated : accumulated;

        int32_t fp_thresh = (int32_t)(SUBSTRATE_INT / 4);                           /* = 34 */
        int32_t ge_thresh = (int32_t)(MISMATCH_TAX_NUM * 400 / MISMATCH_TAX_DEN);  /* = 14 */

        /* Frustration: accumulated error above threshold OR instantaneous spike.
         * Keep the instantaneous path — a sudden large error shouldn't wait
         * for accumulation to catch up. Belt and suspenders. */
        int frustrated = (abs_accum > fp_thresh) || (abs_fp > fp_thresh) || (eng->graph_error > ge_thresh);
        int32_t abs_error = abs_accum > abs_fp ? abs_accum : abs_fp;
        if (eng->graph_error > abs_error) abs_error = eng->graph_error;
        int32_t silence_thresh = (int32_t)(MISMATCH_TAX_NUM);

        /* ── FRUSTRATION: either signal above its threshold ──
         * Seek connections. Grow faster. Find and erode the worst
         * incoherent node — not the most deviant, but the most WRONG.
         * A coherent node far from the mean is learning (moved WITH neighbors).
         * An incoherent node far from the mean is broken. */
        if (frustrated) {
            Graph *g0 = &eng->shells[0].g;

            /* Accelerate growth: shrink interval toward minimum */
            g0->grow_interval = g0->grow_interval * 2 / 3;
            if (g0->grow_interval < 2) g0->grow_interval = 2;

            /* Find worst node: incoherent > coherent, crystal > non-crystal.
             * Scoring: base = deviation from mean val.
             * +10,000 if incoherent. +20,000 if incoherent crystal. */
            int64_t val_sum = 0;
            int val_count = 0;
            for (int i = 0; i < g0->n_nodes; i++) {
                if (g0->nodes[i].alive && !g0->nodes[i].layer_zero) {
                    val_sum += g0->nodes[i].val;
                    val_count++;
                }
            }
            if (val_count > 0) {
                int32_t mean_val = (int32_t)(val_sum / val_count);
                int max_score = 0, target = -1;
                for (int i = 0; i < g0->n_nodes; i++) {
                    Node *n = &g0->nodes[i];
                    if (!n->alive || n->layer_zero) continue;
                    int score = abs(n->val - mean_val);
                    if (n->coherent < 0) score += 10000;         /* incoherent */
                    if (n->coherent < 0 && n->valence >= 200)
                        score += 20000;                            /* incoherent crystal */
                    if (score > max_score) { max_score = score; target = i; }
                }
                /* Erode the target's crystallization at mismatch tax rate.
                 * Only incoherent nodes: a coherent node far from mean is learning. */
                if (target >= 0 && g0->nodes[target].coherent < 0
                    && g0->nodes[target].valence > 0) {
                    int erosion = (int)g0->nodes[target].valence
                                * (int)MISMATCH_TAX_NUM / (int)MISMATCH_TAX_DEN;
                    if (erosion < 1) erosion = 1;
                    g0->nodes[target].valence -= erosion;
                }
            }

            /* Contradiction erosion: when two nodes linked by inverted edges
             * both have high valence, erode the one with lower crystal strength.
             * Conservation alone can't resolve contradictions — this gives
             * frustration real teeth against stalemates. */
            for (int e = 0; e < g0->n_edges; e++) {
                Edge *ed = &g0->edges[e];
                if (!ed->invert_a && !ed->invert_b) continue;
                if (ed->weight == 0) continue;
                Node *na = &g0->nodes[ed->src_a];
                Node *nd = &g0->nodes[ed->dst];
                if (!na->alive || !nd->alive) continue;
                if (na->valence < 128 || nd->valence < 128) continue;  /* only high-valence stalemates */

                /* Erode the weaker crystal */
                Node *weaker = (crystal_strength(na) <= crystal_strength(nd)) ? na : nd;
                int erosion = VALENCE_DECAY_RATE * 2;
                if (weaker->valence > erosion)
                    weaker->valence -= erosion;
                else
                    weaker->valence = 0;
            }

            /* Heat incoherent nodes: frustration raises plasticity */
            for (int i = 0; i < g0->n_nodes; i++) {
                Node *nh = &g0->nodes[i];
                if (!nh->alive || nh->layer_zero) continue;
                if (nh->coherent < 0) {
                    nh->plasticity += PLASTICITY_HEAT;

                    /* Phase transition: structural cleaving.
                     * Node has been incoherent long enough that Lc tuning can't fix it.
                     * Consume the heat to sever the worst incoming edge. */
                    if (nh->plasticity > PLASTICITY_MAX) {
                        int worst_edge = -1;
                        double max_lc = -1.0;

                        /* Find most resistive incoming edge (highest Lc = most impedance) */
                        for (int e = 0; e < g0->n_edges; e++) {
                            Edge *ed = &g0->edges[e];
                            if (ed->dst == (uint16_t)i && ed->weight > 0 && ed->tl.n_cells > 0) {
                                if (ed->tl.Lc[0] > max_lc) {
                                    max_lc = ed->tl.Lc[0];
                                    worst_edge = e;
                                }
                            }
                        }

                        /* Sever and consume heat — only if there's a bond to break */
                        if (worst_edge >= 0) {
                            g0->edges[worst_edge].weight = 0;
                            g0->edges[worst_edge].tl.n_cells = 0;
                            eng->total_cleaved++;
                            nh->plasticity = PLASTICITY_DEFAULT;  /* consume: reset to 1.0 */
                        } else {
                            nh->plasticity = PLASTICITY_MAX;  /* no bond to break: cap */
                        }
                    }
                }
            }
            g0->drive = 1;  /* frustration */
        }

        /* ── CURIOSITY: error positive but not alarming ──
         * The system is learning. Do nothing. The absence of intervention
         * IS curiosity. This comment exists so the absence is intentional. */
        if (!frustrated && !(abs_error < silence_thresh && stability)) {
            g0_drive->drive = 0;  /* curiosity */
        }

        /* ── BOREDOM: error below noise floor + system stable ──
         * Crystallize COHERENT active nodes only. A node must be:
         * - coherent (moving WITH its neighbors, not trivially silent)
         * - active (val != 0, recently touched)
         * Coherent by silence doesn't count. Only nodes carrying signal
         * that agrees with their neighborhood harden. */
        if (abs_error < silence_thresh && stability) {
            Graph *g0 = &eng->shells[0].g;

            for (int i = 0; i < g0->n_nodes; i++) {
                Node *n = &g0->nodes[i];
                if (!n->alive || n->layer_zero) continue;
                if (n->coherent <= 0) continue;  /* must be actively coherent */
                if (n->contradicted) continue;   /* disputed nodes don't harden */
                uint32_t age = T_now(&eng->T) - n->last_active;
                if (n->val != 0 && age < SUBSTRATE_INT) {
                    if (n->valence < 255) n->valence++;
                    /* Cool coherent nodes: boredom lowers plasticity */
                    if (n->plasticity > PLASTICITY_MIN) {
                        n->plasticity -= PLASTICITY_COOL;
                        if (n->plasticity < PLASTICITY_MIN) n->plasticity = PLASTICITY_MIN;
                    }
                    /* Strengthen incoming edges: they carried signal that worked */
                    for (int e = 0; e < g0->n_edges; e++) {
                        if (g0->edges[e].dst == (uint16_t)i && g0->edges[e].weight > 0) {
                            tline_strengthen(&g0->edges[e].tl, 0.01 * n->plasticity);
                            g0->edges[e].weight = tline_weight(&g0->edges[e].tl);
                        }
                    }
                }
            }

            /* Prune faster: weak edges are noise that survived by chance */
            g0->prune_interval = g0->prune_interval * 2 / 3;
            if (g0->prune_interval < 4) g0->prune_interval = 4;

            /* Grow slower: stop seeking, digest what you have */
            g0->grow_interval = g0->grow_interval * 3 / 2;
            if (g0->grow_interval > 200) g0->grow_interval = 200;

            g0->drive = 2;  /* boredom */
        }
    }
}

/* ══════════════════════════════════════════════════════════════
 * QUERY
 * ══════════════════════════════════════════════════════════════ */

int engine_query(const Engine *eng, const BitStream *query,
                 QueryResult *results, int max_results) {
    int n_results = 0;
    for (int s = 0; s < eng->n_shells; s++) {
        const Graph *g = &eng->shells[s].g;
        for (int i = 0; i < g->n_nodes; i++) {
            if (!g->nodes[i].alive || g->nodes[i].identity.len < 16) continue;
            /* Containment: what fraction of query bits appear in node.
             * Correct for asymmetric search (small query inside large corpus). */
            int corr = bs_contain(query, &g->nodes[i].identity);
            if (corr < 20) continue;

            if (n_results < max_results) {
                QueryResult *r = &results[n_results++];
                r->id = i; r->shell = s;
                r->raw_corr = corr;
                r->crystal = crystal_strength(&g->nodes[i]);
                r->resonance = corr + g->nodes[i].valence / 2;
                strncpy(r->name, g->nodes[i].name, NAME_LEN - 1);
            } else {
                int min_k = 0;
                for (int k = 1; k < max_results; k++)
                    if (results[k].resonance < results[min_k].resonance) min_k = k;
                int res = corr + g->nodes[i].valence / 2;
                if (res > results[min_k].resonance) {
                    results[min_k].id = i; results[min_k].shell = s;
                    results[min_k].raw_corr = corr;
                    results[min_k].crystal = crystal_strength(&g->nodes[i]);
                    results[min_k].resonance = res;
                    strncpy(results[min_k].name, g->nodes[i].name, NAME_LEN - 1);
                }
            }
        }
    }
    return n_results;
}

int engine_query_name(const Engine *eng, const char *substr,
                      QueryResult *results, int max_results) {
    /* Name substring search across all shells.
     * For short queries where structural fingerprinting is meaningless. */
    int n_results = 0;
    int slen = (int)strlen(substr);
    if (slen == 0) return 0;

    /* Case-insensitive substring match */
    char lower_query[NAME_LEN];
    for (int i = 0; i < slen && i < NAME_LEN - 1; i++)
        lower_query[i] = (char)tolower((unsigned char)substr[i]);
    lower_query[slen < NAME_LEN - 1 ? slen : NAME_LEN - 1] = '\0';

    for (int s = 0; s < eng->n_shells; s++) {
        const Graph *g = &eng->shells[s].g;
        for (int i = 0; i < g->n_nodes; i++) {
            if (!g->nodes[i].alive) continue;

            /* Lowercase node name for comparison */
            char lower_name[NAME_LEN];
            const char *nn = g->nodes[i].name;
            for (int k = 0; nn[k] && k < NAME_LEN - 1; k++)
                lower_name[k] = (char)tolower((unsigned char)nn[k]);
            lower_name[strlen(nn) < NAME_LEN - 1 ? strlen(nn) : NAME_LEN - 1] = '\0';

            if (!strstr(lower_name, lower_query)) continue;

            /* Score: crystal strength + valence (structural standing in the graph) */
            int score = crystal_strength(&g->nodes[i]) + g->nodes[i].valence;

            if (n_results < max_results) {
                QueryResult *r = &results[n_results++];
                r->id = i; r->shell = s;
                r->raw_corr = 100;  /* exact name match */
                r->crystal = crystal_strength(&g->nodes[i]);
                r->resonance = score;
                strncpy(r->name, g->nodes[i].name, NAME_LEN - 1);
            } else {
                int min_k = 0;
                for (int k = 1; k < max_results; k++)
                    if (results[k].resonance < results[min_k].resonance) min_k = k;
                if (score > results[min_k].resonance) {
                    results[min_k].id = i; results[min_k].shell = s;
                    results[min_k].raw_corr = 100;
                    results[min_k].crystal = crystal_strength(&g->nodes[i]);
                    results[min_k].resonance = score;
                    strncpy(results[min_k].name, g->nodes[i].name, NAME_LEN - 1);
                }
            }
        }
    }
    return n_results;
}

/* ══════════════════════════════════════════════════════════════
 * STATUS
 * ══════════════════════════════════════════════════════════════ */

void engine_status(const Engine *eng) {
    printf("ENGINE: %llu ticks, %d boundary edges, %d children\n",
           (unsigned long long)eng->total_ticks,
           eng->n_boundary_edges, eng->n_children);
    for (int s = 0; s < eng->n_shells; s++) {
        const Graph *g = &eng->shells[s].g;
        printf("  shell[%d] '%s': %d nodes, %d edges, mean=%d\n",
               s, eng->shells[s].name, g->n_nodes, g->n_edges, g->grow_mean);
    }
    printf("  ONETWO: density=%d symmetry=%d period=%d error=%d\n",
           eng->onetwo.analysis.density, eng->onetwo.analysis.symmetry,
           eng->onetwo.analysis.period, eng->onetwo.feedback[7]);
}

/* ══════════════════════════════════════════════════════════════
 * TRANSMISSION LINE EDGES — FDTD propagation on graph edges
 *
 * Ported from xyzt_unified.c (February 2026, proven standalone).
 * Self-contained: operates on TLineGraph, does not touch Engine.
 * This is Step 1 of the incremental integration.
 * ══════════════════════════════════════════════════════════════ */

void tlg_init(TLineGraph *g) { memset(g, 0, sizeof(TLineGraph)); }

int tlg_add_node(TLineGraph *g, int x, int y, int z, const char *name) {
    int id = g->n_nodes++;
    TLineNode *n = &g->nodes[id];
    memset(n, 0, sizeof(TLineNode));
    n->x = x; n->y = y; n->z = z;
    n->impedance = 1.0;
    strncpy(n->name, name, 31);
    return id;
}

int  tlg_add_edge(TLineGraph *g, int src, int dst, double L_base) {
    int id = g->n_edges++;
    TLineEdge *e = &g->edges[id];
    memset(e, 0, sizeof(TLineEdge));
    e->src = src; e->dst = dst;
    TLineNode *ns = &g->nodes[src], *nd = &g->nodes[dst];
    int dist = abs(ns->x - nd->x) + abs(ns->y - nd->y) + abs(ns->z - nd->z);
    e->n_cells = dist * 4 + 12;
    if (e->n_cells > TLINE_EC) e->n_cells = TLINE_EC;
    e->L_base = L_base;
    e->Z0_base = sqrt(L_base / TLINE_C0);
    for (int i = 0; i < TLINE_EC; i++) e->Lc[i] = L_base;

    ns->edge_ids[ns->n_edges] = id;
    ns->edge_dirs[ns->n_edges] = -1;  /* src side */
    ns->n_edges++;
    nd->edge_ids[nd->n_edges] = id;
    nd->edge_dirs[nd->n_edges] = +1;  /* dst side */
    nd->n_edges++;
    return id;
}

void tlg_clear(TLineGraph *g) {
    for (int ei = 0; ei < g->n_edges; ei++) {
        TLineEdge *e = &g->edges[ei];
        memset(e->V, 0, sizeof(double) * TLINE_EC);
        memset(e->I, 0, sizeof(double) * TLINE_EC);
    }
    for (int ni = 0; ni < g->n_nodes; ni++) {
        TLineNode *n = &g->nodes[ni];
        n->V = 0; n->V_peak = 0;
        n->energy = 0; n->I_energy = 0; n->total_energy = 0;
    }
}

void tlg_inject(TLineGraph *g, int nid, int bit, double amp) {
    TLineNode *n = &g->nodes[nid];
    double a = bit ? +amp : -amp;
    double sigma = 2.0;
    for (int ei = 0; ei < n->n_edges; ei++) {
        TLineEdge *e = &g->edges[n->edge_ids[ei]];
        int dir = n->edge_dirs[ei];
        int center = (dir < 0) ? 3 : e->n_cells - 4;
        for (int i = 0; i < e->n_cells; i++) {
            double d = (double)(i - center);
            double env = exp(-d * d / (2 * sigma * sigma));
            e->V[i] += a * env;
            double Z_local = sqrt(e->Lc[i] / TLINE_C0);
            double I_sign = (dir < 0) ? +1.0 : -1.0;
            e->I[i] += (a * env / Z_local) * I_sign;
        }
    }
}

/* Node impedance → edge boundary L bumps.
 * High impedance = mass on the transmission line = voltage amplification.
 * T = 2*Z_high / (Z_low + Z_high) > 1 when Z_high > Z_low.
 * Same physics as Schwarzschild in universe_tline_v2.c. */
void tlg_apply_impedance(TLineGraph *g) {
    /* Reset all cells to base */
    for (int ei = 0; ei < g->n_edges; ei++) {
        TLineEdge *e = &g->edges[ei];
        for (int i = 0; i < e->n_cells; i++) e->Lc[i] = e->L_base;
    }
    /* Apply node impedance as boundary bumps */
    for (int ni = 0; ni < g->n_nodes; ni++) {
        TLineNode *n = &g->nodes[ni];
        if (n->impedance <= 1.0001) continue;
        for (int ei = 0; ei < n->n_edges; ei++) {
            TLineEdge *e = &g->edges[n->edge_ids[ei]];
            int dir = n->edge_dirs[ei];
            int nc = e->n_cells;
            for (int d = 0; d < TLINE_IMP_DEPTH && d < nc; d++) {
                double scale = n->impedance * exp(-(double)d * 0.7);
                if (scale < 1.0) scale = 1.0;
                if (dir < 0)
                    e->Lc[d] = e->L_base * scale;
                else
                    e->Lc[nc - 1 - d] = e->L_base * scale;
            }
        }
    }
}

/* Single FDTD step: telegrapher's equations on every edge,
 * then junction coupling at every non-input node. */
void tlg_propagate_step(TLineGraph *g) {
    /* FDTD on each edge with per-cell L */
    for (int ei = 0; ei < g->n_edges; ei++) {
        TLineEdge *e = &g->edges[ei];
        int nc = e->n_cells;
        /* Update I (staggered: I[i] sits between V[i] and V[i+1]) */
        for (int i = 0; i < nc - 1; i++) {
            double dV = e->V[i + 1] - e->V[i];
            e->I[i] -= (TLINE_DT / e->Lc[i]) * dV
                      + (TLINE_DAMP * TLINE_DT / e->Lc[i]) * e->I[i];
        }
        /* Update V (interior cells only) */
        for (int i = 1; i < nc - 1; i++) {
            double dI = e->I[i] - e->I[i - 1];
            e->V[i] -= (TLINE_DT / TLINE_C0) * dI;
        }
    }

    /* Node junction coupling: impedance-weighted average of arriving signals */
    for (int ni = 0; ni < g->n_nodes; ni++) {
        TLineNode *n = &g->nodes[ni];
        if (n->n_edges == 0 || n->is_input) continue;

        double sum_VZ = 0, sum_1Z = 0;
        for (int ei = 0; ei < n->n_edges; ei++) {
            TLineEdge *e = &g->edges[n->edge_ids[ei]];
            int dir = n->edge_dirs[ei];
            int cell = (dir > 0) ? e->n_cells - 2 : 1;
            double V_end = e->V[cell];
            double Z_local = sqrt(e->Lc[cell] / TLINE_C0);
            sum_VZ += V_end / Z_local;
            sum_1Z += 1.0 / Z_local;
        }
        /* Node as impedance stub in the junction */
        double node_contrib = n->V / (n->impedance + 0.001);
        sum_VZ += node_contrib;
        sum_1Z += 1.0 / (n->impedance + 0.001);

        if (sum_1Z > 1e-12) n->V = sum_VZ / sum_1Z;
        if (fabs(n->V) > fabs(n->V_peak)) n->V_peak = n->V;
        n->energy += 0.5 * n->V * n->V;

        /* Track I energy */
        double Isum = 0;
        for (int ei = 0; ei < n->n_edges; ei++) {
            TLineEdge *e = &g->edges[n->edge_ids[ei]];
            int dir = n->edge_dirs[ei];
            int cell = (dir > 0) ? e->n_cells - 2 : 1;
            Isum += e->I[cell] * e->I[cell];
        }
        n->I_energy += 0.5 * Isum;
        n->total_energy = n->energy + n->I_energy;

        /* Scatter node voltage to boundary cells */
        for (int ei = 0; ei < n->n_edges; ei++) {
            TLineEdge *e = &g->edges[n->edge_ids[ei]];
            int dir = n->edge_dirs[ei];
            if (dir > 0) e->V[e->n_cells - 1] = n->V;
            else e->V[0] = n->V;
        }
    }
}

void tlg_propagate(TLineGraph *g, int steps) {
    tlg_apply_impedance(g);
    for (int s = 0; s < steps; s++) tlg_propagate_step(g);
}

void tlg_observe(TLineNode *n) {
    n->V_peak = n->V_peak;  /* already tracked in propagate_step */
}

/* Back-reaction: collision-only. Impedance grows only when 2+ edges
 * deliver energy simultaneously. Single signal = relay = no mass. */
void tlg_backreaction(TLineGraph *g, double kappa) {
    for (int ni = 0; ni < g->n_nodes; ni++) {
        TLineNode *n = &g->nodes[ni];
        if (n->is_input) continue;
        int active_edges = 0;
        for (int ei = 0; ei < n->n_edges; ei++) {
            TLineEdge *e = &g->edges[n->edge_ids[ei]];
            int dir = n->edge_dirs[ei];
            int cell = (dir > 0) ? e->n_cells - 2 : 1;
            double edge_energy = 0.5 * e->V[cell] * e->V[cell]
                               + 0.5 * e->I[cell] * e->I[cell];
            if (edge_energy > 0.01) active_edges++;
        }
        if (active_edges >= 2)
            n->impedance += kappa * n->total_energy;
        if (n->impedance < 1.0) n->impedance = 1.0;
        if (n->impedance > 50.0) n->impedance = 50.0;
        n->energy *= 0.5; n->I_energy *= 0.5; n->total_energy *= 0.5;
    }
}

/* ══════════════════════════════════════════════════════════════
 * SAVE / LOAD (binary format, v9-v13)
 * ══════════════════════════════════════════════════════════════ */

static void save_graph(FILE *f, const Graph *g) {
    fwrite(&g->n_nodes, 4, 1, f);
    fwrite(&g->n_edges, 4, 1, f);
    fwrite(&g->total_ticks, 8, 1, f);
    fwrite(&g->total_learns, 8, 1, f);
    fwrite(&g->total_grown, 8, 1, f);
    fwrite(&g->total_pruned, 8, 1, f);
    fwrite(&g->total_boundary_crossings, 8, 1, f);
    int32_t params[15] = {
        g->grow_threshold, g->prune_threshold,
        g->learn_strengthen, g->learn_weaken, g->learn_rate,
        g->auto_grow, g->auto_prune, g->auto_learn,
        g->grow_mean, g->grow_interval, g->prune_interval,
        g->error_accum, g->prev_output, g->local_heartbeat, (int32_t)g->drive
    };
    fwrite(params, sizeof(params), 1, f);
    for (int i = 0; i < g->n_nodes; i++)
        fwrite(&g->nodes[i], sizeof(Node), 1, f);
    for (int i = 0; i < g->n_edges; i++)
        fwrite(&g->edges[i], sizeof(Edge), 1, f);
}

static int load_graph(FILE *f, Graph *g, int ver) {
    /* g must already have nodes/edges allocated (graph_init or shell init) */
    if (fread(&g->n_nodes, 4, 1, f) != 1) return -1;
    if (fread(&g->n_edges, 4, 1, f) != 1) return -1;
    if (g->n_nodes > MAX_NODES) g->n_nodes = MAX_NODES;
    if (g->n_edges > MAX_EDGES) g->n_edges = MAX_EDGES;
    fread(&g->total_ticks, 8, 1, f);
    fread(&g->total_learns, 8, 1, f);
    fread(&g->total_grown, 8, 1, f);
    fread(&g->total_pruned, 8, 1, f);
    fread(&g->total_boundary_crossings, 8, 1, f);
    if (ver >= 12) {
        int32_t params[15];
        fread(params, sizeof(params), 1, f);
        g->grow_threshold = params[0];    g->prune_threshold = params[1];
        g->learn_strengthen = params[2];  g->learn_weaken = params[3];
        g->learn_rate = params[4];
        g->auto_grow = params[5];         g->auto_prune = params[6];
        g->auto_learn = params[7];
        g->grow_mean = params[8];         g->grow_interval = params[9];
        g->prune_interval = params[10];
        g->error_accum = params[11];      g->prev_output = params[12];
        g->local_heartbeat = params[13];  g->drive = (uint8_t)params[14];
    } else {
        int32_t params[11];
        fread(params, sizeof(params), 1, f);
        g->grow_threshold = params[0];    g->prune_threshold = params[1];
        g->learn_strengthen = params[2];  g->learn_weaken = params[3];
        g->learn_rate = params[4];
        g->auto_grow = params[5];         g->auto_prune = params[6];
        g->auto_learn = params[7];
        g->grow_mean = params[8];         g->grow_interval = params[9];
        g->prune_interval = params[10];
        g->error_accum = 0; g->prev_output = 0;
        g->local_heartbeat = 0; g->drive = 0;
    }
    if (ver >= 13) {
        for (int i = 0; i < g->n_nodes; i++)
            if (fread(&g->nodes[i], sizeof(Node), 1, f) != 1) return -1;
    } else {
        /* v12: Node without plasticity (4 bytes smaller) */
        size_t old_sz = sizeof(Node) - sizeof(float);
        for (int i = 0; i < g->n_nodes; i++) {
            memset(&g->nodes[i], 0, sizeof(Node));
            if (fread(&g->nodes[i], old_sz, 1, f) != 1) return -1;
            g->nodes[i].plasticity = PLASTICITY_DEFAULT;
        }
    }
    for (int i = 0; i < g->n_edges; i++)
        if (fread(&g->edges[i], sizeof(Edge), 1, f) != 1) return -1;
    return 0;
}

int engine_save(const Engine *eng, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t magic = 0x58595A54; /* XYZT */
    uint32_t version = 13;
    fwrite(&magic, 4, 1, f);
    fwrite(&version, 4, 1, f);

    /* Engine scalars */
    uint32_t ns = (uint32_t)eng->n_shells;
    uint32_t nb = (uint32_t)eng->n_boundary_edges;
    fwrite(&ns, 4, 1, f);
    fwrite(&nb, 4, 1, f);
    fwrite(&eng->total_ticks, 8, 1, f);
    int32_t li = eng->learn_interval;
    int32_t bi = eng->boundary_interval;
    int32_t ler = eng->low_error_run;
    int32_t nc = eng->n_children;
    fwrite(&li, 4, 1, f);
    fwrite(&bi, 4, 1, f);
    fwrite(&ler, 4, 1, f);
    fwrite(&nc, 4, 1, f);

    /* SubstrateT */
    fwrite(&eng->T.count, 8, 1, f);
    fwrite(&eng->T.birth_ms, sizeof(double), 1, f);

    /* OneTwoSystem (feedback + counters — analysis is recomputed) */
    for (int i = 0; i < 8; i++)
        fwrite(&eng->onetwo.feedback[i], 4, 1, f);
    int32_t tc = eng->onetwo.tick_count;
    int32_t ti = eng->onetwo.total_inputs;
    fwrite(&tc, 4, 1, f);
    fwrite(&ti, 4, 1, f);
    fwrite(&eng->onetwo.prev_match, 4, 1, f);

    /* Shells */
    for (int s = 0; s < eng->n_shells; s++)
        save_graph(f, &eng->shells[s].g);

    /* Boundary edges */
    for (int i = 0; i < eng->n_boundary_edges; i++)
        fwrite(&eng->boundary_edges[i], sizeof(Edge), 1, f);

    /* Children */
    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (eng->child_owner[i] < 0) continue;
        int32_t slot = i;
        int32_t owner = eng->child_owner[i];
        fwrite(&slot, 4, 1, f);
        fwrite(&owner, 4, 1, f);
        save_graph(f, &eng->child_pool[i]);
    }

    /* ── YEE1 block: L-field + accumulator persistence ── */
    if (yee_is_initialized()) {
        uint32_t yee_magic = YEE1_MAGIC;
        uint16_t gx = YEE_GX, gy = YEE_GY, gz = YEE_GZ;
        float *h_buf = (float *)malloc(YEE_N_VOXELS * sizeof(float));
        if (h_buf) {
            fwrite(&yee_magic, 4, 1, f);
            fwrite(&gx, 2, 1, f);
            fwrite(&gy, 2, 1, f);
            fwrite(&gz, 2, 1, f);

            yee_download_L(h_buf, YEE_N_VOXELS);
            fwrite(h_buf, sizeof(float), YEE_N_VOXELS, f);

            yee_download_acc_raw(h_buf, YEE_N_VOXELS);
            fwrite(h_buf, sizeof(float), YEE_N_VOXELS, f);

            free(h_buf);
        }
    }

    fclose(f);
    return 0;
}

int engine_load(Engine *eng, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint32_t magic, version;
    fread(&magic, 4, 1, f);
    fread(&version, 4, 1, f);
    if (magic != 0x58595A54) { fclose(f); return -1; }

    /* v9 packed ShellHdr — must match Solo AI/xyzt.c layout exactly */
    #pragma pack(push, 1)
    typedef struct {
        uint32_t n_nodes, n_edges;
        uint64_t ticks, learns, grown, pruned, crossings;
        int32_t  grow_t, prune_t, learn_s, learn_w, learn_r;
        int32_t  auto_g, auto_p, auto_l;
        uint8_t  sid, resolve_jt;
        char     sname[30];
    } V9ShellHdr;
    #pragma pack(pop)

    if (version >= 3 && version <= 9) {
        /* v3-v9 format: packed EngHdr already read magic+version.
         * Remaining header: n_shells(u32), n_be(u32), ticks(u64). */
        uint32_t ns, nb;
        fread(&ns, 4, 1, f);
        fread(&nb, 4, 1, f);
        fread(&eng->total_ticks, sizeof(eng->total_ticks), 1, f);
        eng->n_shells = (int)ns;
        if (eng->n_shells > MAX_SHELLS) eng->n_shells = MAX_SHELLS;
        eng->n_boundary_edges = (int)nb;
        if (eng->n_boundary_edges > eng->max_boundary_edges)
            eng->n_boundary_edges = eng->max_boundary_edges;

        /* Per-shell: v9 writes packed ShellHdr, then bulk Node[], then bulk Edge[] */
        for (int s = 0; s < eng->n_shells; s++) {
            Graph *g = &eng->shells[s].g;
            V9ShellHdr sh;
            if (fread(&sh, sizeof(sh), 1, f) != 1) { fclose(f); return -3; }
            g->n_nodes = (int)sh.n_nodes;
            g->n_edges = (int)sh.n_edges;
            if (g->n_nodes > MAX_NODES) g->n_nodes = MAX_NODES;
            if (g->n_edges > MAX_EDGES) g->n_edges = MAX_EDGES;
            g->total_ticks = sh.ticks;
            g->total_learns = sh.learns;
            g->total_grown = sh.grown;
            g->total_pruned = sh.pruned;
            g->total_boundary_crossings = sh.crossings;
            g->grow_threshold = sh.grow_t;
            g->prune_threshold = sh.prune_t;
            g->learn_strengthen = sh.learn_s;
            g->learn_weaken = sh.learn_w;
            g->learn_rate = sh.learn_r;
            g->auto_grow = sh.auto_g;
            g->auto_prune = sh.auto_p;
            g->auto_learn = sh.auto_l;
            eng->shells[s].id = sh.sid;
            strncpy(eng->shells[s].name, sh.sname, 31);
            /* v9 bulk reads — old Node without plasticity */
            {
                size_t old_sz = sizeof(Node) - sizeof(float);
                for (int i = 0; i < g->n_nodes; i++) {
                    memset(&g->nodes[i], 0, sizeof(Node));
                    if (fread(&g->nodes[i], old_sz, 1, f) != 1) { fclose(f); return -4; }
                    g->nodes[i].plasticity = PLASTICITY_DEFAULT;
                }
            }
            for (int i = 0; i < g->n_edges; i++)
                if (fread(&g->edges[i], sizeof(Edge), 1, f) != 1) { fclose(f); return -5; }
            /* v9 does NOT write grow_mean */
        }
        /* Boundary edges */
        for (int i = 0; i < eng->n_boundary_edges; i++)
            if (fread(&eng->boundary_edges[i], sizeof(Edge), 1, f) != 1) { fclose(f); return -6; }
        /* v9 child graph data — skip (we'll re-spawn from topology) */

    } else if (version == 10) {
        /* v10 format (PC engine): same header field order, no ShellHdr */
        uint32_t ns, nb;
        fread(&ns, 4, 1, f);
        fread(&nb, 4, 1, f);
        fread(&eng->total_ticks, sizeof(eng->total_ticks), 1, f);
        eng->n_shells = (int)ns;
        if (eng->n_shells > MAX_SHELLS) eng->n_shells = MAX_SHELLS;
        eng->n_boundary_edges = (int)nb;
        if (eng->n_boundary_edges > eng->max_boundary_edges)
            eng->n_boundary_edges = eng->max_boundary_edges;

        for (int s = 0; s < eng->n_shells; s++) {
            Graph *g = &eng->shells[s].g;
            fread(&g->n_nodes, sizeof(g->n_nodes), 1, f);
            if (g->n_nodes > MAX_NODES) g->n_nodes = MAX_NODES;
            {
                size_t old_sz = sizeof(Node) - sizeof(float);
                for (int i = 0; i < g->n_nodes; i++) {
                    memset(&g->nodes[i], 0, sizeof(Node));
                    fread(&g->nodes[i], old_sz, 1, f);
                    g->nodes[i].plasticity = PLASTICITY_DEFAULT;
                }
            }
            fread(&g->n_edges, sizeof(g->n_edges), 1, f);
            if (g->n_edges > MAX_EDGES) g->n_edges = MAX_EDGES;
            for (int i = 0; i < g->n_edges; i++)
                fread(&g->edges[i], sizeof(Edge), 1, f);
            fread(&g->grow_mean, sizeof(g->grow_mean), 1, f);
        }
        for (int i = 0; i < eng->n_boundary_edges; i++)
            fread(&eng->boundary_edges[i], sizeof(Edge), 1, f);
    } else if (version >= 11 && version <= 13) {
        /* v11-v13: full persistence — engine params, OneTwoSystem, SubstrateT, children */
        uint32_t ns, nb;
        fread(&ns, 4, 1, f);
        fread(&nb, 4, 1, f);
        fread(&eng->total_ticks, 8, 1, f);
        int32_t li, bi, ler, nc;
        fread(&li, 4, 1, f);  eng->learn_interval = li;
        fread(&bi, 4, 1, f);  eng->boundary_interval = bi;
        fread(&ler, 4, 1, f); eng->low_error_run = ler;
        fread(&nc, 4, 1, f);
        eng->n_shells = (int)ns;
        if (eng->n_shells > MAX_SHELLS) eng->n_shells = MAX_SHELLS;
        eng->n_boundary_edges = (int)nb;
        if (eng->n_boundary_edges > eng->max_boundary_edges)
            eng->n_boundary_edges = eng->max_boundary_edges;

        /* SubstrateT */
        fread(&eng->T.count, 8, 1, f);
        fread(&eng->T.birth_ms, sizeof(double), 1, f);

        /* OneTwoSystem */
        for (int i = 0; i < 8; i++)
            fread(&eng->onetwo.feedback[i], 4, 1, f);
        int32_t tc, ti;
        fread(&tc, 4, 1, f); eng->onetwo.tick_count = tc;
        fread(&ti, 4, 1, f); eng->onetwo.total_inputs = ti;
        fread(&eng->onetwo.prev_match, 4, 1, f);

        /* Shells */
        for (int s = 0; s < eng->n_shells; s++) {
            if (load_graph(f, &eng->shells[s].g, (int)version) != 0) { fclose(f); return -3; }
        }

        /* Boundary edges */
        for (int i = 0; i < eng->n_boundary_edges; i++)
            if (fread(&eng->boundary_edges[i], sizeof(Edge), 1, f) != 1) { fclose(f); return -6; }

        /* Children */
        eng->n_children = 0;
        for (int i = 0; i < MAX_CHILDREN; i++) eng->child_owner[i] = -1;
        for (int c = 0; c < nc; c++) {
            int32_t slot, owner;
            fread(&slot, 4, 1, f);
            fread(&owner, 4, 1, f);
            if (slot < 0 || slot >= MAX_CHILDREN) { fclose(f); return -7; }
            graph_init(&eng->child_pool[slot]);
            if (load_graph(f, &eng->child_pool[slot], (int)version) != 0) { fclose(f); return -8; }
            eng->child_owner[slot] = owner;
            eng->n_children++;
            /* Reconnect parent node's child_id */
            Graph *g0 = &eng->shells[0].g;
            if (owner >= 0 && owner < g0->n_nodes && g0->nodes[owner].alive)
                g0->nodes[owner].child_id = (int8_t)slot;
        }
    } else {
        fprintf(stderr, "Unknown .xyzt version %u\n", version);
        fclose(f);
        return -1;
    }

    /* ── Try reading optional YEE1 block (appended after v13 data) ── */
    if (yee_is_initialized()) {
        uint32_t yee_magic = 0;
        if (fread(&yee_magic, 4, 1, f) == 1 && yee_magic == YEE1_MAGIC) {
            uint16_t gx, gy, gz;
            fread(&gx, 2, 1, f);
            fread(&gy, 2, 1, f);
            fread(&gz, 2, 1, f);
            int n = (int)gx * (int)gy * (int)gz;
            if (n == YEE_N_VOXELS) {
                float *h_buf = (float *)malloc(n * sizeof(float));
                if (h_buf) {
                    if (fread(h_buf, sizeof(float), n, f) == (size_t)n)
                        yee_upload_L(h_buf, n);
                    if (fread(h_buf, sizeof(float), n, f) == (size_t)n)
                        yee_upload_acc(h_buf, n);
                    free(h_buf);
                }
            }
            /* dimension mismatch: skip, L stays at init default */
        }
        /* no YEE1 magic: backward compat, L stays at init default */
    }

    fclose(f);
    return 0;
}

/* ══════════════════════════════════════════════════════════════
 * WIRE GRAPH BRIDGE — toolkit wire.bin ↔ engine graph
 * ══════════════════════════════════════════════════════════════ */

/* Toolkit wire.bin format (from tools engine wire.h v3) */
/* v3 (legacy) toolkit limits */
#define TK_WIRE_V3_MAX_NODES 512
#define TK_WIRE_V3_MAX_EDGES 4096

/* v4: engine-scale (matches CLI wire.h v4) */
#define TK_WIRE_MAX_NODES 4096
#define TK_WIRE_MAX_EDGES 65536
#define TK_WIRE_NAME_LEN  128
#define TK_WIRE_MAGIC     0x57495245  /* "WIRE" */
#define TK_WIRE_VER       4
#define TK_WIRE_ALIVE     128

#pragma pack(push, 1)
/* v3 node layout — for migration */
typedef struct {
    char     name[TK_WIRE_NAME_LEN];
    uint8_t  type;
    uint8_t  _pad[3];
    uint32_t created;
    uint32_t last_active;
    uint32_t hit_count;
} TkWireNodeV3;  /* 144 bytes */

/* v4 node — with plasticity, valence, layer_zero */
typedef struct {
    char     name[TK_WIRE_NAME_LEN];
    uint8_t  type;       /* 0=file, 1=problem, 2=concept */
    uint8_t  layer_zero; /* all edges passes>0, fails==0 */
    uint8_t  valence;    /* count of alive edges */
    uint8_t  _pad;
    uint32_t created;
    uint32_t last_active;
    uint32_t hit_count;
    float    plasticity; /* learning rate multiplier (0.5..2.0) */
} TkWireNode;  /* 148 bytes */

typedef struct {
    uint16_t src;
    uint16_t dst;
    uint8_t  weight;     /* 5-250, alive >= 128 */
    uint8_t  passes;
    uint8_t  fails;
    uint8_t  stale;
    uint32_t created;
    uint32_t last_active;
} TkWireEdge;  /* 16 bytes */

/* v3 graph — for migration detection */
typedef struct {
    uint32_t     magic;
    uint8_t      version;
    uint8_t      _pad[3];
    uint32_t     n_nodes;
    uint32_t     n_edges;
    uint32_t     total_learns;
    TkWireNodeV3 nodes[TK_WIRE_V3_MAX_NODES];
    TkWireEdge   edges[TK_WIRE_V3_MAX_EDGES];
    char         node_shells[TK_WIRE_V3_MAX_NODES][32];
} TkWireGraphV3;

typedef struct {
    uint32_t   magic;
    uint8_t    version;
    uint8_t    _pad[3];
    uint32_t   n_nodes;
    uint32_t   n_edges;
    uint32_t   total_learns;
    TkWireNode nodes[TK_WIRE_MAX_NODES];
    TkWireEdge edges[TK_WIRE_MAX_EDGES];
    char       node_shells[TK_WIRE_MAX_NODES][32];
} TkWireGraph;
#pragma pack(pop)

int engine_wire_import(Engine *eng, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    /* Detect v3 vs v4 by file size */
    TkWireGraph *wg = NULL;

    if (fsize > 0 && (size_t)fsize <= sizeof(TkWireGraphV3) + 64) {
        /* v3 format — migrate on the fly */
        TkWireGraphV3 *old = (TkWireGraphV3 *)calloc(1, sizeof(TkWireGraphV3));
        if (!old) { fclose(f); return -2; }
        size_t rd = (size_t)fsize < sizeof(TkWireGraphV3) ? (size_t)fsize : sizeof(TkWireGraphV3);
        if (fread(old, 1, rd, f) < 16 || old->magic != TK_WIRE_MAGIC) {
            free(old); fclose(f); return -3;
        }
        fclose(f);

        wg = (TkWireGraph *)calloc(1, sizeof(TkWireGraph));
        if (!wg) { free(old); return -2; }
        wg->magic = TK_WIRE_MAGIC;
        wg->version = TK_WIRE_VER;
        wg->n_nodes = old->n_nodes;
        wg->n_edges = old->n_edges;
        wg->total_learns = old->total_learns;
        for (uint32_t i = 0; i < old->n_nodes && i < TK_WIRE_V3_MAX_NODES; i++) {
            strncpy(wg->nodes[i].name, old->nodes[i].name, TK_WIRE_NAME_LEN - 1);
            wg->nodes[i].type = old->nodes[i].type;
            wg->nodes[i].created = old->nodes[i].created;
            wg->nodes[i].last_active = old->nodes[i].last_active;
            wg->nodes[i].hit_count = old->nodes[i].hit_count;
            wg->nodes[i].plasticity = 1.0f;
            strncpy(wg->node_shells[i], old->node_shells[i], 31);
        }
        for (uint32_t i = 0; i < old->n_edges && i < TK_WIRE_V3_MAX_EDGES; i++) {
            wg->edges[i] = old->edges[i];
        }
        free(old);
        printf("Wire import: migrated v3 → v4\n");
    } else {
        /* v4 format — direct read */
        wg = (TkWireGraph *)calloc(1, sizeof(TkWireGraph));
        if (!wg) { fclose(f); return -2; }
        size_t to_read = (size_t)fsize;
        if (to_read > sizeof(TkWireGraph)) to_read = sizeof(TkWireGraph);
        if (fread(wg, 1, to_read, f) < 16 || wg->magic != TK_WIRE_MAGIC) {
            free(wg); fclose(f); return -3;
        }
        fclose(f);
    }

    Graph *g = &eng->shells[0].g;
    int imported_nodes = 0;
    int node_map[TK_WIRE_MAX_NODES];
    memset(node_map, -1, sizeof(node_map));

    /* Import nodes with non-empty names */
    for (uint32_t i = 0; i < wg->n_nodes && i < TK_WIRE_MAX_NODES; i++) {
        if (wg->nodes[i].name[0] == '\0') continue;
        int eid = graph_add(g, wg->nodes[i].name, 0, &eng->T);
        if (eid < 0) continue;
        node_map[i] = eid;
        g->nodes[eid].hit_count = wg->nodes[i].hit_count;
        /* v4: carry plasticity into engine node */
        if (wg->nodes[i].plasticity >= 0.5f && wg->nodes[i].plasticity <= 2.0f)
            g->nodes[eid].plasticity = wg->nodes[i].plasticity;
        imported_nodes++;
    }

    /* Import alive, non-stale edges */
    int imported_edges = 0;
    for (uint32_t i = 0; i < wg->n_edges && i < TK_WIRE_MAX_EDGES; i++) {
        TkWireEdge *we = &wg->edges[i];
        if (we->weight < TK_WIRE_ALIVE) continue;
        if (we->stale) continue;
        int s = (we->src < TK_WIRE_MAX_NODES) ? node_map[we->src] : -1;
        int d = (we->dst < TK_WIRE_MAX_NODES) ? node_map[we->dst] : -1;
        if (s < 0 || d < 0) continue;
        /* Engine edges: src_a=s, src_b=s (pass-through), dst=d */
        if (graph_wire(g, s, s, d, we->weight, 0) >= 0)
            imported_edges++;
    }

    free(wg);
    printf("Wire import: %d nodes, %d edges from '%s'\n",
           imported_nodes, imported_edges, path);
    return imported_nodes;
}

int engine_wire_export(const Engine *eng, const char *path) {
    TkWireGraph *wg = (TkWireGraph *)calloc(1, sizeof(TkWireGraph));
    if (!wg) return -1;

    wg->magic = TK_WIRE_MAGIC;
    wg->version = TK_WIRE_VER;

    const Graph *g = &eng->shells[0].g;

    /* Build alive-node index map (engine idx → export idx) */
    int export_map[MAX_NODES];
    memset(export_map, -1, sizeof(export_map));

    uint32_t nn = 0;
    for (int i = 0; i < g->n_nodes && nn < TK_WIRE_MAX_NODES; i++) {
        if (!g->nodes[i].alive) continue;
        export_map[i] = (int)nn;
        TkWireNode *wn = &wg->nodes[nn];
        strncpy(wn->name, g->nodes[i].name, TK_WIRE_NAME_LEN - 1);
        wn->type = g->nodes[i].shell_id;
        wn->layer_zero = g->nodes[i].layer_zero;
        wn->valence = g->nodes[i].valence;
        wn->created = g->nodes[i].created;
        wn->last_active = g->nodes[i].last_active;
        wn->hit_count = g->nodes[i].hit_count;
        wn->plasticity = g->nodes[i].plasticity;
        strncpy(wg->node_shells[nn], "xyzt-pc", 31);
        nn++;
    }
    wg->n_nodes = nn;

    /* Export engine edges */
    uint32_t ne = 0;
    for (int i = 0; i < g->n_edges && ne < TK_WIRE_MAX_EDGES; i++) {
        const Edge *e = &g->edges[i];
        int ws = export_map[e->src_a];
        int wd = export_map[e->dst];
        if (ws < 0 || wd < 0) continue;

        TkWireEdge *we = &wg->edges[ne];
        we->src = (uint16_t)ws;
        we->dst = (uint16_t)wd;
        we->weight = e->weight;
        we->passes = 0;
        we->fails = 0;
        we->stale = 0;
        we->created = e->created;
        we->last_active = e->last_active;

        /* Crystallized source node → weight boost */
        if (g->nodes[e->src_a].crystal_n >= 6 && we->weight < 250) {
            int nw = (int)we->weight + 20;
            we->weight = nw > 250 ? 250 : (uint8_t)nw;
        }
        ne++;
    }
    wg->n_edges = ne;
    wg->total_learns = (uint32_t)(g->total_learns & 0xFFFFFFFF);

    FILE *f = fopen(path, "wb");
    if (!f) { free(wg); return -2; }
    fwrite(wg, sizeof(TkWireGraph), 1, f);
    fclose(f);

    printf("Wire export: %d nodes, %d edges to '%s'\n", (int)nn, (int)ne, path);
    free(wg);
    return (int)nn;
}

/* ══════════════════════════════════════════════════════════════
 * .xyzt TEXT ASSEMBLY INTERPRETER
 * ══════════════════════════════════════════════════════════════ */

int engine_exec(Engine *eng, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "exec: cannot open '%s'\n", path); return -1; }

    int gx = 0, gy = 0, gz = 0;
    int32_t *grid = NULL;
    int grid_size = 0;

    char line[512];
    int linenum = 0;

    while (fgets(line, sizeof(line), f)) {
        linenum++;
        /* Strip comment and trailing whitespace */
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r' || line[len-1] == ' '))
            line[--len] = '\0';
        if (len == 0) continue;

        if (strncmp(line, "LATTICE ", 8) == 0) {
            if (sscanf(line + 8, "%d %d %d", &gx, &gy, &gz) != 3) {
                fprintf(stderr, "exec:%d: bad LATTICE\n", linenum);
                fclose(f); free(grid); return -2;
            }
            grid_size = gx * gy * gz;
            free(grid);
            grid = (int32_t *)calloc(grid_size, sizeof(int32_t));
            printf("LATTICE %d×%d×%d = %d positions\n", gx, gy, gz, grid_size);
        }
        else if (strncmp(line, "SET ", 4) == 0) {
            int x, y, z, val;
            if (sscanf(line + 4, "%d %d %d %d", &x, &y, &z, &val) != 4) continue;
            if (!grid || x < 0 || x >= gx || y < 0 || y >= gy || z < 0 || z >= gz) continue;
            int idx = x + y * gx + z * gx * gy;
            grid[idx] = val;
        }
        else if (strncmp(line, "XOR ", 4) == 0 || strncmp(line, "AND ", 4) == 0 ||
                 strncmp(line, "OR ", 3) == 0) {
            /* Parse: OP ax ay az  bx by bz -> dx dy dz */
            int ax, ay, az, bx, by, bz, dx, dy, dz;
            const char *p = line + 4;
            if (line[0] == 'O' && line[1] == 'R') p = line + 3;
            while (*p == ' ') p++;
            char arrow[4];
            if (sscanf(p, "%d %d %d %d %d %d %3s %d %d %d",
                        &ax, &ay, &az, &bx, &by, &bz, arrow, &dx, &dy, &dz) != 10) continue;
            if (!grid) continue;
            int ai = ax + ay * gx + az * gx * gy;
            int bi = bx + by * gx + bz * gx * gy;
            int di = dx + dy * gx + dz * gx * gy;
            if (ai < 0 || ai >= grid_size || bi < 0 || bi >= grid_size ||
                di < 0 || di >= grid_size) continue;

            int32_t va = grid[ai], vb = grid[bi];
            if (strncmp(line, "XOR", 3) == 0)     grid[di] = va ^ vb;
            else if (strncmp(line, "AND", 3) == 0) grid[di] = va & vb;
            else                                   grid[di] = va | vb;
        }
        else if (strncmp(line, "RUN ", 4) == 0) {
            int n = atoi(line + 4);
            if (n < 1) n = 1;
            for (int i = 0; i < n; i++)
                engine_tick(eng);
        }
        else if (strncmp(line, "READ ", 5) == 0) {
            int x, y, z;
            if (sscanf(line + 5, "%d %d %d", &x, &y, &z) != 3) continue;
            if (!grid || x < 0 || x >= gx || y < 0 || y >= gy || z < 0 || z >= gz) continue;
            int idx = x + y * gx + z * gx * gy;
            printf("READ (%d,%d,%d) = %d\n", x, y, z, grid[idx]);
        }
        else if (strcmp(line, "PRINT") == 0) {
            if (!grid) continue;
            printf("--- GRID %d×%d×%d ---\n", gx, gy, gz);
            for (int z = 0; z < gz; z++) {
                printf("z=%d:\n", z);
                for (int y = 0; y < gy; y++) {
                    printf("  ");
                    for (int x = 0; x < gx; x++) {
                        int idx = x + y * gx + z * gx * gy;
                        printf("%d ", grid[idx]);
                    }
                    printf("\n");
                }
            }
        }
    }

    fclose(f);
    free(grid);
    printf("exec: %d lines processed\n", linenum);
    return 0;
}
