/*
 * hw.h — PIO Retina + Body Discovery
 *
 * RP2040 PIO: dedicated silicon, runs at system clock (125MHz).
 * Independent of CPU. Two instructions: sample pins, push to FIFO.
 * DMA drains FIFO to RAM continuously. CPU never involved.
 *
 * 125MHz = 8ns per sample.
 * USB 1.1 Full Speed = 12 Mbps = 83ns per bit.
 * We oversample by ~10x. Every edge is captured.
 *
 * The PIO doesn't know what USB is. It doesn't know what I2C is.
 * It doesn't know what anything is. It samples voltage on copper.
 *
 * On boot the device probes its own pins to discover its body:
 * which pins are connected to each other, which respond to drive,
 * which are floating. The topology map IS self-knowledge.
 *
 * Isaac Oravec & Claude — February 2026
 */

#ifndef HW_H
#define HW_H

#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include <string.h>

/* ═══════════════════════════════════════════════════════════
 * PIN LAYOUT
 * ═══════════════════════════════════════════════════════════ */
#define OBS_PIN_BASE   0
#define OBS_PIN_COUNT  28   /* GPIO 0-27 observed/driven */
#define OBS_PIN_MASK   0x0FFFFFFFu  /* bits 0-27 */
#define TAP_PIN        28   /* telemetry out — never probed */

/* ═══════════════════════════════════════════════════════════
 * BODY MAP — discovered at boot, not configured
 *
 * Each pin gets classified by what happened when we drove it
 * and what it does when we don't.
 * ═══════════════════════════════════════════════════════════ */
#define PIN_UNKNOWN    0   /* not yet probed */
#define PIN_FLOATING   1   /* nothing responds, nothing drives it */
#define PIN_WORLD_IN   2   /* something external is driving this pin */
#define PIN_SELF_LINK  3   /* driving this pin caused a response elsewhere */
#define PIN_OUTPUT     4   /* classified as available for output */

typedef struct {
    uint8_t  role[OBS_PIN_COUNT];           /* PIN_* classification */
    uint8_t  responds_to[OBS_PIN_COUNT];    /* if SELF_LINK: which pin responded */
    uint32_t world_activity[OBS_PIN_COUNT]; /* edge count seen during probe (no drive) */
    uint32_t probe_response[OBS_PIN_COUNT]; /* bitmask: which pins moved when I drove this one */
    uint8_t  n_world;                       /* count of WORLD_IN pins */
    uint8_t  n_self;                        /* count of SELF_LINK pins */
    uint8_t  n_floating;                    /* count of FLOATING pins */
    uint8_t  probed;                        /* 1 after boot probe completes */
} body_map_t;

static body_map_t body;

/* ═══════════════════════════════════════════════════════════
 * CAPTURE BUFFER — timestamped edge events
 * ═══════════════════════════════════════════════════════════ */
#define CAPTURE_MAX_EDGES 4096

typedef struct {
    uint32_t timestamp_ns;
    uint32_t pin_state;
} edge_event_t;

typedef struct {
    edge_event_t edges[CAPTURE_MAX_EDGES];
    uint32_t count;
} capture_buf_t;

/* ═══════════════════════════════════════════════════════════
 * PIO PROGRAM — the entire "driver"
 *
 * Two instructions on bare silicon:
 *   in pins, 32    ; grab all GPIO states
 *   push noblock   ; shove into FIFO
 *
 * Runs at system clock. Independent of CPU.
 * ═══════════════════════════════════════════════════════════ */
static const uint16_t retina_instructions[] = {
    0x4020,  /* in pins, 32 */
    0x8060,  /* push noblock */
};

static const struct pio_program retina_prog = {
    .instructions = retina_instructions,
    .length = 2,
    .origin = -1,
};

/* ═══════════════════════════════════════════════════════════
 * RAW SAMPLE BUFFER — DMA fills, CPU reads
 * ═══════════════════════════════════════════════════════════ */
#define RAW_BUF_WORDS 4096
#define RAW_BUF_MASK  (RAW_BUF_WORDS - 1)
static uint32_t raw_samples[RAW_BUF_WORDS] __attribute__((aligned(16384)));

static PIO  pio_retina;
static uint pio_sm;
static int  dma_chan;

/* ═══════════════════════════════════════════════════════════
 * INIT — bind PIO to copper, start DMA
 * ═══════════════════════════════════════════════════════════ */
static void hw_init_pio_retina(void) {
    pio_retina = pio0;
    pio_sm = pio_claim_unused_sm(pio_retina, true);
    uint offset = pio_add_program(pio_retina, &retina_prog);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset, offset + 1);
    sm_config_set_in_pins(&c, 0);
    sm_config_set_in_shift(&c, false, true, 32);
    sm_config_set_clkdiv(&c, 1.0f);

    /* All observation pins start as input */
    for (uint pin = 0; pin < OBS_PIN_COUNT; pin++) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        pio_gpio_init(pio_retina, pin);
    }

    pio_sm_init(pio_retina, pio_sm, offset, &c);

    /* DMA: PIO RX FIFO → RAM ring buffer */
    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, false);
    channel_config_set_write_increment(&dc, true);
    channel_config_set_ring(&dc, true, 14);
    channel_config_set_dreq(&dc, pio_get_dreq(pio_retina, pio_sm, false));

    dma_channel_configure(dma_chan, &dc,
        raw_samples,
        &pio_retina->rxf[pio_sm],
        0xFFFFFFFF,
        true);

    pio_sm_set_enabled(pio_retina, pio_sm, true);
}

/* ═══════════════════════════════════════════════════════════
 * CAPTURE — digest raw samples into edge events
 *
 * PIO = 2 instructions = 2 cycles per sample
 * At 125MHz: 62.5M samples/sec = 16ns per sample
 * ═══════════════════════════════════════════════════════════ */
#define NS_PER_SAMPLE 16

static void capture_for_duration(capture_buf_t *buf, uint32_t duration_us) {
    buf->count = 0;

    uint32_t n_samples = (duration_us * 1000) / NS_PER_SAMPLE;
    if (n_samples > RAW_BUF_WORDS) n_samples = RAW_BUF_WORDS;

    uint32_t start_pos = ((uint32_t)(uintptr_t)dma_hw->ch[dma_chan].write_addr
                         - (uint32_t)(uintptr_t)raw_samples) / 4;

    busy_wait_us_32(duration_us);

    uint32_t prev = raw_samples[start_pos & RAW_BUF_MASK] & OBS_PIN_MASK;

    for (uint32_t i = 1; i < n_samples && buf->count < CAPTURE_MAX_EDGES; i++) {
        uint32_t idx = (start_pos + i) & RAW_BUF_MASK;
        uint32_t curr = raw_samples[idx] & OBS_PIN_MASK;

        if (curr != prev) {
            buf->edges[buf->count].timestamp_ns = i * NS_PER_SAMPLE;
            buf->edges[buf->count].pin_state = curr;
            buf->count++;
            prev = curr;
        }
    }
}

/* ═══════════════════════════════════════════════════════════
 * BODY DISCOVERY — learn what I am
 *
 * Phase 1: Listen. PIO retina runs, CPU counts edges per pin.
 *          Any pin with activity = WORLD_IN (something external).
 *
 * Phase 2: Probe. For each non-world pin:
 *          Pull PIO ownership briefly, drive pin HIGH,
 *          read all other pins, drive LOW, read again.
 *          If another pin moved = SELF_LINK (my own wiring).
 *          If nothing moved = FLOATING (free limb).
 *
 * Phase 3: Classify outputs. Floating pins with no world
 *          neighbor are available for output.
 *
 * The body map persists for the lifetime of the device.
 * The sense layer uses it to know self from world.
 * ═══════════════════════════════════════════════════════════ */

/* Phase 1: passive listen — who's already talking? */
static void body_listen(void) {
    capture_buf_t buf;

    /* Sample 3 windows to catch slow signals too */
    for (int pass = 0; pass < 3; pass++) {
        capture_for_duration(&buf, 10000);  /* 10ms per pass */

        for (uint32_t e = 1; e < buf.count; e++) {
            uint32_t changed = buf.edges[e].pin_state ^ buf.edges[e-1].pin_state;
            for (uint pin = 0; pin < OBS_PIN_COUNT; pin++) {
                if (changed & (1u << pin)) {
                    body.world_activity[pin]++;
                }
            }
        }
    }

    /* Any pin with significant activity is world-driven */
    for (uint pin = 0; pin < OBS_PIN_COUNT; pin++) {
        if (body.world_activity[pin] > 4) {
            body.role[pin] = PIN_WORLD_IN;
            body.n_world++;
        }
    }
}

/* Phase 2: active probe — touch each limb, see what moves */
static void body_probe(void) {
    for (uint pin = 0; pin < OBS_PIN_COUNT; pin++) {
        if (body.role[pin] == PIN_WORLD_IN) continue;  /* don't fight external drivers */

        /* Briefly take this pin from PIO, make it an output */
        gpio_set_function(pin, GPIO_FUNC_SIO);
        gpio_set_dir(pin, GPIO_OUT);

        /* Read baseline */
        busy_wait_us_32(10);
        uint32_t baseline = gpio_get_all() & OBS_PIN_MASK & ~(1u << pin);

        /* Drive HIGH, observe */
        gpio_put(pin, 1);
        busy_wait_us_32(50);  /* let signals settle */
        uint32_t high_state = gpio_get_all() & OBS_PIN_MASK & ~(1u << pin);

        /* Drive LOW, observe */
        gpio_put(pin, 0);
        busy_wait_us_32(50);
        uint32_t low_state = gpio_get_all() & OBS_PIN_MASK & ~(1u << pin);

        /* What moved? */
        uint32_t response = (high_state ^ baseline) | (low_state ^ high_state);
        body.probe_response[pin] = response;

        /* Release pin back to PIO input */
        gpio_set_dir(pin, GPIO_IN);
        pio_gpio_init(pio_retina, pin);

        if (response) {
            body.role[pin] = PIN_SELF_LINK;
            /* Record first responder */
            for (uint r = 0; r < OBS_PIN_COUNT; r++) {
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

/* Phase 3: classify outputs — floating pins become available limbs */
static void body_classify(void) {
    for (uint pin = 0; pin < OBS_PIN_COUNT; pin++) {
        if (body.role[pin] == PIN_FLOATING) {
            body.role[pin] = PIN_OUTPUT;
            /* Don't switch to output yet — just mark as available.
             * The sense layer decides when and what to drive. */
        }
    }
    body.probed = 1;
}

/* Full boot sequence: discover what I am */
static void hw_discover_body(void) {
    memset(&body, 0, sizeof(body));
    body_listen();
    body_probe();
    body_classify();
}

/* ═══════════════════════════════════════════════════════════
 * OUTPUT — drive a pin based on learned state
 *
 * Called by the sense layer when the wire graph says
 * a feature should be expressed physically.
 * Only drives pins classified as PIN_OUTPUT.
 * ═══════════════════════════════════════════════════════════ */
static void hw_drive_pin(uint pin, uint8_t value) {
    if (pin >= OBS_PIN_COUNT) return;
    if (body.role[pin] != PIN_OUTPUT && body.role[pin] != PIN_SELF_LINK) return;

    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, value ? 1 : 0);
}

/* Return a pin to observation after driving */
static void hw_release_pin(uint pin) {
    if (pin >= OBS_PIN_COUNT) return;
    gpio_set_dir(pin, GPIO_IN);
    pio_gpio_init(pio_retina, pin);
}

#endif /* HW_H */
