/*
 * transducer.h — Input layer: file ingestion
 *
 * Isaac Oravec & Claude, March 2026
 */

#ifndef TRANSDUCER_H
#define TRANSDUCER_H

#include "engine.h"

/* Read a file and produce a raw-byte-encoded BitStream */
int transducer_file(const char *path, BitStream *out);

#endif /* TRANSDUCER_H */
