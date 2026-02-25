/*
 * XYZT-OS v6 Kernel — Pure Co-Presence, On Metal
 *
 * Same soul as the Pico. Different body.
 *
 * Pi Zero 2: no PIO, so CPU polls GPIO directly.
 * Cortex-A53 at 1GHz polls fast enough.
 * UART streams telemetry (where Pico uses PIO1+GPIO28).
 * System timer gives microsecond edge timestamps.
 *
 * Boot:
 *   1. UART init (telemetry channel)
 *   2. All GPIO input (retina open)
 *   3. Body discovery (listen, probe, classify)
 *   4. Observe forever
 *
 * Isaac Oravec & Claude — February 2026
 */

#include "xyzt_os.h"
#include "hw.h"

/* Freestanding: compiler needs these */
extern void *memcpy(void *dst, const void *src, unsigned long n);
extern void *memset(void *s, int c, unsigned long n);

/* =========================================================================
 * ENGINE — identical to Pico xyzt_engine.h, inlined for bare-metal
 * ========================================================================= */

static void xyzt_grid_init(xyzt_grid_t *g) {
    memset(g, 0, sizeof(*g));
}

static void xyzt_mark(xyzt_grid_t *g, uint8_t pos) {
    if (pos < GRID_SIZE && !g->marks[pos]) {
        g->marks[pos] = 1;
        g->pop++;
    }
}

static void xyzt_clear(xyzt_grid_t *g, uint8_t pos) {
    if (pos < GRID_SIZE && g->marks[pos]) {
        g->marks[pos] = 0;
        g->pop--;
    }
}

static void xyzt_clear_all(xyzt_grid_t *g) {
    memset(g->marks, 0, GRID_SIZE);
    g->pop = 0;
}

static void xyzt_tick(xyzt_grid_t *g) {
    g->co_pop = 0;
    for (int i = 0; i < GRID_SIZE; i++) {
        g->snap[i] = g->marks[i];
        g->co_present[i] = 0;
    }
    g->tick_count++;
}

static void xyzt_resolve(xyzt_grid_t *g) {
    g->co_pop = 0;
    for (int i = 0; i < GRID_SIZE; i++) {
        g->co_present[i] = g->snap[i] & g->marks[i];
        if (g->co_present[i]) g->co_pop++;
    }
}

/* =========================================================================
 * WIRE GRAPH — identical to Pico wire.h, inlined
 * ========================================================================= */

static void wire_init(wire_graph_t *w) {
    memset(w, 0, sizeof(*w));
}

static void wire_bind(wire_graph_t *w, uint8_t a, uint8_t b) {
    if (a >= WIRE_MAX_FEATURES || b >= WIRE_MAX_FEATURES || a == b) return;
    uint16_t va = (uint16_t)w->edge[a][b] + WIRE_STRENGTHEN;
    if (va > WIRE_SATURATE) va = WIRE_SATURATE;
    w->edge[a][b] = (uint8_t)va;
    w->edge[b][a] = (uint8_t)va;
}

static void wire_decay_all(wire_graph_t *w) {
    for (int i = 0; i < WIRE_MAX_FEATURES; i++) {
        for (int j = i + 1; j < WIRE_MAX_FEATURES; j++) {
            if (w->edge[i][j] > WIRE_DECAY) {
                w->edge[i][j] -= WIRE_DECAY;
                w->edge[j][i] -= WIRE_DECAY;
            } else {
                w->edge[i][j] = 0;
                w->edge[j][i] = 0;
            }
        }
    }
}

static uint32_t wire_total_weight(const wire_graph_t *w, uint8_t f) {
    uint32_t sum = 0;
    for (int i = 0; i < WIRE_MAX_FEATURES; i++)
        sum += w->edge[f][i];
    return sum;
}

/* =========================================================================
 * STATE
 * ========================================================================= */

static body_map_t   body;
static xyzt_grid_t  grid;
static wire_graph_t wires;
static uint32_t     observe_count = 0;

static uint8_t  prev_features[GRID_SIZE];
static uint32_t prev_feature_count = 0;

#define PERIOD_BINS 16
#define PERIOD_BIN_WIDTH_US 1  /* 1μs per bin (system timer is μs) */

static uint32_t pattern_shift = 0;
static uint32_t pattern_history[64];
static uint32_t pattern_hist_idx = 0;

static uint8_t  pin_feat[OBS_PIN_COUNT];
static uint8_t  output_active[OBS_PIN_COUNT];
static uint32_t output_hold_ticks[OBS_PIN_COUNT];
static uint8_t  babble_pin = 0;
static uint32_t babble_cooldown = 0;

#define BABBLE_INTERVAL  50
#define BABBLE_HOLD       5
#define EXPRESS_THRESHOLD 128

/* =========================================================================
 * CAPTURE — CPU-driven GPIO polling with system timer timestamps
 *
 * No PIO on Pi Zero 2. CPU polls gpio_read_all() in tight loop.
 * At 1GHz, ~100ns per poll. System timer gives 1μs resolution.
 * ========================================================================= */

static void capture_for_duration(capture_buf_t *buf, uint64_t duration_us) {
    buf->count = 0;

    uint64_t start = timer_get_ticks();
    uint64_t end = start + duration_us;
    uint32_t prev = gpio_read_all() & SENSE_PIN_MASK;

    while (timer_get_ticks() < end && buf->count < CAPTURE_MAX_EDGES) {
        uint32_t curr = gpio_read_all() & SENSE_PIN_MASK;
        if (curr != prev) {
            buf->edges[buf->count].timestamp_us = timer_get_ticks() - start;
            buf->edges[buf->count].pin_state = curr;
            buf->count++;
            prev = curr;
        }
    }
}

/* =========================================================================
 * BODY DISCOVERY — same algorithm as Pico, different HAL
 * ========================================================================= */

static void body_listen(void) {
    capture_buf_t buf;

    for (int pass = 0; pass < 3; pass++) {
        capture_for_duration(&buf, 10000);

        for (uint32_t e = 1; e < buf.count; e++) {
            uint32_t changed = buf.edges[e].pin_state ^ buf.edges[e-1].pin_state;
            for (uint32_t pin = 0; pin < OBS_PIN_COUNT; pin++) {
                if (changed & (1u << pin)) {
                    body.world_activity[pin]++;
                }
            }
        }
    }

    for (uint32_t pin = 0; pin < OBS_PIN_COUNT; pin++) {
        /* Reserve UART pins */
        if (pin == 14 || pin == 15) {
            body.role[pin] = PIN_RESERVED;
            continue;
        }
        if (body.world_activity[pin] > 4) {
            body.role[pin] = PIN_WORLD_IN;
            body.n_world++;
        }
    }
}

static void body_probe(void) {
    for (uint32_t pin = 0; pin < OBS_PIN_COUNT; pin++) {
        if (body.role[pin] == PIN_WORLD_IN || body.role[pin] == PIN_RESERVED)
            continue;

        /* Drive pin */
        gpio_set_function(pin, GPIO_FUNC_OUTPUT);

        timer_wait_us(10);
        uint32_t baseline = gpio_read_all() & SENSE_PIN_MASK & ~(1u << pin);

        gpio_set(pin);
        timer_wait_us(50);
        uint32_t high_state = gpio_read_all() & SENSE_PIN_MASK & ~(1u << pin);

        gpio_clear(pin);
        timer_wait_us(50);
        uint32_t low_state = gpio_read_all() & SENSE_PIN_MASK & ~(1u << pin);

        uint32_t response = (high_state ^ baseline) | (low_state ^ high_state);
        body.probe_response[pin] = response;

        /* Release back to input */
        gpio_set_function(pin, GPIO_FUNC_INPUT);

        if (response) {
            body.role[pin] = PIN_SELF_LINK;
            for (uint32_t r = 0; r < OBS_PIN_COUNT; r++) {
                if (response & (1u << r)) {
                    body.responds_to[pin] = r;
                    break;
                }
            }
            body.n_self++;
        } else {
            body.role[pin] = PIN_FLOATING;
            body.n_floating++;
        }
    }
}

static void body_classify(void) {
    for (uint32_t pin = 0; pin < OBS_PIN_COUNT; pin++) {
        if (body.role[pin] == PIN_FLOATING) {
            body.role[pin] = PIN_OUTPUT;
        }
    }
    body.probed = 1;
}

static void hw_discover_body(void) {
    memset(&body, 0, sizeof(body));
    body_listen();
    body_probe();
    body_classify();
}

/* =========================================================================
 * OUTPUT — drive pins based on wire graph pressure
 * ========================================================================= */

static void hw_drive_pin(uint32_t pin, uint8_t value) {
    if (pin >= OBS_PIN_COUNT) return;
    if (body.role[pin] != PIN_OUTPUT && body.role[pin] != PIN_SELF_LINK) return;
    gpio_set_function(pin, GPIO_FUNC_OUTPUT);
    if (value) gpio_set(pin); else gpio_clear(pin);
}

static void hw_release_pin(uint32_t pin) {
    if (pin >= OBS_PIN_COUNT) return;
    gpio_set_function(pin, GPIO_FUNC_INPUT);
}

/* =========================================================================
 * TELEMETRY — stream wire graph over UART
 *
 * Same frame format as Pico telemetry:
 *   [0xAA] [row_index] [128 bytes] [XOR checksum]
 * At 115200 baud: 131 bytes = ~11.4ms per frame.
 * One row per tick. Full matrix every 128 ticks = ~14.6s.
 * Slower than Pico (2Mbps), but same protocol. Same listener.
 * ========================================================================= */

#define TAP_SYNC 0xAA
static uint8_t tap_row_cursor = 0;

static void telemetry_tap(const wire_graph_t *w) {
    uint8_t row = tap_row_cursor;

    uart_putc((char)TAP_SYNC);
    uart_putc((char)row);

    uint8_t cksum = row;
    for (int i = 0; i < WIRE_MAX_FEATURES; i++) {
        uint8_t b = w->edge[row][i];
        uart_putc((char)b);
        cksum ^= b;
    }
    uart_putc((char)cksum);

    tap_row_cursor = (tap_row_cursor + 1) & (WIRE_MAX_FEATURES - 1);
}

/* =========================================================================
 * SENSE_OBSERVE — the entire AI (same as Pico sense.c)
 * ========================================================================= */

static uint32_t pin_activity[OBS_PIN_COUNT];

static void sense_observe(const capture_buf_t *buf, uint32_t window_us) {
    xyzt_tick(&grid);
    xyzt_clear_all(&grid);

    if (buf->count < 2) {
        wire_decay_all(&wires);
        telemetry_tap(&wires);
        observe_count++;
        return;
    }

    /* Feature extraction */
    uint32_t period_hist[PERIOD_BINS];
    memset(period_hist, 0, sizeof(period_hist));
    memset(pin_activity, 0, sizeof(pin_activity));

    uint32_t corr_count = 0;
    uint32_t total_edges = buf->count;
    uint32_t burst_gaps = 0;

    for (uint32_t i = 1; i < buf->count; i++) {
        uint32_t dt = (uint32_t)(buf->edges[i].timestamp_us - buf->edges[i-1].timestamp_us);

        uint32_t bin = dt / PERIOD_BIN_WIDTH_US;
        if (bin >= PERIOD_BINS) bin = PERIOD_BINS - 1;
        period_hist[bin]++;

        uint32_t changed = buf->edges[i].pin_state ^ buf->edges[i-1].pin_state;
        uint32_t bits_changed = 0;
        uint32_t tmp = changed;
        while (tmp) { bits_changed += tmp & 1; tmp >>= 1; }
        if (bits_changed > 1) corr_count++;

        for (uint32_t pin = 0; pin < OBS_PIN_COUNT; pin++) {
            if (changed & (1u << pin)) pin_activity[pin]++;
        }

        if (dt > window_us / 20) burst_gaps++;

        uint32_t rising = buf->edges[i].pin_state & ~buf->edges[i-1].pin_state;
        pattern_shift = (pattern_shift << 1) | (rising ? 1 : 0);
    }

    /* Mark features */
    uint8_t  active_features[GRID_SIZE];
    uint32_t n_active = 0;

    /* Regularity */
    uint32_t reg_threshold = total_edges / (PERIOD_BINS * 2);
    if (reg_threshold < 2) reg_threshold = 2;
    for (int b = 0; b < PERIOD_BINS; b++) {
        if (period_hist[b] > reg_threshold) {
            uint8_t pos = FEAT_REG_BASE + b;
            xyzt_mark(&grid, pos);
            active_features[n_active++] = pos;
        }
    }

    /* Correlation */
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

    /* Burst */
    if (burst_gaps > 0) {
        uint32_t burst_level = burst_gaps;
        if (burst_level >= FEAT_BURST_COUNT) burst_level = FEAT_BURST_COUNT - 1;
        for (uint32_t b = 0; b <= burst_level; b++) {
            uint8_t pos = FEAT_BURST_BASE + b;
            xyzt_mark(&grid, pos);
            active_features[n_active++] = pos;
        }
    }

    /* Pattern */
    pattern_history[pattern_hist_idx & 63] = pattern_shift;
    pattern_hist_idx++;
    uint32_t pat_matches = 0;
    for (uint32_t h = 0; h < 64; h++) {
        if (pattern_history[h] != 0 &&
            (pattern_shift & 0xFF) == (pattern_history[h] & 0xFF))
            pat_matches++;
    }
    if (pat_matches > 2) {
        uint8_t pat_pos = FEAT_PAT_BASE + (pattern_shift & (FEAT_PAT_COUNT - 1));
        xyzt_mark(&grid, pat_pos);
        active_features[n_active++] = pat_pos;
    }

    /* Activity */
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

    /* Self-awareness */
    if (body.probed) {
        uint32_t self_activity = 0;
        for (uint32_t pin = 0; pin < OBS_PIN_COUNT; pin++) {
            if (output_active[pin] && pin_activity[pin] > 0)
                self_activity++;
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

    /* Resolve */
    xyzt_resolve(&grid);

    /* Hebbian wiring */
    for (uint32_t i = 0; i < n_active; i++) {
        for (uint32_t j = i + 1; j < n_active; j++) {
            wire_bind(&wires, active_features[i], active_features[j]);
        }
        wires.fire_count[active_features[i]]++;
    }
    wire_decay_all(&wires);

    memcpy(prev_features, active_features,
           n_active < GRID_SIZE ? n_active : GRID_SIZE);
    prev_feature_count = n_active;

    /* Motor system: babble → learn → express */
    if (body.probed) {

        /* Babble: try output pins, let Hebbian sort them */
        if (babble_cooldown > 0) {
            babble_cooldown--;
        } else {
            uint8_t tried = 0;
            while (tried < OBS_PIN_COUNT) {
                if (body.role[babble_pin] == PIN_OUTPUT &&
                    !output_active[babble_pin])
                    break;
                babble_pin = (babble_pin + 1) % OBS_PIN_COUNT;
                tried++;
            }

            if (tried < OBS_PIN_COUNT) {
                if (pin_feat[babble_pin] == 0xFF) {
                    for (int f = 0; f < FEAT_OUT_COUNT; f++) {
                        uint8_t slot = FEAT_OUT_BASE + f;
                        uint8_t taken = 0;
                        for (uint32_t p = 0; p < OBS_PIN_COUNT; p++) {
                            if (pin_feat[p] == slot) { taken = 1; break; }
                        }
                        if (!taken) {
                            pin_feat[babble_pin] = slot;
                            break;
                        }
                    }
                }

                if (pin_feat[babble_pin] != 0xFF) {
                    hw_drive_pin(babble_pin, 1);
                    output_active[babble_pin] = 1;
                    output_hold_ticks[babble_pin] = BABBLE_HOLD;
                    xyzt_mark(&grid, pin_feat[babble_pin]);

                    uint8_t self_slot = FEAT_SELF_BASE +
                        (pin_feat[babble_pin] - FEAT_OUT_BASE);
                    if (self_slot < FEAT_SELF_BASE + FEAT_SELF_COUNT)
                        xyzt_mark(&grid, self_slot);
                }

                babble_pin = (babble_pin + 1) % OBS_PIN_COUNT;
                babble_cooldown = BABBLE_INTERVAL;
            }
        }

        /* Express: drive pins whose learned features have pressure */
        for (uint32_t pin = 0; pin < OBS_PIN_COUNT; pin++) {
            if (body.role[pin] != PIN_OUTPUT) continue;
            if (pin_feat[pin] == 0xFF) continue;

            uint32_t pressure = wire_total_weight(&wires, pin_feat[pin]);

            if (pressure > EXPRESS_THRESHOLD && !output_active[pin]) {
                hw_drive_pin(pin, 1);
                output_active[pin] = 1;
                output_hold_ticks[pin] = BABBLE_HOLD;
                xyzt_mark(&grid, pin_feat[pin]);
            }

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

    /* Telemetry */
    telemetry_tap(&wires);
    observe_count++;
}

/* =========================================================================
 * KERNEL MAIN
 * ========================================================================= */

void xyzt_kernel_main(void) {
    /* UART: telemetry channel */
    uart_init();
    uart_puts("\n\nXYZT-OS v6 — booting\n");

    /* All GPIO as input (retina open) */
    for (uint32_t pin = 0; pin < OBS_PIN_COUNT; pin++) {
        if (pin == 14 || pin == 15) continue;  /* UART reserved */
        gpio_set_function(pin, GPIO_FUNC_INPUT);
    }

    uart_puts("Retina open. Discovering body...\n");

    /* Init state */
    xyzt_grid_init(&grid);
    wire_init(&wires);
    memset(prev_features, 0, sizeof(prev_features));
    memset(pattern_history, 0, sizeof(pattern_history));
    memset(pin_feat, 0xFF, sizeof(pin_feat));
    memset(output_active, 0, sizeof(output_active));
    memset(output_hold_ticks, 0, sizeof(output_hold_ticks));
    babble_pin = 0;
    babble_cooldown = 0;

    /* Body discovery */
    hw_discover_body();

    uart_puts("Body map: world=");
    uart_dec(body.n_world);
    uart_puts(" self=");
    uart_dec(body.n_self);
    uart_puts(" output=");
    uart_dec(body.n_floating);
    uart_puts("\nObserving...\n");

    /* The observer digests reality forever */
    capture_buf_t buf;
    while (1) {
        capture_for_duration(&buf, 1000);
        sense_observe(&buf, 1000);
    }
}
