/*
 * telemetry.h — Observing the Observer
 *
 * PIO1 + DMA shifts the wire graph out GPIO 28 at 2Mbps.
 * One row per tick. Full 64x64 matrix every 64ms.
 * Zero CPU overhead after trigger.
 *
 * Wire protocol out GPIO 28:
 *   [0xAA] [flags:row_index] [64 bytes of edge weights] [checksum]
 *   = 67 bytes per frame at 2Mbps = 0.27ms
 *
 * flags byte: bit 7 = converged, bits 0-6 = row index
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

#define TAP_PIN         28
#define TAP_BAUD        2000000

#define TAP_SYNC        0xAA
#define TAP_ROW_SIZE    WIRE_MAX_FEATURES  /* 64 */
#define TAP_FRAME_SIZE  (1 + 1 + TAP_ROW_SIZE + 1)  /* 67 bytes */

/* ═══════════════════════════════════════════════════════════
 * PIO PROGRAM — 8N1 serial transmitter
 * ═══════════════════════════════════════════════════════════ */

static const uint16_t tap_tx_instructions[] = {
    0x80a0,  /* 0: pull block */
    0xe700,  /* 1: set pins, 0 [7]  — START bit */
    0xe027,  /* 2: set x, 7         — 8 bits */
    0x6601,  /* 3: out pins, 1 [6]  — data bit */
    0x0043,  /* 4: jmp x--, 3       — (patched to offset+3) */
    0xe701,  /* 5: set pins, 1 [7]  — STOP bit */
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

static uint8_t tap_frame[2][TAP_FRAME_SIZE];
static uint8_t tap_buf_idx = 0;
static uint8_t tap_row_cursor = 0;

/* ═══════════════════════════════════════════════════════════
 * INIT
 * ═══════════════════════════════════════════════════════════ */

static void telemetry_init(void) {
    pio_tap = pio1;
    tap_sm = pio_claim_unused_sm(pio_tap, true);
    uint offset = pio_add_program(pio_tap, &tap_tx_prog);

    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset, offset + tap_tx_prog.length - 1);
    sm_config_set_out_pins(&c, TAP_PIN, 1);
    sm_config_set_set_pins(&c, TAP_PIN, 1);
    sm_config_set_out_shift(&c, true, true, 8);

    float div = (float)clock_get_hz(clk_sys) / (TAP_BAUD * 8.0f);
    sm_config_set_clkdiv(&c, div);

    pio_gpio_init(pio_tap, TAP_PIN);
    gpio_set_dir(TAP_PIN, GPIO_OUT);
    gpio_put(TAP_PIN, 1);

    pio_tap->instr_mem[offset + 4] = 0x0040 | (offset + 3);

    pio_sm_init(pio_tap, tap_sm, offset, &c);
    pio_sm_exec(pio_tap, tap_sm, 0xe001);  /* set pins, 1 — idle high */
    pio_sm_init(pio_tap, tap_sm, offset, &c);

    tap_dma = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(tap_dma);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_8);
    channel_config_set_read_increment(&dc, true);
    channel_config_set_write_increment(&dc, false);
    channel_config_set_dreq(&dc, pio_get_dreq(pio_tap, tap_sm, true));

    dma_channel_configure(tap_dma, &dc,
        &pio_tap->txf[tap_sm],
        tap_frame[0],
        TAP_FRAME_SIZE,
        false);

    pio_sm_set_enabled(pio_tap, tap_sm, true);
    memset(tap_frame, 0, sizeof(tap_frame));
}

/* ═══════════════════════════════════════════════════════════
 * TRIGGER — snapshot one row of wire graph, fire DMA
 *
 * Streams one row per tick. Full 64x64 matrix every 64ms.
 * ═══════════════════════════════════════════════════════════ */

static void telemetry_tap(const wire_graph_t *w, uint8_t converged) {
    dma_channel_wait_for_finish_blocking(tap_dma);

    uint8_t *f = tap_frame[tap_buf_idx];
    uint8_t row = tap_row_cursor;

    f[0] = TAP_SYNC;
    f[1] = row | (converged ? 0x80 : 0x00);

    memcpy(&f[2], w->edge[row], TAP_ROW_SIZE);

    uint8_t cksum = f[1];
    for (int i = 0; i < TAP_ROW_SIZE; i++) {
        cksum ^= w->edge[row][i];
    }
    f[2 + TAP_ROW_SIZE] = cksum;

    dma_channel_set_read_addr(tap_dma, f, true);

    tap_buf_idx ^= 1;
    tap_row_cursor = (tap_row_cursor + 1) & (WIRE_MAX_FEATURES - 1);
}

#endif /* TELEMETRY_H */
