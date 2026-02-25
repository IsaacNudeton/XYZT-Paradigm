/*
 * XYZT-OS v6 — Pure Co-Presence, On Metal
 *
 * Three axioms:
 *   1. Distinction exists (marked or not)
 *   2. Position is identity (where = what)
 *   3. Observation creates meaning (observer interprets co-presence)
 *
 * Same soul as the Pico. Different body.
 * Pi Zero 2: Cortex-A53, 512MB RAM, 1GHz, 26 usable GPIO.
 * No PIO — CPU polls GPIO directly. System timer for timestamps.
 * UART on GPIO 14/15 for telemetry (what PIO1+GPIO28 does on Pico).
 *
 * Isaac Oravec & Claude — February 2026
 */

#ifndef XYZT_OS_H
#define XYZT_OS_H

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * LAYER 0: HARDWARE (drivers.c)
 * ========================================================================= */

#define GPIO_FUNC_INPUT  0
#define GPIO_FUNC_OUTPUT 1

void     gpio_set_function(uint32_t pin, uint32_t func);
void     gpio_set(uint32_t pin);
void     gpio_clear(uint32_t pin);
uint32_t gpio_read(uint32_t pin);
uint32_t gpio_read_all(void);
void     gpio_set_pull(uint32_t pin, uint32_t pull);
void     gpio_enable_edge_detect(uint32_t pin, int rising, int falling);
uint32_t gpio_check_event(uint32_t pin);
uint64_t timer_get_ticks(void);
void     timer_wait_us(uint64_t us);
void     uart_init(void);
void     uart_putc(char c);
char     uart_getc(void);
int      uart_data_ready(void);
void     uart_puts(const char *s);
void     uart_hex(uint32_t val);
void     uart_hex64(uint64_t val);
void     uart_dec(uint32_t val);
void     uart_dec_signed(int32_t val);

/* =========================================================================
 * PIN LAYOUT
 *
 * 28 GPIO total. Pins 14/15 = UART (telemetry). 26 usable.
 * All start as input. Body discovery classifies them.
 * ========================================================================= */

#define OBS_PIN_COUNT   28
#define OBS_PIN_MASK    0x0FFFFFFFu  /* bits 0-27 */
#define UART_PIN_MASK   ((1u << 14) | (1u << 15))  /* reserved for UART */
#define SENSE_PIN_MASK  (OBS_PIN_MASK & ~UART_PIN_MASK)  /* 26 usable pins */

/* =========================================================================
 * BODY MAP — discovered at boot
 * ========================================================================= */

#define PIN_UNKNOWN    0
#define PIN_FLOATING   1
#define PIN_WORLD_IN   2
#define PIN_SELF_LINK  3
#define PIN_OUTPUT     4
#define PIN_RESERVED   5   /* UART pins */

typedef struct {
    uint8_t  role[OBS_PIN_COUNT];
    uint8_t  responds_to[OBS_PIN_COUNT];
    uint32_t world_activity[OBS_PIN_COUNT];
    uint32_t probe_response[OBS_PIN_COUNT];
    uint8_t  n_world;
    uint8_t  n_self;
    uint8_t  n_floating;
    uint8_t  probed;
} body_map_t;

/* =========================================================================
 * ENGINE — same as Pico, inlined for bare-metal
 * ========================================================================= */

#define GRID_SIZE 64

typedef struct {
    uint8_t  marks[GRID_SIZE];
    uint8_t  snap[GRID_SIZE];
    uint8_t  co_present[GRID_SIZE];
    uint32_t tick_count;
    uint16_t pop;
    uint16_t co_pop;
} xyzt_grid_t;

/* =========================================================================
 * WIRE GRAPH — same as Pico
 * ========================================================================= */

#define WIRE_MAX_FEATURES 128
#define WIRE_STRENGTHEN   8
#define WIRE_DECAY        1
#define WIRE_SATURATE     255

typedef struct {
    uint8_t  edge[WIRE_MAX_FEATURES][WIRE_MAX_FEATURES];
    uint32_t fire_count[WIRE_MAX_FEATURES];
    uint16_t n_features;
} wire_graph_t;

/* =========================================================================
 * CAPTURE BUFFER
 * ========================================================================= */

#define CAPTURE_MAX_EDGES 4096

typedef struct {
    uint64_t timestamp_us;  /* microseconds from system timer */
    uint32_t pin_state;
} edge_event_t;

typedef struct {
    edge_event_t edges[CAPTURE_MAX_EDGES];
    uint32_t count;
} capture_buf_t;

/* =========================================================================
 * FEATURE SLOTS — match Pico sense.c exactly
 * ========================================================================= */

#define FEAT_REG_BASE    0
#define FEAT_REG_COUNT   16
#define FEAT_CORR_BASE   16
#define FEAT_CORR_COUNT  8
#define FEAT_BURST_BASE  24
#define FEAT_BURST_COUNT 8
#define FEAT_PAT_BASE    32
#define FEAT_PAT_COUNT   8
#define FEAT_ACT_BASE    40
#define FEAT_ACT_COUNT   8
#define FEAT_SELF_BASE   48
#define FEAT_SELF_COUNT  8
#define FEAT_OUT_BASE    56
#define FEAT_OUT_COUNT   8

#endif
