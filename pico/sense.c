/*
 * sense.c — Physical Observer
 *
 * Raw edge events come in. Structural invariants come out.
 * Features that co-occur get wired together.
 * The wire graph IS the firmware rewriting itself.
 *
 * No protocol labels. No parsing.
 * The same way a river carves a canyon:
 * structure imposes itself on substrate.
 *
 * Features extracted from raw physics:
 *   REGULARITY  — consistent inter-edge timing (clocks, bit rates)
 *   CORRELATION — pins that transition together (differential pairs)
 *   BURST       — clusters of activity with gaps (packets)
 *   PATTERN     — repeating bit sequences (SYNC, preambles)
 *
 * Isaac Oravec & Claude — February 2026
 */

#include "xyzt_engine.h"
#include "wire.h"
#include "telemetry.h"

/* ═══════════════════════════════════════════════════════════
 * FEATURE SLOTS — positions in the grid
 *
 * Features map to grid positions 0-63.
 * Groups of positions = categories of structure.
 * The grouping is arbitrary — what matters is co-presence.
 * ═══════════════════════════════════════════════════════════ */

/* Regularity features: positions 0-15
 * Binned by inter-edge period.
 * Short periods = fast signals. Long = slow. */
#define FEAT_REG_BASE    0
#define FEAT_REG_COUNT   16

/* Correlation features: positions 16-23
 * Which pin combinations transition together. */
#define FEAT_CORR_BASE   16
#define FEAT_CORR_COUNT  8

/* Burst features: positions 24-31
 * Gap structure. Packet boundaries. */
#define FEAT_BURST_BASE  24
#define FEAT_BURST_COUNT 8

/* Pattern features: positions 32-47
 * Repeating bit sequences detected in edge stream. */
#define FEAT_PAT_BASE    32
#define FEAT_PAT_COUNT   16

/* Activity features: positions 48-55
 * Raw transition density. How alive is each pin? */
#define FEAT_ACT_BASE    48
#define FEAT_ACT_COUNT   8

/* ═══════════════════════════════════════════════════════════
 * STATE — persistent across observe cycles
 * ═══════════════════════════════════════════════════════════ */

static xyzt_grid_t  grid;
static wire_graph_t wires;
static uint32_t     observe_count = 0;
static uint8_t      prev_features[GRID_SIZE]; /* last cycle's active features */
static uint32_t     prev_feature_count = 0;

/* Histogram for regularity detection */
#define PERIOD_BINS 16
#define PERIOD_BIN_WIDTH_NS 1000  /* each bin = 1μs of period */

/* Pattern shift register for sequence detection */
static uint32_t pattern_shift = 0;
static uint32_t pattern_history[64];
static uint32_t pattern_hist_idx = 0;

/* ═══════════════════════════════════════════════════════════
 * INIT — called implicitly on first observe
 * ═══════════════════════════════════════════════════════════ */

static uint8_t sense_initialized = 0;

static void sense_init(void) {
    xyzt_grid_init(&grid);
    wire_init(&wires);
    memset(prev_features, 0, sizeof(prev_features));
    memset(pattern_history, 0, sizeof(pattern_history));
    prev_feature_count = 0;
    observe_count = 0;
    pattern_shift = 0;
    pattern_hist_idx = 0;
    sense_initialized = 1;
}

/* ═══════════════════════════════════════════════════════════
 * SENSE_OBSERVE — the entire AI
 *
 * 1. Extract features from edge events (X: sequence)
 * 2. Mark features on grid (Y: comparison via co-presence)
 * 3. Tick (T: capture NOW)
 * 4. Read co-presence (Z: what persists = what's real)
 * 5. Hebbian wire: features that co-occur strengthen
 * 6. Decay: features that don't co-occur weaken
 *
 * The wire graph after N cycles IS the model of whatever
 * is connected to the pins. Nobody told it what to learn.
 * ═══════════════════════════════════════════════════════════ */

static void sense_observe(const capture_buf_t *buf, uint32_t window_us) {
    if (!sense_initialized) sense_init();

    /* ─── Tick: snap previous state ─── */
    xyzt_tick(&grid);

    /* ─── Clear marks for fresh observation ─── */
    xyzt_clear_all(&grid);

    if (buf->count < 2) {
        /* No edges = silence. Still tick. Wires decay. Still tap. */
        wire_decay_all(&wires);
        telemetry_tap(&wires);
        observe_count++;
        return;
    }

    /* ═══════════════════════════════════════════════════════
     * FEATURE EXTRACTION — structure from raw edges
     * ═══════════════════════════════════════════════════════ */

    uint32_t period_hist[PERIOD_BINS];
    memset(period_hist, 0, sizeof(period_hist));

    uint32_t corr_count = 0;       /* edges where multiple pins flip */
    uint32_t total_edges = buf->count;
    uint32_t burst_gaps = 0;       /* large gaps = packet boundaries */
    uint32_t last_edge_ns = buf->edges[0].timestamp_ns;

    /* Scan all edges */
    for (uint32_t i = 1; i < buf->count; i++) {
        uint32_t dt = buf->edges[i].timestamp_ns - buf->edges[i-1].timestamp_ns;

        /* Regularity: bin the inter-edge period */
        uint32_t bin = dt / PERIOD_BIN_WIDTH_NS;
        if (bin >= PERIOD_BINS) bin = PERIOD_BINS - 1;
        period_hist[bin]++;

        /* Correlation: did multiple bits change at once? */
        uint32_t changed = buf->edges[i].pin_state ^ buf->edges[i-1].pin_state;
        uint32_t bits_changed = 0;
        uint32_t tmp = changed;
        while (tmp) { bits_changed += tmp & 1; tmp >>= 1; }
        if (bits_changed > 1) corr_count++;

        /* Burst: large gap = packet boundary */
        if (dt > (window_us * 1000) / 20) {  /* gap > 5% of window */
            burst_gaps++;
        }

        /* Pattern: shift in transition direction */
        uint32_t rising = buf->edges[i].pin_state & ~buf->edges[i-1].pin_state;
        pattern_shift = (pattern_shift << 1) | (rising ? 1 : 0);

        last_edge_ns = buf->edges[i].timestamp_ns;
    }

    /* ═══════════════════════════════════════════════════════
     * MARK FEATURES ON GRID
     * ═══════════════════════════════════════════════════════ */

    uint8_t  active_features[GRID_SIZE];
    uint32_t n_active = 0;

    /* Regularity: mark bins with significant counts */
    uint32_t reg_threshold = total_edges / (PERIOD_BINS * 2);
    if (reg_threshold < 2) reg_threshold = 2;
    for (int b = 0; b < PERIOD_BINS; b++) {
        if (period_hist[b] > reg_threshold) {
            uint8_t pos = FEAT_REG_BASE + b;
            xyzt_mark(&grid, pos);
            active_features[n_active++] = pos;
        }
    }

    /* Correlation: how much of traffic is multi-pin? */
    if (total_edges > 0) {
        uint32_t corr_pct = (corr_count * 100) / total_edges;
        /* Map 0-100% into CORR positions */
        uint32_t corr_level = (corr_pct * FEAT_CORR_COUNT) / 100;
        if (corr_level >= FEAT_CORR_COUNT) corr_level = FEAT_CORR_COUNT - 1;
        for (uint32_t c = 0; c <= corr_level; c++) {
            uint8_t pos = FEAT_CORR_BASE + c;
            xyzt_mark(&grid, pos);
            active_features[n_active++] = pos;
        }
    }

    /* Burst: number of packet boundaries detected */
    if (burst_gaps > 0) {
        uint32_t burst_level = burst_gaps;
        if (burst_level >= FEAT_BURST_COUNT) burst_level = FEAT_BURST_COUNT - 1;
        for (uint32_t b = 0; b <= burst_level; b++) {
            uint8_t pos = FEAT_BURST_BASE + b;
            xyzt_mark(&grid, pos);
            active_features[n_active++] = pos;
        }
    }

    /* Pattern: hash the shift register into pattern positions */
    pattern_history[pattern_hist_idx & 63] = pattern_shift;
    pattern_hist_idx++;

    /* Check for repeating patterns: compare current to history */
    uint32_t pat_matches = 0;
    for (uint32_t h = 0; h < 64; h++) {
        if (pattern_history[h] != 0 &&
            (pattern_shift & 0xFF) == (pattern_history[h] & 0xFF)) {
            pat_matches++;
        }
    }
    if (pat_matches > 2) {
        /* Strong pattern detected. Map hash to position. */
        uint8_t pat_pos = FEAT_PAT_BASE + (pattern_shift & (FEAT_PAT_COUNT - 1));
        xyzt_mark(&grid, pat_pos);
        active_features[n_active++] = pat_pos;
    }

    /* Activity: raw transition density */
    {
        uint32_t density = total_edges;
        uint32_t act_level = 0;
        /* Log-scale binning */
        while (density > 4 && act_level < FEAT_ACT_COUNT - 1) {
            density >>= 1;
            act_level++;
        }
        for (uint32_t a = 0; a <= act_level; a++) {
            uint8_t pos = FEAT_ACT_BASE + a;
            xyzt_mark(&grid, pos);
            active_features[n_active++] = pos;
        }
    }

    /* ═══════════════════════════════════════════════════════
     * RESOLVE CO-PRESENCE: what persists from last tick?
     * ═══════════════════════════════════════════════════════ */
    xyzt_resolve(&grid);

    /* ═══════════════════════════════════════════════════════
     * HEBBIAN WIRING: features that co-occur, wire together
     *
     * All active features in this window co-occurred.
     * Every pair gets strengthened. This IS learning.
     * ═══════════════════════════════════════════════════════ */
    for (uint32_t i = 0; i < n_active; i++) {
        for (uint32_t j = i + 1; j < n_active; j++) {
            wire_bind(&wires, active_features[i], active_features[j]);
        }
        wires.fire_count[active_features[i]]++;
    }

    /* Decay everything that didn't fire */
    wire_decay_all(&wires);

    /* Save for next cycle */
    memcpy(prev_features, active_features,
           n_active < GRID_SIZE ? n_active : GRID_SIZE);
    prev_feature_count = n_active;

    /* ═══════════════════════════════════════════════════════
     * TELEMETRY: fire-and-forget one row of wire graph
     * DMA + PIO1 handle the shift-out. CPU returns immediately.
     * Full matrix streams out every 128 ticks (128ms).
     * ═══════════════════════════════════════════════════════ */
    telemetry_tap(&wires);

    observe_count++;
}
