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
#define SENSE_MAX_EVENTS 512

typedef struct {
    uint8_t  value;
    uint64_t timestamp;
} sense_event_t;

typedef struct {
    sense_event_t events[SENSE_MAX_EVENTS];
    uint32_t count;
} sense_buf_t;

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

#endif /* SENSE_H */
