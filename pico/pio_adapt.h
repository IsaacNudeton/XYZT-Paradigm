/*
 * pio_adapt.h — PIO sample buffer → capture_buf_t adapter
 *
 * The Pico's PIO sampler captures raw pin snapshots at fixed intervals.
 * sense.c expects capture_buf_t with edge timestamps.
 * This adapter scans the PIO buffer, detects edges, and builds
 * the capture buffer sense_extract needs.
 *
 * PIO sample: uint32_t with pin states at time (index * sample_period_us)
 * capture_buf_t: edge_event_t array with {timestamp_us, pin_state}
 *
 * Isaac Oravec & Claude — February 2026
 */

#ifndef PIO_ADAPT_H
#define PIO_ADAPT_H

#include <stdint.h>

/* These types must match xyzt_os.h / sense.c */
#ifndef CAPTURE_BUF_SIZE
#define CAPTURE_BUF_SIZE 4096
#endif

typedef struct { uint64_t timestamp; uint32_t pin_state; } edge_event_t;
typedef struct {
    edge_event_t events[CAPTURE_BUF_SIZE];
    uint32_t head, count, pin_mask;
} capture_buf_t;

/*
 * Convert PIO sample buffer to capture_buf_t.
 *
 * samples:    raw PIO DMA buffer (lower 16 bits = pin states)
 * n_samples:  number of valid samples
 * period_us:  microseconds per sample (= clock_div / 125.0)
 * pin_mask:   which pins to watch (e.g. 0xFFFF for all 16)
 * out:        output capture buffer
 *
 * Only records edges (state changes). Skips identical consecutive samples.
 * Timestamps are synthetic: sample_index * period_us.
 */
static inline void pio_to_capture(
    const uint32_t *samples,
    int n_samples,
    float period_us,
    uint32_t pin_mask,
    capture_buf_t *out)
{
    out->head = 0;
    out->count = 0;
    out->pin_mask = pin_mask;

    if (n_samples <= 0) return;

    /* First sample always recorded (establishes baseline) */
    uint32_t prev = samples[0] & pin_mask;
    out->events[0].timestamp = 0;
    out->events[0].pin_state = prev;
    out->count = 1;

    for (int i = 1; i < n_samples && out->count < CAPTURE_BUF_SIZE; i++) {
        uint32_t curr = samples[i] & pin_mask;
        if (curr != prev) {
            out->events[out->count].timestamp = (uint64_t)(i * period_us);
            out->events[out->count].pin_state = curr;
            out->count++;
            prev = curr;
        }
    }
}

#endif /* PIO_ADAPT_H */
