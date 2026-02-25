/*
 * hw.h — PIO Retina
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
 * NOTE: All 29 GPIO pins are the retina. Wire anything to any pin.
 * I2C, SPI, USB tap, buttons, sensors, another Pico — the board
 * doesn't know and doesn't care. It observes voltage on copper.
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
 * PIN ASSIGNMENT — wire whatever you want here
 * ═══════════════════════════════════════════════════════════ */
/* ALL GPIO pins EXCEPT 28 (telemetry tap).
 * The PIO samples every pin every cycle.
 * GPIO 28 is sacrificed for exfiltration. */
#define OBS_PIN_BASE   0
#define OBS_PIN_COUNT  28   /* GPIO 0-27 observed */
#define OBS_PIN_MASK   0x0FFFFFFFu  /* bits 0-27, pin 28 excluded */

/* ═══════════════════════════════════════════════════════════
 * CAPTURE BUFFER — timestamped edge events
 * Raw voltage → distinction. That's all.
 * ═══════════════════════════════════════════════════════════ */
#define CAPTURE_MAX_EDGES 4096

typedef struct {
    uint32_t timestamp_ns;  /* nanoseconds from capture start */
    uint32_t pin_state;     /* which pins are high */
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
 * That's the retina. Raw photons hitting raw nerve.
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
 * Ring buffer: DMA wraps automatically, never stalls.
 * ═══════════════════════════════════════════════════════════ */
#define RAW_BUF_WORDS 4096                   /* 16KB, must be power of 2 */
#define RAW_BUF_MASK  (RAW_BUF_WORDS - 1)
static uint32_t raw_samples[RAW_BUF_WORDS] __attribute__((aligned(16384)));

static PIO  pio_retina;
static uint pio_sm;
static int  dma_chan;

/* ═══════════════════════════════════════════════════════════
 * INIT — bind PIO to copper, start DMA
 *
 * After this call:
 *   PIO is sampling every pin at 62.5M samples/sec
 *   DMA is draining FIFO to RAM in a ring buffer
 *   CPU is free to observe
 * ═══════════════════════════════════════════════════════════ */
static void hw_init_pio_retina(void) {
    pio_retina = pio0;
    pio_sm = pio_claim_unused_sm(pio_retina, true);
    uint offset = pio_add_program(pio_retina, &retina_prog);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset, offset + 1);
    sm_config_set_in_pins(&c, 0);              /* read from GPIO 0 base */
    sm_config_set_in_shift(&c, false, true, 32); /* shift left, autopush */
    sm_config_set_clkdiv(&c, 1.0f);            /* FULL system clock */

    /* Every GPIO pin is an input. Every pin is the retina. */
    for (uint pin = 0; pin < OBS_PIN_COUNT; pin++) {
        gpio_init(pin);
        gpio_set_dir(pin, GPIO_IN);
        pio_gpio_init(pio_retina, pin);
    }

    pio_sm_init(pio_retina, pio_sm, offset, &c);

    /* DMA: PIO RX FIFO → RAM ring buffer, free-running */
    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, false);   /* always read from FIFO */
    channel_config_set_write_increment(&dc, true);    /* advance through RAM */
    channel_config_set_ring(&dc, true, 14);           /* wrap write at 2^14 = 16KB = 4096 words */
    channel_config_set_dreq(&dc, pio_get_dreq(pio_retina, pio_sm, false));

    dma_channel_configure(dma_chan, &dc,
        raw_samples,                            /* write: RAM ring buffer */
        &pio_retina->rxf[pio_sm],               /* read:  PIO RX FIFO */
        0xFFFFFFFF,                              /* transfer count: run forever */
        true);                                   /* start immediately */

    /* Enable PIO state machine — retina is now open */
    pio_sm_set_enabled(pio_retina, pio_sm, true);
}

/* ═══════════════════════════════════════════════════════════
 * CAPTURE — digest raw samples into edge events
 *
 * Waits for duration_us microseconds worth of PIO samples.
 * Scans for transitions on the observation pins.
 * Each transition becomes a timestamped edge event.
 *
 * PIO program = 2 instructions = 2 cycles per sample
 * At 125MHz: 62.5M samples/sec = 16ns per sample
 * ═══════════════════════════════════════════════════════════ */
#define NS_PER_SAMPLE 16

static void capture_for_duration(capture_buf_t *buf, uint32_t duration_us) {
    buf->count = 0;

    /* How many PIO samples fit in this window? */
    uint32_t n_samples = (duration_us * 1000) / NS_PER_SAMPLE;
    if (n_samples > RAW_BUF_WORDS) n_samples = RAW_BUF_WORDS;

    /* Snapshot current DMA write position */
    uint32_t start_pos = ((uint32_t)(uintptr_t)dma_hw->ch[dma_chan].write_addr
                         - (uint32_t)(uintptr_t)raw_samples) / 4;

    /* Wait for DMA to advance enough samples */
    busy_wait_us_32(duration_us);

    /* Scan the ring buffer for edges on our pins */
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

#endif /* HW_H */
