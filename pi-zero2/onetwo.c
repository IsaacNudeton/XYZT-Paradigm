/*
 * onetwo.c — GPIO Capture (bare metal)
 *
 * ONETWO IS the grid. Every position predicts, computes residue.
 * This file is just the physical world → grid input path.
 *
 * Isaac Oravec & Claude — February 2026
 */

#include "xyzt_os.h"
#include "hw.h"

/* =========================================================================
 * GPIO SIGNAL CAPTURE — physical world observation
 * ========================================================================= */

void capture_init(capture_buf_t *buf, uint32_t pin_mask) {
    buf->head = 0;
    buf->count = 0;
    buf->pin_mask = pin_mask;
    for (int p = 0; p < 28; p++) {
        if (pin_mask & (1 << p)) {
            gpio_set_function(p, GPIO_FUNC_INPUT);
            gpio_set_pull(p, 1);
            gpio_enable_edge_detect(p, 1, 1);
        }
    }
}

void capture_for_duration(capture_buf_t *buf, uint64_t duration_us) {
    uint64_t start = timer_get_ticks();
    uint64_t end = start + duration_us;
    uint32_t prev_state = gpio_read_all() & buf->pin_mask;
    while (timer_get_ticks() < end) {
        uint32_t state = gpio_read_all() & buf->pin_mask;
        if (state != prev_state) {
            if (buf->count < CAPTURE_BUF_SIZE) {
                buf->events[buf->count].timestamp = timer_get_ticks();
                buf->events[buf->count].pin_state = state;
                buf->count++;
            }
            prev_state = state;
        }
    }
}

void sense_decompose(capture_buf_t *buf, sense_result_t *result) {
    result->active_lines = 0;
    result->idle_state = 0;
    result->min_period_us = 0xFFFFFFFFFFFFFFFFULL;
    result->max_period_us = 0;
    result->has_clock_line = 0;
    result->clock_pin = 0;
    result->bits_per_frame = 0;
    result->protocol_name = "UNKNOWN";
    result->confidence = 0;
    if (buf->count < 2) return;

    uint32_t changed = 0;
    for (uint32_t i = 1; i < buf->count; i++)
        changed |= buf->events[i].pin_state ^ buf->events[i - 1].pin_state;
    for (int p = 0; p < 28; p++)
        if (changed & (1 << p)) result->active_lines++;
    result->idle_state = buf->events[0].pin_state;

    for (uint32_t i = 1; i < buf->count; i++) {
        uint64_t dt = buf->events[i].timestamp - buf->events[i - 1].timestamp;
        if (dt < result->min_period_us) result->min_period_us = dt;
        if (dt > result->max_period_us) result->max_period_us = dt;
    }

    /* Clock detection — look for regular toggling */
    for (int p = 0; p < 28; p++) {
        if (!(changed & (1 << p))) continue;
        int transitions = 0;
        uint64_t sum_dt = 0, prev_t = 0;
        int first = 1;
        for (uint32_t i = 1; i < buf->count; i++) {
            if ((buf->events[i].pin_state ^ buf->events[i - 1].pin_state) & (1 << p)) {
                transitions++;
                if (!first) sum_dt += buf->events[i].timestamp - prev_t;
                prev_t = buf->events[i].timestamp;
                first = 0;
            }
        }
        if (transitions > 4) {
            uint64_t avg = sum_dt / (transitions > 1 ? (uint64_t)(transitions - 1) : 1);
            int regular = 1;
            prev_t = 0; first = 1;
            for (uint32_t i = 1; i < buf->count && regular; i++) {
                if ((buf->events[i].pin_state ^ buf->events[i - 1].pin_state) & (1 << p)) {
                    if (!first) {
                        uint64_t dt = buf->events[i].timestamp - prev_t;
                        if (dt > avg * 6 / 5 || dt < avg * 4 / 5) regular = 0;
                    }
                    prev_t = buf->events[i].timestamp;
                    first = 0;
                }
            }
            if (regular) { result->has_clock_line = 1; result->clock_pin = p; break; }
        }
    }

    if (result->active_lines == 2 && result->has_clock_line)
        { result->protocol_name = "I2C"; result->bits_per_frame = 9; result->confidence = 75; }
    else if (result->active_lines >= 3 && result->has_clock_line)
        { result->protocol_name = "SPI"; result->bits_per_frame = 8; result->confidence = 70; }
    else if (result->active_lines == 1 && !result->has_clock_line)
        { result->protocol_name = "UART"; result->bits_per_frame = 10; result->confidence = 60; }
}

void sense_print(sense_result_t *result) {
    uart_puts("  Sense: ");
    uart_puts(result->protocol_name);
    uart_puts(" ("); uart_dec(result->confidence); uart_puts("%)");
    uart_puts("  lines="); uart_dec(result->active_lines);
    uart_puts("  clock="); uart_puts(result->has_clock_line ? "yes" : "no");
    uart_puts("\n");
}
