/*
 * sense.c — Temporal feature extraction for PC engine
 *
 * Port of shared/sense.c for byte-stream input (no GPIO).
 * Each byte's 8 bit positions = 8 channels.
 * Detects transitions, timing, correlations in the byte stream.
 * Creates "s:"-prefixed graph nodes in shell 0.
 *
 * The engine observes its own state_buf (edge weights, valence)
 * and grows structure from the observation. That IS the computation.
 *
 * Isaac Oravec & Claude, February 2026
 */

#include "sense.h"
#include "onetwo.h"
#include <string.h>

/* ── Feature types ─────────────────────────────────────── */
#define FEAT_ACTIVE       0
#define FEAT_REGULARITY   1
#define FEAT_DUTY         2
#define FEAT_CORRELATION  3
#define FEAT_ASYMMETRY    4
#define FEAT_BURST        5
#define FEAT_PERIOD       6

#define PERIOD_FAST    0
#define PERIOD_MED     1
#define PERIOD_SLOW    2
#define PERIOD_VSLOW   3

/* ── Thresholds (adapted for byte-stream timescale) ──── */
#define ACTIVE_THRESH     4
#define REG_TOLERANCE_NUM 1
#define REG_TOLERANCE_DEN 4
#define DUTY_LO_NUM  35
#define DUTY_HI_NUM  65
#define DUTY_DEN    100
#define CORR_WINDOW   3     /* samples, not microseconds */
#define W_DECAY       2

/* ── Feature naming ──────────────────────────────────── */
static void feat_name(char *buf, int type, int ch_a, int ch_b) {
    const char tags[] = "ARDCSBP";
    char t = (type >= 0 && type <= 6) ? tags[type] : '?';
    int i = 0;
    buf[i++] = 's'; buf[i++] = ':';  /* prefix to avoid collisions */
    buf[i++] = t;
    buf[i++] = '0' + ch_a;
    if (type == FEAT_CORRELATION || type == FEAT_ASYMMETRY || type == FEAT_PERIOD) {
        buf[i++] = '.';
        buf[i++] = '0' + ch_b;
    }
    buf[i] = '\0';
}

/* Region-aware naming: appends .N suffix for pass_id > 0 */
static void feat_name_region(char *buf, int type, int ch_a, int ch_b, int pass_id) {
    feat_name(buf, type, ch_a, ch_b);
    if (pass_id > 0) {
        int i = (int)strlen(buf);
        buf[i++] = '.';
        buf[i++] = '0' + pass_id;
        buf[i] = '\0';
    }
}

/* ── Add feature node to graph ───────────────────────── */
static void add_feature(Engine *eng, sense_result_t *result, const char *name) {
    if (result->n_features >= SENSE_MAX_FEATS) return;
    Graph *g = &eng->shells[0].g;
    int id = graph_add(g, name, 0, &eng->T);
    if (id < 0) return;

    Node *n = &g->nodes[id];
    n->layer_zero = 1;  /* sense nodes observe, they don't crystallize */
    n->hit_count++;
    n->last_active = (uint32_t)T_now(&eng->T);

    /* Build identity from name so node participates in grow phase */
    if (n->identity.len == 0) {
        encode_bytes(&n->identity, (const uint8_t *)name, (int)strlen(name));
    }

    result->node_ids[result->n_features++] = id;
}

/* ══════════════════════════════════════════════════════════
 * SENSE FILL — byte buffer → event array
 * ══════════════════════════════════════════════════════════ */

void sense_fill(sense_buf_t *buf, const uint8_t *data, int len) {
    buf->count = 0;
    int cap = len < SENSE_MAX_EVENTS ? len : SENSE_MAX_EVENTS;
    for (int i = 0; i < cap; i++) {
        buf->events[i].value = data[i];
        buf->events[i].timestamp = (uint64_t)i;
        buf->count++;
    }
}

/* ══════════════════════════════════════════════════════════
 * SENSE EXTRACT — 7 structural features from byte stream
 *
 * pass_id == 0: backward compatible (no suffix on names)
 * pass_id > 0:  appends .N suffix for windowed extraction
 * When pass_id > 0, does NOT reset n_features (accumulates).
 * ══════════════════════════════════════════════════════════ */

static void sense_extract_inner(Engine *eng, sense_buf_t *buf,
                                sense_result_t *result, int pass_id) {
    if (pass_id == 0) result->n_features = 0;
    if (buf->count < 2) return;

    uint32_t n = buf->count;
    uint32_t trans[SENSE_CHANNELS];
    uint32_t high_samples[SENSE_CHANNELS];

    for (int ch = 0; ch < SENSE_CHANNELS; ch++) {
        trans[ch] = 0; high_samples[ch] = 0;
    }

    for (uint32_t i = 0; i < n; i++) {
        uint8_t val = buf->events[i].value;
        for (int ch = 0; ch < SENSE_CHANNELS; ch++) {
            if (val & (1 << ch)) high_samples[ch]++;
            if (i > 0) {
                uint8_t prev = buf->events[i-1].value;
                if ((val ^ prev) & (1 << ch)) trans[ch]++;
            }
        }
    }

    char name[16];
    int active_ch[SENSE_CHANNELS];
    int n_active = 0;
    int feat_start = result->n_features;  /* track region start for Hebbian */

    /* ACTIVE */
    for (int ch = 0; ch < SENSE_CHANNELS; ch++) {
        active_ch[ch] = 0;
        if (trans[ch] >= ACTIVE_THRESH) {
            active_ch[ch] = 1;
            n_active++;
            feat_name_region(name, FEAT_ACTIVE, ch, 0, pass_id);
            add_feature(eng, result, name);
        }
    }
    if (n_active == 0) return;

    /* DUTY */
    for (int ch = 0; ch < SENSE_CHANNELS; ch++) {
        if (!active_ch[ch]) continue;
        uint32_t h100 = high_samples[ch] * DUTY_DEN;
        if (h100 >= n * DUTY_LO_NUM && h100 <= n * DUTY_HI_NUM) {
            feat_name_region(name, FEAT_DUTY, ch, 0, pass_id);
            add_feature(eng, result, name);
        }
    }

    /* REGULARITY + PERIOD */
    for (int ch = 0; ch < SENSE_CHANNELS; ch++) {
        if (!active_ch[ch] || trans[ch] < 6) continue;
        uint64_t sum_dt = 0, prev_t = 0;
        int count = 0, first = 1;
        for (uint32_t i = 1; i < n; i++) {
            if ((buf->events[i].value ^ buf->events[i-1].value) & (1 << ch)) {
                if (!first) { sum_dt += buf->events[i].timestamp - prev_t; count++; }
                prev_t = buf->events[i].timestamp;
                first = 0;
            }
        }
        if (count < 3) continue;
        uint64_t mean = sum_dt / (uint64_t)count;
        if (mean == 0) continue;

        int regular = 1;
        prev_t = 0; first = 1;
        for (uint32_t i = 1; i < n && regular; i++) {
            if ((buf->events[i].value ^ buf->events[i-1].value) & (1 << ch)) {
                if (!first) {
                    uint64_t dt = buf->events[i].timestamp - prev_t;
                    uint64_t diff = (dt > mean) ? (dt - mean) : (mean - dt);
                    if (diff * REG_TOLERANCE_DEN > mean * REG_TOLERANCE_NUM) regular = 0;
                }
                prev_t = buf->events[i].timestamp;
                first = 0;
            }
        }
        if (regular) {
            feat_name_region(name, FEAT_REGULARITY, ch, 0, pass_id);
            add_feature(eng, result, name);
            int bucket;
            if (mean < 4)       bucket = PERIOD_FAST;
            else if (mean < 16) bucket = PERIOD_MED;
            else if (mean < 64) bucket = PERIOD_SLOW;
            else                bucket = PERIOD_VSLOW;
            feat_name_region(name, FEAT_PERIOD, ch, bucket, pass_id);
            add_feature(eng, result, name);
        }
    }

    /* CORRELATION */
    for (int p = 0; p < SENSE_CHANNELS; p++) {
        if (!active_ch[p]) continue;
        for (int q = p + 1; q < SENSE_CHANNELS; q++) {
            if (!active_ch[q]) continue;
            int corr_count = 0, p_trans = 0;
            for (uint32_t i = 1; i < n; i++) {
                uint8_t edge = buf->events[i].value ^ buf->events[i-1].value;
                if (!(edge & (1 << p))) continue;
                p_trans++;
                uint32_t lo = (i > CORR_WINDOW) ? i - CORR_WINDOW : 1;
                uint32_t hi = (i + CORR_WINDOW < n) ? i + CORR_WINDOW : n - 1;
                for (uint32_t j = lo; j <= hi; j++) {
                    if (j == 0) continue;
                    uint8_t e2 = buf->events[j].value ^ buf->events[j-1].value;
                    if (e2 & (1 << q)) { corr_count++; break; }
                }
            }
            if (p_trans > 0 && corr_count * 2 > p_trans) {
                feat_name_region(name, FEAT_CORRELATION, p, q, pass_id);
                add_feature(eng, result, name);
            }
        }
    }

    /* ASYMMETRY */
    for (int ch = 0; ch < SENSE_CHANNELS; ch++) {
        if (high_samples[ch] * 100 < n * 80) continue;
        if (trans[ch] > ACTIVE_THRESH * 2) continue;
        for (int q = 0; q < SENSE_CHANNELS; q++) {
            if (ch == q || !active_ch[q]) continue;
            feat_name_region(name, FEAT_ASYMMETRY, ch, q, pass_id);
            add_feature(eng, result, name);
        }
    }

    /* BURST */
    for (int ch = 0; ch < SENSE_CHANNELS; ch++) {
        if (!active_ch[ch] || trans[ch] < 6) continue;
        uint64_t gaps[256];
        int n_gaps = 0;
        uint64_t prev_t = 0;
        int first = 1;
        for (uint32_t i = 1; i < n && n_gaps < 256; i++) {
            if (!((buf->events[i].value ^ buf->events[i-1].value) & (1 << ch))) continue;
            if (!first) gaps[n_gaps++] = buf->events[i].timestamp - prev_t;
            prev_t = buf->events[i].timestamp;
            first = 0;
        }
        if (n_gaps >= 4) {
            /* Sort gaps for median */
            for (int si = 1; si < n_gaps; si++) {
                uint64_t key = gaps[si];
                int sj = si - 1;
                while (sj >= 0 && gaps[sj] > key) { gaps[sj+1] = gaps[sj]; sj--; }
                gaps[sj+1] = key;
            }
            uint64_t median = gaps[n_gaps / 2];
            uint64_t max_gap = gaps[n_gaps - 1];
            if (median > 0 && max_gap > median * 4) {
                feat_name_region(name, FEAT_BURST, ch, 0, pass_id);
                add_feature(eng, result, name);
            }
        }
    }

    /* ── HEBBIAN WIRING: co-occurring features within this region ── */
    Graph *g = &eng->shells[0].g;
    for (int i = feat_start; i < result->n_features; i++) {
        for (int j = i + 1; j < result->n_features; j++) {
            int a = result->node_ids[i], b = result->node_ids[j];
            if (graph_find_edge(g, a, b, a) < 0)
                graph_wire(g, a, b, a, 128, 0);
        }
    }
}

/* Public wrapper: backward compatible (no pass suffix) */
void sense_extract(Engine *eng, sense_buf_t *buf, sense_result_t *result) {
    sense_extract_inner(eng, buf, result, 0);
}

/* ══════════════════════════════════════════════════════════
 * SENSE DECAY — weaken edges of absent sense nodes
 * ══════════════════════════════════════════════════════════ */

void sense_decay(Engine *eng, const sense_result_t *result) {
    Graph *g = &eng->shells[0].g;
    for (int i = 0; i < g->n_nodes; i++) {
        if (!g->nodes[i].alive) continue;
        /* Only decay sense nodes (s: prefix) */
        if (g->nodes[i].name[0] != 's' || g->nodes[i].name[1] != ':') continue;

        /* Was this node in the current result? */
        int found = 0;
        for (int j = 0; j < result->n_features; j++)
            if (result->node_ids[j] == i) { found = 1; break; }

        if (!found && g->nodes[i].hit_count > 0) {
            for (int e = 0; e < g->n_edges; e++) {
                Edge *edge = &g->edges[e];
                if ((int)edge->src_a == i || (int)edge->src_b == i || (int)edge->dst == i) {
                    int nw = (int)edge->weight - W_DECAY;
                    edge->weight = (nw < 0) ? 0 : (uint8_t)nw;
                }
            }
        }
    }
}

/* ══════════════════════════════════════════════════════════
 * SENSE FEEDBACK — GPU co-presence → valence
 * ══════════════════════════════════════════════════════════ */

void sense_feedback(Engine *eng, const sense_result_t *result) {
    Graph *g = &eng->shells[0].g;
    for (int i = 0; i < result->n_features; i++) {
        int id = result->node_ids[i];
        if (id < 0 || id >= g->n_nodes) continue;
        Node *n = &g->nodes[id];
        if (!n->alive) continue;
        if (n->val != 0) {
            if (n->valence < 255) n->valence++;
        } else {
            if (n->valence > 0) n->valence--;
        }
    }
}

/* ══════════════════════════════════════════════════════════
 * SENSE PASS — convenience: fill + extract + decay
 * ══════════════════════════════════════════════════════════ */

void sense_pass(Engine *eng, const uint8_t *data, int len, sense_result_t *result) {
    sense_buf_t buf;
    sense_fill(&buf, data, len);
    sense_extract(eng, &buf, result);
    sense_decay(eng, result);
}

/* ══════════════════════════════════════════════════════════
 * SENSE EXTRACT REGION — single region with pass_id suffix
 * ══════════════════════════════════════════════════════════ */

void sense_extract_region(Engine *eng, const uint8_t *data, int len,
                          int pass_id, sense_result_t *result) {
    sense_buf_t buf;
    sense_fill(&buf, data, len);
    sense_extract_inner(eng, &buf, result, pass_id);
}

/* ══════════════════════════════════════════════════════════
 * SENSE PASS WINDOWED — per-region extraction + cross-wire
 *
 * 1. Init result
 * 2. For each region: extract features (accumulates into result)
 * 3. Cross-region Hebbian wiring (weight=64, half of within-region 128)
 * 4. Single decay pass
 * ══════════════════════════════════════════════════════════ */

void sense_pass_windowed(Engine *eng, const uint8_t *data,
                         const sense_region_t *regions, int n_regions,
                         sense_result_t *result) {
    result->n_features = 0;

    /* Track where each region's features start/end */
    int region_start[SENSE_MAX_REGIONS];
    int region_end[SENSE_MAX_REGIONS];

    for (int r = 0; r < n_regions && r < SENSE_MAX_REGIONS; r++) {
        region_start[r] = result->n_features;
        sense_extract_region(eng, data + regions[r].offset, regions[r].length,
                             regions[r].pass_id, result);
        region_end[r] = result->n_features;
    }

    /* Cross-region Hebbian wiring: weight=64 (half of within-region 128) */
    Graph *g = &eng->shells[0].g;
    for (int r1 = 0; r1 < n_regions && r1 < SENSE_MAX_REGIONS; r1++) {
        for (int r2 = r1 + 1; r2 < n_regions && r2 < SENSE_MAX_REGIONS; r2++) {
            for (int i = region_start[r1]; i < region_end[r1]; i++) {
                for (int j = region_start[r2]; j < region_end[r2]; j++) {
                    int a = result->node_ids[i], b = result->node_ids[j];
                    if (graph_find_edge(g, a, b, a) < 0)
                        graph_wire(g, a, b, a, 64, 0);
                }
            }
        }
    }

    /* Single decay pass over all accumulated features */
    sense_decay(eng, result);
}
