/*
 * transducer.h — Input abstraction
 *
 * v3 analog->binary with auto-calibrating threshold.
 * Supports: stdin text, file, serial port, raw byte arrays.
 * Output: BitStream compatible with engine_ingest.
 *
 * Isaac Oravec & Claude, February 2026
 */

#ifndef TRANSDUCER_H
#define TRANSDUCER_H

#include "engine.h"

/* Transducer modes */
#define XDUCE_STDIN   0
#define XDUCE_FILE    1
#define XDUCE_SERIAL  2
#define XDUCE_RAW     3

typedef struct {
    int     mode;
    char    path[256];      /* file path or serial port (e.g. COM7) */
    int     baud;           /* serial baud rate */
    /* Auto-calibration state */
    int     threshold;      /* current binary threshold (0-255) */
    int     min_seen;       /* min value seen */
    int     max_seen;       /* max value seen */
    int     n_samples;      /* total samples for calibration */
    int64_t sum;            /* running sum for mean */
} Transducer;

/* Initialize transducer in given mode */
void transducer_init(Transducer *t, int mode, const char *path, int baud);

/* Read raw bytes from source. Returns bytes read, -1 on error. */
int transducer_read(Transducer *t, uint8_t *buf, int max_bytes);

/* Convert raw bytes to BitStream using auto-calibrated threshold.
 * Each byte above threshold -> 1, below -> 0. */
void transducer_to_bits(Transducer *t, const uint8_t *raw, int len, BitStream *out);

/* Full pipeline: read -> calibrate -> convert to BitStream */
int transducer_ingest(Transducer *t, BitStream *out);

/* Read a file and produce an ONETWO-encoded BitStream */
int transducer_file(const char *path, BitStream *out);

/* Read from stdin (one line) and produce a BitStream */
int transducer_stdin(BitStream *out);

#endif /* TRANSDUCER_H */
