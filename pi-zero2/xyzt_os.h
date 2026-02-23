/*
 * XYZT-OS v6 — Pure Co-Presence, On Metal
 *
 * Three axioms:
 *   1. Distinction exists (marked or not)
 *   2. Position is identity (where = what)
 *   3. Observation creates meaning (observer interprets co-presence)
 *
 * The engine routes. The observer decides.
 *
 * Isaac Oravec & Claude — February 2026
 */

#ifndef XYZT_OS_H
#define XYZT_OS_H

#include <stdint.h>
#include <stddef.h>
#include "v6_core.h"

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
 * LAYER 1: GRID LAYOUT — Pi Zero 2
 *
 * [0-27]   GPIO bridge (28 pins, 14/15 reserved for UART)
 * [28-43]  User data (16 positions)
 * [44-51]  Program word (8 positions)
 * [52-53]  Observer selector (2 bits)
 * [54]     Execution output
 * [55-63]  Scratch / expansion
 * ========================================================================= */

#define PI_GPIO_BASE    0
#define PI_GPIO_COUNT   28
#define PI_DATA_BASE    28
#define PI_DATA_WIDTH   16
#define PI_PROG_BASE    44
#define PI_PROG_WIDTH   8
#define PI_OBS_BASE     52
#define PI_OUT_POS      54
#define PI_SCRATCH_BASE 55

/* =========================================================================
 * LAYER 2: WIRE — Hebbian Graph (wire.c)
 * ========================================================================= */

#define WIRE_MAX_NODES   64
#define WIRE_MAX_EDGES   256
#define WIRE_NAME_LEN    48
#define W_INIT    128
#define W_LEARN   20
#define W_MIN     5
#define W_MAX     250

#define WNODE_FILE     0
#define WNODE_PROBLEM  1
#define WNODE_CONCEPT  2

typedef struct {
    char     name[WIRE_NAME_LEN];
    uint8_t  type;
    uint16_t hit_count;
    uint8_t  _pad;
} WireNode;

typedef struct {
    uint16_t src, dst;
    uint8_t  weight;
    uint8_t  passes, fails, _pad;
} WireEdge;

typedef struct {
    uint32_t n_nodes;
    uint32_t n_edges;
    uint32_t total_learns;
    WireNode nodes[WIRE_MAX_NODES];
    WireEdge edges[WIRE_MAX_EDGES];
} WireGraph;

void       wire_init(void);
int        wire_find(const char *name);
int        wire_add(const char *name, uint8_t type);
void       wire_connect(int a, int b);
void       wire_learn(int node_id, int pass);
int        wire_weight(int src, int dst);
void       wire_print(void);
void       wire_query(const char *name);
void       wire_reset(void);
WireGraph *wire_get(void);

/* =========================================================================
 * GPIO CAPTURE (onetwo.c)
 * ========================================================================= */

#define CAPTURE_BUF_SIZE 4096

typedef struct { uint64_t timestamp; uint32_t pin_state; } edge_event_t;

typedef struct {
    edge_event_t events[CAPTURE_BUF_SIZE];
    uint32_t head, count, pin_mask;
} capture_buf_t;

typedef struct {
    uint32_t active_lines, idle_state;
    uint64_t min_period_us, max_period_us;
    int has_clock_line;
    uint32_t clock_pin, bits_per_frame;
    const char *protocol_name;
    uint32_t confidence;
} sense_result_t;

void capture_init(capture_buf_t *buf, uint32_t pin_mask);
void capture_for_duration(capture_buf_t *buf, uint64_t duration_us);
void sense_decompose(capture_buf_t *buf, sense_result_t *result);
void sense_print(sense_result_t *result);

#endif
