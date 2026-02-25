/*
 * telemetry.h — Observing the Observer
 *
 * Problem: 32KB wire graph updates every millisecond in SRAM.
 * We need to pipe it out for visualization without polluting
 * the observer's timeline.
 *
 * Solution: Second PIO + DMA. Zero CPU involvement after trigger.
 *
 * Architecture:
 *   PIO0 (pio_retina) — samples all GPIO at 125MHz (the retina)
 *   PIO1 (pio_tap)    — shifts telemetry out GPIO 28 at 2Mbps
 *   DMA chan 0         — retina: PIO0 RX → RAM (continuous)
 *   DMA chan 1         — tap:    RAM → PIO1 TX (triggered per frame)
 *
 * Timing budget per 1ms window:
 *   sense_observe:     ~400μs (wire_decay_all dominates)
 *   capture_for_dur:   ~1000μs (busy-wait on DMA)
 *   dead time:         ~600μs between sense_observe end and next capture
 *
 *   Wire graph snapshot: 128×128 = 16,384 bytes
 *   At 2Mbps: 16384 × 8 / 2,000,000 = 65.5ms to stream full matrix
 *
 *   So we DON'T stream the full matrix every tick. We stream a
 *   compressed delta or a subsampled slice. Options:
 *
 *   (a) Top-K edges only: ~256 edges × 3 bytes = 768 bytes = 3ms
 *   (b) Row-at-a-time: 128 bytes/tick = 128 ticks for full matrix
 *   (c) Changed edges only: typically <500 bytes = 2ms
 *
 *   We go with (b): one row per tick. Full matrix every 128ms.
 *   Simple. Deterministic. No compression logic eating CPU.
 *
 * Wire protocol out GPIO 28:
 *   [0xAA] [row_index] [128 bytes of edge weights] [checksum]
 *   = 131 bytes per frame at 2Mbps = 0.52ms. Fits in dead time.
 *
 * Isaac Oravec & Claude — February 2026
 */

#ifndef TELEMETRY_H
#define TELEMETRY_H

#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "wire.h"

/* ═══════════════════════════════════════════════════════════
 * CONFIG
 * ═══════════════════════════════════════════════════════════ */

#define TAP_PIN         28      /* GPIO 28: telemetry output */
#define TAP_BAUD        2000000 /* 2 Mbps serial out */

/* Frame format: SYNC + ROW_IDX + 128 edge bytes + CHECKSUM */
#define TAP_SYNC        0xAA
#define TAP_ROW_SIZE    WIRE_MAX_FEATURES  /* 128 */
#define TAP_FRAME_SIZE  (1 + 1 + TAP_ROW_SIZE + 1)  /* 131 bytes */

/* ═══════════════════════════════════════════════════════════
 * PIO PROGRAM — 8N1 serial transmitter
 *
 * Standard UART framing so any FTDI/CP2102 adapter reads it:
 *   [idle HIGH] [START=LOW] [8 data bits LSB first] [STOP=HIGH]
 *
 * .program tap_tx
 *   pull block       ; wait for data in TX FIFO
 *   set pins, 0 [7]  ; START bit (low), hold for 8 cycles = 1 bit time
 *   set x, 7         ; 8 data bits to shift
 * bitloop:
 *   out pins, 1 [6]  ; shift one data bit, hold for 7+1=8 cycles
 *   jmp x-- bitloop  ; loop (1 cycle, total = 8 per bit)
 *   set pins, 1 [7]  ; STOP bit (high), hold for 8 cycles
 *
 * Each bit = 8 PIO cycles. Clkdiv = sys_clk / (baud * 8).
 * At 125MHz, 2Mbps: div = 125e6 / (2e6 * 8) = 7.8125
 * Line idles high between bytes (PIO stalls on pull).
 * ═══════════════════════════════════════════════════════════ */

static const uint16_t tap_tx_instructions[] = {
    0x80a0,  /* 0: pull block           ; wait for TX FIFO */
    0xe700,  /* 1: set pins, 0 [7]      ; START bit, 8 cycles */
    0xe027,  /* 2: set x, 7             ; 8 bits to shift */
    0x6601,  /* 3: out pins, 1 [6]      ; data bit, 7+1=8 cycles */
    0x0043,  /* 4: jmp x--, 3           ; loop (will be patched to offset+3) */
    0xe701,  /* 5: set pins, 1 [7]      ; STOP bit, 8 cycles */
};

static const struct pio_program tap_tx_prog = {
    .instructions = tap_tx_instructions,
    .length = 6,
    .origin = -1,
};

/* ═══════════════════════════════════════════════════════════
 * STATE
 * ═══════════════════════════════════════════════════════════ */

static PIO  pio_tap;
static uint tap_sm;
static int  tap_dma;

/* Double-buffered frame: CPU writes one, DMA reads the other */
static uint8_t tap_frame[2][TAP_FRAME_SIZE];
static uint8_t tap_buf_idx = 0;
static uint8_t tap_row_cursor = 0;  /* which row to stream next */

/* ═══════════════════════════════════════════════════════════
 * INIT — configure PIO1 as serial transmitter on GPIO 28
 * ═══════════════════════════════════════════════════════════ */

static void telemetry_init(void) {
    pio_tap = pio1;
    tap_sm = pio_claim_unused_sm(pio_tap, true);
    uint offset = pio_add_program(pio_tap, &tap_tx_prog);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset, offset + tap_tx_prog.length - 1);

    /* Output on TAP_PIN */
    sm_config_set_out_pins(&c, TAP_PIN, 1);
    sm_config_set_set_pins(&c, TAP_PIN, 1);

    /* Shift config: 8 bits, shift right (LSB first), autopull */
    sm_config_set_out_shift(&c, true, true, 8);

    /* Baud rate: each bit = 8 PIO cycles
     * divider = sys_clk / (baud × 8)
     * 125MHz / (2Mbps × 8) = 7.8125 */
    float div = (float)clock_get_hz(clk_sys) / (TAP_BAUD * 8.0f);
    sm_config_set_clkdiv(&c, div);

    /* Configure GPIO 28 as PIO output, idle HIGH (UART idle state) */
    pio_gpio_init(pio_tap, TAP_PIN);
    gpio_set_dir(TAP_PIN, GPIO_OUT);
    gpio_put(TAP_PIN, 1);  /* line idles high before PIO takes over */

    /* Patch jmp target: jmp x--, offset+3 (the out instruction) */
    pio_tap->instr_mem[offset + 4] = 0x0040 | (offset + 3);

    pio_sm_init(pio_tap, tap_sm, offset, &c);

    /* Force line high via set pins before enabling */
    pio_sm_exec(pio_tap, tap_sm, 0xe001);  /* set pins, 1 — idle high */

    pio_sm_init(pio_tap, tap_sm, offset, &c);

    /* DMA: RAM → PIO1 TX FIFO, one-shot per frame */
    tap_dma = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(tap_dma);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_8);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(pio_tap, tap_sm, true));

    dma_channel_configure(tap_dma, &dc,
        &pio_tap->txf[tap_sm],   /* write: PIO1 TX FIFO */
        tap_frame[0],             /* read: frame buffer (updated per trigger) */
        TAP_FRAME_SIZE,           /* count: one frame */
        false);                   /* don't start yet */

    /* Enable PIO state machine */
    pio_sm_set_enabled(pio_tap, tap_sm, true);

    /* Init frame buffers */
    memset(tap_frame, 0, sizeof(tap_frame));
}

/* ═══════════════════════════════════════════════════════════
 * TRIGGER — snapshot one row of wire graph, fire DMA
 *
 * Called at the END of sense_observe(), in the dead time
 * before the next capture. CPU cost: ~2μs to fill frame
 * buffer. Then DMA takes over. CPU is free.
 *
 * Streams one row per tick. Full 128×128 matrix every 128ms.
 * ═══════════════════════════════════════════════════════════ */

static void telemetry_tap(const wire_graph_t *w) {
    /* Wait for previous DMA to finish (should already be done) */
    dma_channel_wait_for_finish_blocking(tap_dma);

    /* Build frame in the inactive buffer */
    uint8_t *f = tap_frame[tap_buf_idx];
    uint8_t row = tap_row_cursor;

    f[0] = TAP_SYNC;
    f[1] = row;

    /* Copy one row of edge weights */
    memcpy(&f[2], w->edge[row], TAP_ROW_SIZE);

    /* Checksum: XOR of all payload bytes */
    uint8_t cksum = row;
    for (int i = 0; i < TAP_ROW_SIZE; i++) {
        cksum ^= w->edge[row][i];
    }
    f[2 + TAP_ROW_SIZE] = cksum;

    /* Point DMA at this buffer and fire */
    dma_channel_set_read_addr(tap_dma, f, true);

    /* Swap buffers, advance row cursor */
    tap_buf_idx ^= 1;
    tap_row_cursor = (tap_row_cursor + 1) & (WIRE_MAX_FEATURES - 1);

    /* CPU returns immediately. DMA + PIO1 handle the rest.
     * 131 bytes at 2Mbps = 0.52ms. Finishes before next
     * sense_observe completes. Zero contention. */
}

#endif /* TELEMETRY_H */
