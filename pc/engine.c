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
    g->nodes = NULL; g->edges = NULL;
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

int graph_wire(Graph *g, int a, int b, int d, uint8_t w, int inter) {
    if (g->n_edges >= MAX_EDGES) return -1;
    if (graph_find_edge(g, a, b, d) >= 0) return -1;
    int id = g->n_edges++;
    Edge *e = &g->edges[id];
    memset(e, 0, sizeof(*e));
    e->src_a = (uint16_t)a;
    e->src_b = (uint16_t)b;
    e->dst = (uint16_t)d;
    e->weight = inter ? apply_fresnel(w, g->nodes[a].Z, g->nodes[d].Z) : w;
    e->intershell = inter ? 1 : 0;
    e->learn_rate = (uint8_t)g->learn_rate;
    return id;
}

int graph_learn(Graph *g) {
    int changed = 0;
    for (int i = 0; i < g->n_edges; i++) {
        Edge *e = &g->edges[i];
        int n = g->nodes[e->src_a].identity.len < g->nodes[e->src_b].identity.len
              ? g->nodes[e->src_a].identity.len : g->nodes[e->src_b].identity.len;
        if (n < 20) continue;
        int corr = bs_corr(&g->nodes[e->src_a].identity, &g->nodes[e->src_b].identity);
        int target = corr * 255 / 100;
        int error = target - (int)e->weight;
        int delta = error * (int)MISMATCH_TAX_NUM / (int)MISMATCH_TAX_DEN;
        if (delta > 0 && e->weight < 255) {
            int nw = (int)e->weight + delta;
            e->weight = nw > 255 ? 255 : (uint8_t)nw;
            changed++;
        } else if (delta < 0 && e->weight > 1) {
            int nw = (int)e->weight + delta;
            e->weight = nw < 1 ? 1 : (uint8_t)nw;
            changed++;
        }
    }
    for (int i = 0; i < g->n_nodes; i++)
        if (g->nodes[i].alive) crystal_update(&g->nodes[i], g->edges, g->n_edges, i);
    g->total_learns += changed;
    return changed;
}

int graph_compute_z(Graph *g) {
    if (g->n_nodes == 0) return 0;
    int *z_level = (int *)calloc(g->n_nodes, sizeof(int));
    uint8_t *bidir = (uint8_t *)calloc(g->n_edges, sizeof(uint8_t));

    for (int e = 0; e < g->n_edges; e++) {
        if (g->edges[e].weight == 0) continue;
        int d = g->edges[e].dst, sa = g->edges[e].src_a;
        for (int j = 0; j < g->n_edges; j++) {
            if (j == e) continue;
            if (g->edges[j].src_a == d && g->edges[j].dst == sa) {
                bidir[e] = bidir[j] = 1;
                break;
            }
        }
    }

    int changed = 1;
    for (int iter = 0; iter < 100 && changed; iter++) {
        changed = 0;
        for (int e = 0; e < g->n_edges; e++) {
            if (g->edges[e].weight == 0 || bidir[e]) continue;
            int sa = g->edges[e].src_a, sb = g->edges[e].src_b, d = g->edges[e].dst;
            if (sa == d || sb == d) continue;
            int max_src = z_level[sa] > z_level[sb] ? z_level[sa] : z_level[sb];
            if (z_level[d] < max_src + 1) {
                z_level[d] = max_src + 1;
                changed = 1;
            }
        }
    }

    int max_z = 0;
    for (int i = 0; i < g->n_nodes; i++) {
        Coord c = g->nodes[i].coord;
        g->nodes[i].coord = coord_pack(coord_x(c), coord_y(c), z_level[i]);
        if (z_level[i] > max_z) max_z = z_level[i];
    }

    free(z_level);
    free(bidir);
    return max_z;
}

/* ══════════════════════════════════════════════════════════════
 * NESTING — systems containing systems
 * ══════════════════════════════════════════════════════════════ */

static int child_tick_once(Graph *g) {
    int changed = 0;
    g->total_ticks++;
    for (int i = 0; i < g->n_edges; i++) {
        Edge *e = &g->edges[i];
        if (e->weight == 0) continue;
        Node *na = &g->nodes[e->src_a], *nb = &g->nodes[e->src_b], *nd = &g->nodes[e->dst];
        if (!na->alive || na->layer_zero || na->identity.len < 1) continue;
        if (!nb->alive || nb->layer_zero || nb->identity.len < 1) continue;
        int32_t va = e->invert_a ? -na->val : na->val;
        int32_t vb = e->invert_b ? -nb->val : nb->val;
        nd->accum += va + vb;
        nd->n_incoming++;
    }
    for (int i = 0; i < g->n_nodes; i++) {
        Node *n = &g->nodes[i];
        if (!n->alive || n->layer_zero) continue;
        if (n->n_incoming == 0 && n->accum == 0) continue;
        int32_t old_val = n->val;
        if (n->n_incoming <= 1) {
            n->val = n->accum;
        } else {
            int32_t total = n->accum + n->val;
            int N = n->n_incoming + 1;
            n->val = 2 * total / N - old_val;
        }
        if (n->valence > 0) {
            n->val = (int32_t)((int64_t)old_val * n->valence +
                               (int64_t)n->val * (255 - n->valence)) / 255;
        }
        int32_t cap = abs(old_val) + abs(n->accum);
        if (abs(n->val) > cap) n->val = n->val > 0 ? cap : -cap;
        if (n->val != old_val) changed = 1;
        n->n_incoming = 0;
        n->accum = 0;
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

    /* 8 retina input nodes (one per octant of parent's 4^3 cube) + 1 output.
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
        }
    }
    int out = graph_add(child, "output", 0, &eng->T);
    if (out >= 0) {
        child->nodes[out].alive = 1;
        child->nodes[out].layer_zero = 0;
        child->nodes[out].identity.len = 64;
        child->nodes[out].Z = owner->Z;
    }

    /* Wire retina pairs to output: (r0,r1)→out, (r2,r3)→out, etc.
     * Paired octants are spatial opposites — natural 2-input edges. */
    for (int r = 0; r < 8; r += 2)
        graph_wire(child, r, r + 1, 8, 128, 0);

    eng->child_owner[slot] = node_id;
    eng->n_children++;
    owner->child_id = (int8_t)slot;
    return slot;
}

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

    /* Auto-wire: top-K by mutual containment */
    if (g0->auto_grow) {
        int thresh = temp_grow;
        int top_j[GROW_K], top_c[GROW_K], n_top = 0;
        for (int i = 0; i < g0->n_nodes; i++) {
            if (i == id0 || !g0->nodes[i].alive || g0->nodes[i].identity.len < 16) continue;
            int corr = bs_mutual_contain(data, &g0->nodes[i].identity);
            if (corr <= thresh) continue;
            if (n_top < GROW_K) {
                top_j[n_top] = i; top_c[n_top] = corr; n_top++;
            } else {
                int min_k = 0;
                for (int k = 1; k < GROW_K; k++)
                    if (top_c[k] < top_c[min_k]) min_k = k;
                if (corr > top_c[min_k]) { top_j[min_k] = i; top_c[min_k] = corr; }
            }
        }
        for (int k = 0; k < n_top; k++) {
            graph_wire(g0, id0, top_j[k], id0, (uint8_t)(top_c[k] * 255 / 100), 0);
            graph_wire(g0, top_j[k], id0, top_j[k], (uint8_t)(top_c[k] * 255 / 100), 0);
            g0->total_grown += 2;
        }
    }

    /* Mirror to shell 1 */
    int id1 = graph_add(g1, name, 1, &eng->T);
    if (id1 >= 0) {
        g1->nodes[id1].identity = *data;
        g1->nodes[id1].hit_count++;
        g1->nodes[id1].val = onetwo_val;
        if (eng->n_boundary_edges < eng->max_boundary_edges) {
            Edge *be = &eng->boundary_edges[eng->n_boundary_edges++];
            memset(be, 0, sizeof(*be));
            be->src_a = (uint16_t)id0;
            be->src_b = (uint16_t)id0;
            be->dst = (uint16_t)id1;
            be->weight = apply_fresnel(255, g0->nodes[id0].Z, g1->nodes[id1].Z);
            be->intershell = 1;
        }
        g0->nodes[id0].layer_zero = 0;
        g1->nodes[id1].layer_zero = 0;
        g0->total_boundary_crossings++;
        if (g0->nodes[id0].valence < 255) g0->nodes[id0].valence++;
        if (g1->nodes[id1].valence < 255) g1->nodes[id1].valence++;

        /* Auto-wire within shell 1 */
        if (g1->auto_grow) {
            int top_j1[GROW_K], top_c1[GROW_K], n_top1 = 0;
            for (int i = 0; i < g1->n_nodes; i++) {
                if (i == id1 || !g1->nodes[i].alive || g1->nodes[i].identity.len < 16) continue;
                int corr = bs_mutual_contain(data, &g1->nodes[i].identity);
                if (corr <= g1->grow_mean) continue;
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
                graph_wire(g1, id1, top_j1[k], id1, (uint8_t)(top_c1[k] * 255 / 100), 0);
                graph_wire(g1, top_j1[k], id1, top_j1[k], (uint8_t)(top_c1[k] * 255 / 100), 0);
                g1->total_grown += 2;
            }
        }
    }

    ot_sys_ingest(&eng->onetwo, (const uint8_t *)data->w, data->len / 8);
    return id0;
}

int engine_ingest_text(Engine *eng, const char *name, const char *text) {
    BitStream bs;
    onetwo_parse((const uint8_t *)text, strlen(text), &bs);
    return engine_ingest(eng, name, &bs);
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

void engine_tick(Engine *eng) {
    T_tick(&eng->T);
    eng->total_ticks++;

    /* v9: nesting check */
    if (eng->total_ticks % SUBSTRATE_INT == 0)
        nest_check(eng);

    /* Boundary propagation */
    for (int i = 0; i < eng->n_boundary_edges; i++) {
        Edge *be = &eng->boundary_edges[i];
        if (be->weight == 0) continue;
        Graph *g0 = &eng->shells[0].g, *g1 = &eng->shells[1].g;
        if (be->src_a >= (uint16_t)g0->n_nodes || be->dst >= (uint16_t)g1->n_nodes) continue;
        Node *src = &g0->nodes[be->src_a], *dst = &g1->nodes[be->dst];
        if (src->layer_zero || src->identity.len < 1) continue;

        int prob = be->weight;
        double K = dst->Z / (src->Z > 0 ? src->Z : 1.0);
        prob = (int)(prob * fresnel_T(K) + 0.5);
        if (prob >= 255 || (rand() % 255) < prob) {
            dst->accum += src->val;
            dst->n_incoming++;
            dst->layer_zero = 0;
            dst->last_active = (uint32_t)T_now(&eng->T);
            g0->total_boundary_crossings++;
        } else {
            int rprob = 255 - be->weight;
            if (rprob > 0 && (rand() % 255) < rprob) {
                src->accum += dst->val;
                src->n_incoming++;
            }
        }
    }

    /* Per-shell processing */
    for (int s = 0; s < eng->n_shells; s++) {
        Graph *g = &eng->shells[s].g;
        g->total_ticks++;

        /* Z-ordered propagate + resolve */
        int max_z = 0;
        for (int i = 0; i < g->n_nodes; i++)
            if (g->nodes[i].alive) {
                int nz = coord_z(g->nodes[i].coord);
                if (nz > max_z) max_z = nz;
            }

        for (int zl = 0; zl <= max_z; zl++) {
            /* Propagate */
            for (int i = 0; i < g->n_edges; i++) {
                Edge *e = &g->edges[i];
                if (e->weight == 0) continue;
                Node *na = &g->nodes[e->src_a], *nb = &g->nodes[e->src_b], *nd = &g->nodes[e->dst];
                if (coord_z(nd->coord) != zl) continue;
                if (na->layer_zero || nb->layer_zero) continue;
                if (na->identity.len < 1 || nb->identity.len < 1) continue;

                int prob = e->weight;
                if (e->intershell) {
                    double K = nd->Z / (na->Z > 0 ? na->Z : 1.0);
                    prob = (int)(prob * fresnel_T(K) + 0.5);
                }
                if (prob >= 255 || (rand() % 255) < prob) {
                    int32_t va = e->invert_a ? -na->val : na->val;
                    int32_t vb = e->invert_b ? -nb->val : nb->val;
                    nd->accum += va + vb;
                    nd->n_incoming++;
                    if (e->weight < 255) e->weight++;
                } else {
                    int rprob = 255 - e->weight;
                    if (rprob > 0 && (rand() % 255) < rprob) {
                        na->accum += nd->val;
                        na->n_incoming++;
                    }
                }
            }

            /* Resolve */
            for (int i = 0; i < g->n_nodes; i++) {
                Node *n = &g->nodes[i];
                if (coord_z(n->coord) != zl) continue;
                if (!n->alive || n->layer_zero) continue;
                if (n->n_incoming == 0 && n->accum == 0) continue;

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
                            child->nodes[r].val = n->accum / n_inp;
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
                    n->val = n->accum;
                } else {
                    int32_t total = n->accum + n->val;
                    int N = n->n_incoming + 1;
                    n->val = 2 * total / N - old_val;
                }
                if (n->valence > 0) {
                    n->val = (int32_t)((int64_t)old_val * n->valence +
                                       (int64_t)n->val * (255 - n->valence)) / 255;
                }
                int32_t cap = abs(old_val) + abs(n->accum);
                if (abs(n->val) > cap) n->val = n->val > 0 ? cap : -cap;
                n->last_active = (uint32_t)T_now(&eng->T);
                n->n_incoming = 0; n->accum = 0;
            }
        }

        /* Safety clear */
        for (int i = 0; i < g->n_nodes; i++) {
            if (g->nodes[i].n_incoming > 0 || g->nodes[i].accum != 0) {
                g->nodes[i].n_incoming = 0;
                g->nodes[i].accum = 0;
            }
        }

        /* Grow phase */
        if (g->auto_grow && g->grow_interval > 0 && (g->total_ticks % (unsigned)g->grow_interval == 0)) {
            long long mc_sum = 0; int mc_count = 0, total_grown = 0;
            for (int i = 0; i < g->n_nodes; i++) {
                if (!g->nodes[i].alive || g->nodes[i].layer_zero || g->nodes[i].identity.len < 16) continue;
                int top_j[GROW_K], top_c[GROW_K], n_top = 0;
                for (int j = 0; j < g->n_nodes; j++) {
                    if (i == j || !g->nodes[j].alive || g->nodes[j].layer_zero || g->nodes[j].identity.len < 16) continue;
                    int corr = bs_mutual_contain(&g->nodes[i].identity, &g->nodes[j].identity);
                    mc_sum += corr; mc_count++;
                    if (corr <= g->grow_mean) continue;
                    if (n_top < GROW_K) {
                        top_j[n_top] = j; top_c[n_top] = corr; n_top++;
                    } else {
                        int min_k = 0;
                        for (int k = 1; k < GROW_K; k++)
                            if (top_c[k] < top_c[min_k]) min_k = k;
                        if (corr > top_c[min_k]) { top_j[min_k] = j; top_c[min_k] = corr; }
                    }
                }
                for (int k = 0; k < n_top; k++) {
                    if (graph_find_edge(g, i, top_j[k], i) >= 0) continue;
                    graph_wire(g, i, top_j[k], i, (uint8_t)(top_c[k] * 255 / 100), 0);
                    graph_wire(g, top_j[k], i, top_j[k], (uint8_t)(top_c[k] * 255 / 100), 0);
                    g->total_grown += 2; total_grown += 2;
                }
            }
            if (mc_count > 0) g->grow_mean = (int)(mc_sum / mc_count);
            g->grow_interval = adaptive_interval(g->grow_interval, total_grown, 2, 200);
        }

        /* Prune phase */
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

        /* T-driven decay */
        for (int i = 0; i < g->n_nodes; i++) {
            Node *n = &g->nodes[i];
            if (!n->alive || n->layer_zero) continue;
            uint32_t age = (uint32_t)T_now(&eng->T) - n->last_active;
            if (age > SUBSTRATE_INT * 3) {
                if (n->valence > 0) n->valence--;
                for (int e = 0; e < g->n_edges; e++)
                    if (g->edges[e].src_a == i || g->edges[e].src_b == i) {
                        int nw = (int)g->edges[e].weight - 1;
                        g->edges[e].weight = nw < 1 ? 1 : (uint8_t)nw;
                    }
            }
        }
    }

    /* Dynamic intershell wiring */
    if (eng->boundary_interval > 0 && eng->total_ticks % (unsigned)eng->boundary_interval == 0 && eng->n_shells >= 2) {
        int before = eng->n_boundary_edges;
        Graph *g0 = &eng->shells[0].g, *g1 = &eng->shells[1].g;
        for (int i = 0; i < g0->n_nodes; i++) {
            if (!g0->nodes[i].alive || g0->nodes[i].layer_zero || g0->nodes[i].identity.len < 16) continue;
            for (int j = 0; j < g1->n_nodes; j++) {
                if (!g1->nodes[j].alive || g1->nodes[j].layer_zero || g1->nodes[j].identity.len < 16) continue;
                int exists = 0;
                for (int b = 0; b < eng->n_boundary_edges; b++) {
                    if (eng->boundary_edges[b].src_a == (uint16_t)i &&
                        eng->boundary_edges[b].dst == (uint16_t)j) { exists = 1; break; }
                }
                if (exists) continue;
                int corr = bs_mutual_contain(&g0->nodes[i].identity, &g1->nodes[j].identity);
                if (corr > g0->grow_mean && eng->n_boundary_edges < eng->max_boundary_edges) {
                    Edge *be = &eng->boundary_edges[eng->n_boundary_edges++];
                    memset(be, 0, sizeof(*be));
                    be->src_a = (uint16_t)i; be->src_b = (uint16_t)i; be->dst = (uint16_t)j;
                    be->weight = apply_fresnel((uint8_t)(corr * 255 / 100), g0->nodes[i].Z, g1->nodes[j].Z);
                    be->intershell = 1;
                }
            }
        }
        eng->boundary_interval = adaptive_interval(eng->boundary_interval, eng->n_boundary_edges - before, 10, 500);
    }

    /* Hebbian + Z */
    if (eng->learn_interval > 0 && eng->total_ticks % (unsigned)eng->learn_interval == 0) {
        for (int s = 0; s < eng->n_shells; s++) {
            graph_learn(&eng->shells[s].g);
            graph_compute_z(&eng->shells[s].g);
        }
    }

    /* ONETWO feedback loop */
    if (eng->total_ticks % SUBSTRATE_INT == 0) {
        uint8_t state_buf[512];
        int pos = 0;

        /* Observe structural state, not raw vals.
         * Edge weights change by +1/-1 (Hebbian) → monotonic runs, constant deltas.
         * Valence increments by 1 → predictable sequences.
         * ONETWO's autocorrelation and run-length bands detect these patterns.
         * Raw vals (millions, oscillating) look like noise to the encoder. */
        for (int s = 0; s < eng->n_shells && pos < 400; s++) {
            Graph *g = &eng->shells[s].g;
            /* Edge weight distribution (sorted-ish via traversal order) */
            for (int e = 0; e < g->n_edges && pos < 400; e++) {
                state_buf[pos++] = g->edges[e].weight;
            }
            /* Node valence and crystal strength */
            for (int i = 0; i < g->n_nodes && pos < 500; i++) {
                if (g->nodes[i].alive && !g->nodes[i].layer_zero) {
                    state_buf[pos++] = g->nodes[i].valence;
                    if (pos < 500)
                        state_buf[pos++] = (uint8_t)crystal_strength(&g->nodes[i]);
                }
            }
        }
        if (pos > 0) ot_sys_ingest(&eng->onetwo, state_buf, pos);

        int32_t error = eng->onetwo.feedback[7];
        int32_t stability = eng->onetwo.feedback[4];
        int32_t energy = eng->onetwo.feedback[5];
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
            eng->low_error_run = 0;
        } else {
            eng->low_error_run++;
        }

        if (stability && error_mag < energy / 4) {
            for (int s = 0; s < eng->n_shells; s++) {
                Graph *g = &eng->shells[s].g;
                for (int i = 0; i < g->n_nodes; i++) {
                    Node *n = &g->nodes[i];
                    if (!n->alive || n->layer_zero) continue;
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
                    int nw = (int)be->weight + 1;
                    be->weight = nw > 255 ? 255 : (uint8_t)nw;
                } else {
                    int nw = (int)be->weight - 1;
                    be->weight = nw < 1 ? 1 : (uint8_t)nw;
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
 * SAVE / LOAD (binary format, v9 compatible)
 * ══════════════════════════════════════════════════════════════ */

int engine_save(const Engine *eng, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    uint32_t magic = 0x58595A54; /* XYZT */
    uint32_t version = 10;       /* v10 = PC engine format */
    fwrite(&magic, 4, 1, f);
    fwrite(&version, 4, 1, f);
    /* v10 header: n_shells, n_boundary_edges, total_ticks (matches v9 field order) */
    uint32_t ns = (uint32_t)eng->n_shells;
    uint32_t nb = (uint32_t)eng->n_boundary_edges;
    fwrite(&ns, 4, 1, f);
    fwrite(&nb, 4, 1, f);
    fwrite(&eng->total_ticks, sizeof(eng->total_ticks), 1, f);
    for (int s = 0; s < eng->n_shells; s++) {
        const Graph *g = &eng->shells[s].g;
        fwrite(&g->n_nodes, sizeof(g->n_nodes), 1, f);
        for (int i = 0; i < g->n_nodes; i++)
            fwrite(&g->nodes[i], sizeof(Node), 1, f);
        fwrite(&g->n_edges, sizeof(g->n_edges), 1, f);
        for (int i = 0; i < g->n_edges; i++)
            fwrite(&g->edges[i], sizeof(Edge), 1, f);
        fwrite(&g->grow_mean, sizeof(g->grow_mean), 1, f);
    }
    for (int i = 0; i < eng->n_boundary_edges; i++)
        fwrite(&eng->boundary_edges[i], sizeof(Edge), 1, f);
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
            /* v9 bulk reads */
            for (int i = 0; i < g->n_nodes; i++)
                if (fread(&g->nodes[i], sizeof(Node), 1, f) != 1) { fclose(f); return -4; }
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
            for (int i = 0; i < g->n_nodes; i++)
                fread(&g->nodes[i], sizeof(Node), 1, f);
            fread(&g->n_edges, sizeof(g->n_edges), 1, f);
            if (g->n_edges > MAX_EDGES) g->n_edges = MAX_EDGES;
            for (int i = 0; i < g->n_edges; i++)
                fread(&g->edges[i], sizeof(Edge), 1, f);
            fread(&g->grow_mean, sizeof(g->grow_mean), 1, f);
        }
        for (int i = 0; i < eng->n_boundary_edges; i++)
            fread(&eng->boundary_edges[i], sizeof(Edge), 1, f);
    } else {
        fprintf(stderr, "Unknown .xyzt version %u\n", version);
        fclose(f);
        return -1;
    }

    fclose(f);
    return 0;
}

/* ══════════════════════════════════════════════════════════════
 * WIRE GRAPH BRIDGE — toolkit wire.bin ↔ engine graph
 * ══════════════════════════════════════════════════════════════ */

/* Toolkit wire.bin format (from tools engine wire.h v3) */
#define TK_WIRE_MAX_NODES 512
#define TK_WIRE_MAX_EDGES 4096
#define TK_WIRE_NAME_LEN  128
#define TK_WIRE_MAGIC     0x57495245  /* "WIRE" */
#define TK_WIRE_VER       3
#define TK_WIRE_ALIVE     128

#pragma pack(push, 1)
typedef struct {
    char     name[TK_WIRE_NAME_LEN];
    uint8_t  type;       /* 0=file, 1=problem, 2=concept */
    uint8_t  _pad[3];
    uint32_t created;
    uint32_t last_active;
    uint32_t hit_count;
} TkWireNode;  /* 144 bytes */

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
} TkWireGraph;  /* 155,668 bytes */
#pragma pack(pop)

int engine_wire_import(Engine *eng, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    TkWireGraph *wg = (TkWireGraph *)calloc(1, sizeof(TkWireGraph));
    if (!wg) { fclose(f); return -2; }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    size_t to_read = (size_t)fsize;
    if (to_read > sizeof(TkWireGraph)) to_read = sizeof(TkWireGraph);
    if (fread(wg, 1, to_read, f) < 16 || wg->magic != TK_WIRE_MAGIC) {
        free(wg); fclose(f); return -3;
    }
    fclose(f);

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
        wn->created = g->nodes[i].created;
        wn->last_active = g->nodes[i].last_active;
        wn->hit_count = g->nodes[i].hit_count;
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
