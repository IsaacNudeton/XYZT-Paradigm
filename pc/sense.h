/*
 * sense.h — Temporal feature extraction for PC engine
 *
 * Port of shared/sense.c for byte-stream input.
 * Detects: ACTIVE, REGULARITY, DUTY, CORRELATION, ASYMMETRY, BURST, PERIOD
 * Creates graph nodes + Hebbian edges in shell 0.
 *
 * Isaac Oravec & Claude, February 2026
 */
#ifndef SENSE_H
#define SENSE_H

#include "engine.h"

#define SENSE_CHANNELS   8
#define SENSE_MAX_EVENTS 1024

typedef struct {
    uint8_t  value;
    uint64_t timestamp;
} sense_event_t;

typedef struct {
    sense_event_t events[SENSE_MAX_EVENTS];
    uint32_t count;
} sense_buf_t;

/* ── Per-pass region descriptor ──────────────────────── */
#define SENSE_MAX_REGIONS 4

typedef struct {
    int offset;     /* byte offset into state_buf */
    int length;     /* byte count for this region */
    int pass_id;    /* 1-based pass number */
} sense_region_t;

/* Fill sense_buf from a raw byte buffer (timestamp = index) */
void sense_fill(sense_buf_t *buf, const uint8_t *data, int len);

/* Extract features, create graph nodes, Hebbian-wire co-occurring features */
void sense_extract(Engine *eng, sense_buf_t *buf, sense_result_t *result);

/* Decay edges of sense nodes not present in this pass */
void sense_decay(Engine *eng, const sense_result_t *result);

/* Observer feedback: GPU co-presence -> sense node valence */
void sense_feedback(Engine *eng, const sense_result_t *result);

/* Convenience: fill + extract + decay */
void sense_pass(Engine *eng, const uint8_t *data, int len, sense_result_t *result);

/* Extract features from a single region with pass_id suffix on names */
void sense_extract_region(Engine *eng, const uint8_t *data, int len,
                          int pass_id, sense_result_t *result);

/* Windowed: extract per-region, cross-wire, single decay */
void sense_pass_windowed(Engine *eng, const uint8_t *data,
                         const sense_region_t *regions, int n_regions,
                         sense_result_t *result);

#endif /* SENSE_H */
