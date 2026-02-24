/*
 * sense.c — Structural Feature Extraction + Hebbian Learning
 *
 * REPLACES: detect_protocol(), sense_decompose() hardcoded heuristics
 *
 * The system is not told what a protocol is.
 * It observes voltage transitions on a wire.
 * It extracts structural features (regularity, correlation, duty).
 * Features become wire graph nodes — unnamed.
 * Co-occurring features strengthen edges.
 * Clusters form. Those clusters ARE protocols.
 * As many clusters as the physics demands. Not 3. Not 5. Whatever is there.
 *
 * CRITICAL: The observer loop.
 *   tick() writes co_present[], not marked[].
 *   The observer must interpret co_present and feed back as new marks.
 *   Without this, cascaded computation (shift registers, pipelines)
 *   doesn't propagate. The engine presents. The observer decides.
 *   The decision feeds back. That IS the computation.
 *
 * Isaac Oravec & Claude — February 2026
 */

#ifndef TEST_SENSE
#include "xyzt_os.h"
#include "hw.h"
#endif

/* =========================================================================
 * FEATURE TYPES — structural, not semantic
 * ========================================================================= */

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

/* =========================================================================
 * FEATURE NODE NAMING — structural coordinates, not semantics
 * ========================================================================= */

static void feat_name(char *buf, int type, int pin_a, int pin_b) {
    const char tags[] = "ARDCSBP";
    char t = (type >= 0 && type <= 6) ? tags[type] : '?';
    int i = 0;
    buf[i++] = t;
    if (pin_a >= 10) buf[i++] = '0' + (pin_a / 10);
    buf[i++] = '0' + (pin_a % 10);
    if (type == FEAT_CORRELATION || type == FEAT_ASYMMETRY || type == FEAT_PERIOD) {
        buf[i++] = '.';
        if (pin_b >= 10) buf[i++] = '0' + (pin_b / 10);
        buf[i++] = '0' + (pin_b % 10);
    }
    buf[i] = '\0';
}

/* =========================================================================
 * FEATURE EXTRACTION
 * ========================================================================= */

#define ACTIVE_THRESH     4
#define REG_TOLERANCE_NUM 1
#define REG_TOLERANCE_DEN 4
#define DUTY_LO_NUM  35
#define DUTY_HI_NUM  65
#define DUTY_DEN    100
#define CORR_WINDOW_US  5
#define SENSE_MAX_FEATS 64

typedef struct {
    int  node_ids[SENSE_MAX_FEATS];
    int  n_features;
} sense_pass_t;

static void add_feature(sense_pass_t *pass, const char *name) {
    if (pass->n_features >= SENSE_MAX_FEATS) return;
    int id = wire_add(name, WNODE_CONCEPT);
    if (id >= 0) {
        pass->node_ids[pass->n_features++] = id;
    }
}

void sense_extract(capture_buf_t *buf, sense_pass_t *pass) {
    pass->n_features = 0;
    if (buf->count < 2) return;

    uint32_t pin_mask = buf->pin_mask;
    uint32_t n = buf->count;
    uint32_t trans[28];
    uint32_t high_samples[28];
    uint32_t total_samples = n;

    for (int p = 0; p < 28; p++) { trans[p] = 0; high_samples[p] = 0; }

    for (uint32_t i = 0; i < n; i++) {
        uint32_t state = buf->events[i].pin_state;
        for (int p = 0; p < 28; p++) {
            if (!(pin_mask & (1 << p))) continue;
            if (state & (1 << p)) high_samples[p]++;
            if (i > 0) {
                uint32_t prev = buf->events[i-1].pin_state;
                if ((state ^ prev) & (1 << p)) trans[p]++;
            }
        }
    }

    char name[16];
    int active_pins[28];
    int n_active = 0;

    /* ACTIVE */
    for (int p = 0; p < 28; p++) {
        active_pins[p] = 0;
        if (!(pin_mask & (1 << p))) continue;
        if (trans[p] >= ACTIVE_THRESH) {
            active_pins[p] = 1;
            n_active++;
            feat_name(name, FEAT_ACTIVE, p, 0);
            add_feature(pass, name);
        }
    }
    if (n_active == 0) return;

    /* DUTY */
    for (int p = 0; p < 28; p++) {
        if (!active_pins[p]) continue;
        uint32_t h100 = high_samples[p] * DUTY_DEN;
        if (h100 >= total_samples * DUTY_LO_NUM &&
            h100 <= total_samples * DUTY_HI_NUM) {
            feat_name(name, FEAT_DUTY, p, 0);
            add_feature(pass, name);
        }
    }

    /* REGULARITY + PERIOD */
    for (int p = 0; p < 28; p++) {
        if (!active_pins[p] || trans[p] < 6) continue;
        uint64_t sum_dt = 0, prev_t = 0;
        int count = 0, first = 1;
        for (uint32_t i = 1; i < n; i++) {
            if ((buf->events[i].pin_state ^ buf->events[i-1].pin_state) & (1 << p)) {
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
            if ((buf->events[i].pin_state ^ buf->events[i-1].pin_state) & (1 << p)) {
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
            feat_name(name, FEAT_REGULARITY, p, 0);
            add_feature(pass, name);
            int bucket;
            if (mean < 10)        bucket = PERIOD_FAST;
            else if (mean < 100)  bucket = PERIOD_MED;
            else if (mean < 1000) bucket = PERIOD_SLOW;
            else                  bucket = PERIOD_VSLOW;
            feat_name(name, FEAT_PERIOD, p, bucket);
            add_feature(pass, name);
        }
    }

    /* CORRELATION */
    for (int p = 0; p < 28; p++) {
        if (!active_pins[p]) continue;
        for (int q = p + 1; q < 28; q++) {
            if (!active_pins[q]) continue;
            int corr_count = 0, p_trans = 0;
            for (uint32_t i = 1; i < n; i++) {
                uint32_t edge = buf->events[i].pin_state ^ buf->events[i-1].pin_state;
                if (!(edge & (1 << p))) continue;
                p_trans++;
                for (uint32_t j = (i > 3 ? i - 3 : 1); j < n && j <= i + 3; j++) {
                    if (j == 0) continue;
                    uint32_t e2 = buf->events[j].pin_state ^ buf->events[j-1].pin_state;
                    if (e2 & (1 << q)) {
                        uint64_t t1 = buf->events[i].timestamp;
                        uint64_t t2 = buf->events[j].timestamp;
                        uint64_t gap = (t1 > t2) ? (t1 - t2) : (t2 - t1);
                        if (gap <= CORR_WINDOW_US) { corr_count++; break; }
                    }
                }
            }
            if (p_trans > 0 && corr_count * 2 > p_trans) {
                feat_name(name, FEAT_CORRELATION, p, q);
                add_feature(pass, name);
            }
        }
    }

    /* ASYMMETRY */
    for (int p = 0; p < 28; p++) {
        if (!(pin_mask & (1 << p))) continue;
        if (high_samples[p] * 100 < total_samples * 80) continue;
        if (trans[p] > ACTIVE_THRESH * 2) continue;
        for (int q = 0; q < 28; q++) {
            if (p == q || !active_pins[q]) continue;
            feat_name(name, FEAT_ASYMMETRY, p, q);
            add_feature(pass, name);
        }
    }

    /* BURST */
    for (int p = 0; p < 28; p++) {
        if (!active_pins[p] || trans[p] < 6) continue;
        uint64_t gaps[256];
        int n_gaps = 0;
        uint64_t prev_t = 0;
        int first = 1;
        for (uint32_t i = 1; i < n && n_gaps < 256; i++) {
            if (!((buf->events[i].pin_state ^ buf->events[i-1].pin_state) & (1 << p))) continue;
            if (!first) gaps[n_gaps++] = buf->events[i].timestamp - prev_t;
            prev_t = buf->events[i].timestamp;
            first = 0;
        }
        if (n_gaps >= 4) {
            for (int si = 1; si < n_gaps; si++) {
                uint64_t key = gaps[si];
                int sj = si - 1;
                while (sj >= 0 && gaps[sj] > key) { gaps[sj+1] = gaps[sj]; sj--; }
                gaps[sj+1] = key;
            }
            uint64_t median = gaps[n_gaps / 2];
            uint64_t max_gap = gaps[n_gaps - 1];
            if (median > 0 && max_gap > median * 4) {
                feat_name(name, FEAT_BURST, p, 0);
                add_feature(pass, name);
            }
        }
    }

    /* ── HEBBIAN WIRING ──────────────────────────────────────── */
    for (int i = 0; i < pass->n_features; i++)
        for (int j = i + 1; j < pass->n_features; j++)
            wire_connect(pass->node_ids[i], pass->node_ids[j]);

    for (int i = 0; i < pass->n_features; i++)
        wire_learn(pass->node_ids[i], 1);
}

/* =========================================================================
 * DECAY — asymmetric: learn +20, decay -5
 *
 * >25% appearance rate → strengthens
 * <25% appearance rate → weakens
 * ========================================================================= */

/* Decay must be much weaker than learn.
 * Learn=+20, Decay=-2.
 * Maintenance threshold: 2/22 = ~9% appearance rate.
 * With N patterns, each appears 1/N of the time.
 * Supports up to ~11 concurrent patterns before edges decay.
 * For more patterns, reduce W_DECAY further or decay per-epoch. */
#define W_DECAY 2

void sense_decay(sense_pass_t *pass) {
    WireGraph *wg = wire_get();
    for (uint32_t i = 0; i < wg->n_nodes; i++) {
        int found = 0;
        for (int j = 0; j < pass->n_features; j++)
            if (pass->node_ids[j] == (int)i) { found = 1; break; }
        if (!found && wg->nodes[i].hit_count > 0) {
            for (uint32_t e = 0; e < wg->n_edges; e++) {
                WireEdge *edge = &wg->edges[e];
                if ((int)edge->src == (int)i || (int)edge->dst == (int)i) {
                    edge->fails++;
                    int nw = (int)edge->weight - W_DECAY;
                    edge->weight = (nw < W_MIN) ? W_MIN : (uint8_t)nw;
                }
            }
            wg->total_learns++;
        }
    }
}

/* =========================================================================
 * SENSE PRINT
 * ========================================================================= */

void sense_print_pass(sense_pass_t *pass) {
    uart_puts("  Features: "); uart_dec(pass->n_features); uart_puts("\r\n");
    WireGraph *wg = wire_get();
    for (int i = 0; i < pass->n_features; i++) {
        int id = pass->node_ids[i];
        if (id >= 0 && id < (int)wg->n_nodes) {
            uart_puts("    "); uart_puts(wg->nodes[id].name);
            uart_puts("  hits="); uart_dec(wg->nodes[id].hit_count);
            uart_puts("\r\n");
        }
    }
}

/* =========================================================================
 * OBSERVER FEEDBACK — the critical loop closer
 *
 * tick() writes co_present[], NOT marked[].
 * This function reads co_present, applies obs_any, marks back.
 * Without this, shift registers don't shift, pipelines don't flow.
 *
 * GPIO positions are input-only (bridge_sample sets them).
 * Only internal positions get feedback from the observer.
 * ========================================================================= */

#ifndef TEST_SENSE

static void observe_feedback(V6Grid *g, int internal_base) {
    for (int p = internal_base; p < V6_MAX_POS; p++) {
        if (!g->active[p]) continue;
        v6_mark(g, p, v6_obs_any(g->co_present[p]));
    }
}

void sense_observe(capture_buf_t *buf, uint64_t duration_us) {
    capture_for_duration(buf, duration_us);
    static sense_pass_t pass;
    sense_extract(buf, &pass);
    sense_decay(&pass);
}

/* Tick with observer feedback.
 * Use instead of raw v6_tick() when grid computation must propagate.
 * internal_base = first non-GPIO position (PI_DATA_BASE or GPIO_WIDTH) */
void sense_tick(V6Grid *g, int internal_base) {
    v6_tick(g);
    observe_feedback(g, internal_base);
}

#endif
