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
 *   ACTIVITY    — raw transition density per pin
 *   SELF        — features the device caused itself (body awareness)
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

/* Pattern features: positions 32-39
 * Repeating bit sequences detected in edge stream. */
#define FEAT_PAT_BASE    32
#define FEAT_PAT_COUNT   8

/* Activity features: positions 40-47
 * Raw transition density. How alive is each pin? */
#define FEAT_ACT_BASE    40
#define FEAT_ACT_COUNT   8

/* Self features: positions 48-55
 * What happened because I drove a pin.
 * Separated from world so the device knows self from other. */
#define FEAT_SELF_BASE   48
#define FEAT_SELF_COUNT  8

/* Output features: positions 56-63
 * Wire graph pressure toward expression.
 * When these are strongly wired to world features,
 * the device has something to say. */
#define FEAT_OUT_BASE    56
#define FEAT_OUT_COUNT   8

/* ═══════════════════════════════════════════════════════════
 * STATE — persistent across observe cycles
 * ═══════════════════════════════════════════════════════════ */

static xyzt_grid_t  grid;
static wire_graph_t wires;
static uint32_t     observe_count = 0;
static uint8_t      prev_features[GRID_SIZE];
static uint32_t     prev_feature_count = 0;

/* Histogram for regularity detection */
#define PERIOD_BINS 16
#define PERIOD_BIN_WIDTH_NS 1000  /* each bin = 1μs of period */

/* Pattern shift register for sequence detection */
static uint32_t pattern_shift = 0;
static uint32_t pattern_history[64];
static uint32_t pattern_hist_idx = 0;

/* Motor map: learned pin ↔ output feature binding
 * pin_feat[pin] = which FEAT_OUT position this pin maps to (0xFF = unassigned)
 * Babbling assigns them. Wire graph crystallizes the mapping. */
static uint8_t  pin_feat[OBS_PIN_COUNT];     /* pin → output feature slot */
static uint8_t  output_active[OBS_PIN_COUNT];
static uint32_t output_hold_ticks[OBS_PIN_COUNT];
static uint8_t  babble_pin = 0;              /* next pin to try babbling on */
static uint32_t babble_cooldown = 0;         /* ticks between babble attempts */

#define BABBLE_INTERVAL  50   /* babble every 50 ticks */
#define BABBLE_HOLD       5   /* hold babble output for 5 ticks */
#define EXPRESS_THRESHOLD 128 /* wire pressure needed to drive */

/* ═══════════════════════════════════════════════════════════
 * INIT
 * ═══════════════════════════════════════════════════════════ */

static uint8_t sense_initialized = 0;

static void sense_init(void) {
    xyzt_grid_init(&grid);
    wire_init(&wires);
    memset(prev_features, 0, sizeof(prev_features));
    memset(pattern_history, 0, sizeof(pattern_history));
    memset(pin_feat, 0xFF, sizeof(pin_feat));
    memset(output_active, 0, sizeof(output_active));
    memset(output_hold_ticks, 0, sizeof(output_hold_ticks));
    babble_pin = 0;
    babble_cooldown = 0;
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
 * 7. Express: if output features are strongly wired, drive pins
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
        /* Silence. Still tick. Wires decay. Still tap. */
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

    uint32_t corr_count = 0;
    uint32_t total_edges = buf->count;
    uint32_t burst_gaps = 0;
    uint32_t pin_activity[OBS_PIN_COUNT];
    memset(pin_activity, 0, sizeof(pin_activity));

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

        /* Per-pin activity tracking */
        for (uint pin = 0; pin < OBS_PIN_COUNT; pin++) {
            if (changed & (1u << pin)) pin_activity[pin]++;
        }

        /* Burst: large gap = packet boundary */
        if (dt > (window_us * 1000) / 20) {
            burst_gaps++;
        }

        /* Pattern: shift in transition direction */
        uint32_t rising = buf->edges[i].pin_state & ~buf->edges[i-1].pin_state;
        pattern_shift = (pattern_shift << 1) | (rising ? 1 : 0);
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

    uint32_t pat_matches = 0;
    for (uint32_t h = 0; h < 64; h++) {
        if (pattern_history[h] != 0 &&
            (pattern_shift & 0xFF) == (pattern_history[h] & 0xFF)) {
            pat_matches++;
        }
    }
    if (pat_matches > 2) {
        uint8_t pat_pos = FEAT_PAT_BASE + (pattern_shift & (FEAT_PAT_COUNT - 1));
        xyzt_mark(&grid, pat_pos);
        active_features[n_active++] = pat_pos;
    }

    /* Activity: raw transition density */
    {
        uint32_t density = total_edges;
        uint32_t act_level = 0;
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
     * SELF-AWARENESS: mark features caused by my own outputs
     *
     * If any pin I'm currently driving shows activity in this
     * capture window, that's self-caused. Tag it separately
     * so the wire graph knows self from world.
     * ═══════════════════════════════════════════════════════ */
    if (body.probed) {
        uint32_t self_activity = 0;
        for (uint pin = 0; pin < OBS_PIN_COUNT; pin++) {
            if (output_active[pin] && pin_activity[pin] > 0) {
                self_activity++;
            }
        }
        if (self_activity > 0) {
            uint32_t self_level = self_activity;
            if (self_level >= FEAT_SELF_COUNT) self_level = FEAT_SELF_COUNT - 1;
            for (uint32_t s = 0; s <= self_level; s++) {
                uint8_t pos = FEAT_SELF_BASE + s;
                xyzt_mark(&grid, pos);
                active_features[n_active++] = pos;
            }
        }
    }

    /* ═══════════════════════════════════════════════════════
     * RESOLVE CO-PRESENCE: what persists from last tick?
     * ═══════════════════════════════════════════════════════ */
    xyzt_resolve(&grid);

    /* ═══════════════════════════════════════════════════════
     * HEBBIAN WIRING: features that co-occur, wire together
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
     * MOTOR SYSTEM: babble → learn → express
     *
     * Phase 1 — BABBLE: periodically drive a random output
     * pin. Mark its FEAT_OUT position. Next tick, the sense
     * layer sees what happened. If the result co-occurs with
     * world features, wire_bind connects them. If not, decay
     * dissolves the link. The pin mapping crystallizes the
     * same way everything else does.
     *
     * Phase 2 — EXPRESS: for each output pin with a learned
     * mapping (pin_feat[pin] != 0xFF), check if that FEAT_OUT
     * position is strongly wired to currently active features.
     * If so, drive the pin. The wire graph IS the motor map.
     *
     * Three forces: STRENGTHEN on resonance, DECAY on silence,
     * SATURATE when locked in. Same code. Same constants.
     * No new mechanism.
     * ═══════════════════════════════════════════════════════ */
    if (body.probed) {

        /* ── BABBLE: try output pins, let Hebbian sort them ── */
        if (babble_cooldown > 0) {
            babble_cooldown--;
        } else {
            /* Find next available output pin to babble on */
            uint8_t tried = 0;
            while (tried < OBS_PIN_COUNT) {
                if (body.role[babble_pin] == PIN_OUTPUT &&
                    !output_active[babble_pin]) {
                    break;
                }
                babble_pin = (babble_pin + 1) % OBS_PIN_COUNT;
                tried++;
            }

            if (tried < OBS_PIN_COUNT) {
                /* Assign this pin an output feature slot if unassigned */
                if (pin_feat[babble_pin] == 0xFF) {
                    /* Find an unused FEAT_OUT slot */
                    for (int f = 0; f < FEAT_OUT_COUNT; f++) {
                        uint8_t slot = FEAT_OUT_BASE + f;
                        uint8_t taken = 0;
                        for (uint p = 0; p < OBS_PIN_COUNT; p++) {
                            if (pin_feat[p] == slot) { taken = 1; break; }
                        }
                        if (!taken) {
                            pin_feat[babble_pin] = slot;
                            break;
                        }
                    }
                }

                /* Drive the pin, mark its output feature */
                if (pin_feat[babble_pin] != 0xFF) {
                    hw_drive_pin(babble_pin, 1);
                    output_active[babble_pin] = 1;
                    output_hold_ticks[babble_pin] = BABBLE_HOLD;
                    xyzt_mark(&grid, pin_feat[babble_pin]);
                    active_features[n_active++] = pin_feat[babble_pin];

                    /* Also mark self-feature for this babble */
                    uint8_t self_slot = FEAT_SELF_BASE +
                        (pin_feat[babble_pin] - FEAT_OUT_BASE);
                    if (self_slot < FEAT_SELF_BASE + FEAT_SELF_COUNT) {
                        xyzt_mark(&grid, self_slot);
                        active_features[n_active++] = self_slot;
                    }
                }

                babble_pin = (babble_pin + 1) % OBS_PIN_COUNT;
                babble_cooldown = BABBLE_INTERVAL;
            }
        }

        /* ── EXPRESS: drive pins whose learned features have pressure ── */
        for (uint pin = 0; pin < OBS_PIN_COUNT; pin++) {
            if (body.role[pin] != PIN_OUTPUT) continue;
            if (pin_feat[pin] == 0xFF) continue;  /* no mapping yet */

            uint32_t pressure = wire_total_weight(&wires, pin_feat[pin]);

            if (pressure > EXPRESS_THRESHOLD && !output_active[pin]) {
                /* Wire graph says: this output resonates with current input.
                 * Drive the pin. Mark the feature. Next tick captures the
                 * result. If it produces coherent structure, the link
                 * strengthens. If not, it decays. */
                hw_drive_pin(pin, 1);
                output_active[pin] = 1;
                output_hold_ticks[pin] = BABBLE_HOLD;
                xyzt_mark(&grid, pin_feat[pin]);
            }

            /* Hold and release */
            if (output_active[pin]) {
                if (output_hold_ticks[pin] > 0) {
                    output_hold_ticks[pin]--;
                } else {
                    hw_drive_pin(pin, 0);
                    hw_release_pin(pin);
                    output_active[pin] = 0;
                }
            }
        }
    }

    /* ═══════════════════════════════════════════════════════
     * TELEMETRY: fire-and-forget one row of wire graph
     * ═══════════════════════════════════════════════════════ */
    telemetry_tap(&wires);

    observe_count++;
}
