/*
 * sense.h — Sense feedback
 *
 * Isaac Oravec & Claude, March 2026
 */
#ifndef SENSE_H
#define SENSE_H

#include "engine.h"

/* Observer feedback: GPU co-presence -> sense node valence */
void sense_feedback(Engine *eng, const sense_result_t *result);

#endif /* SENSE_H */
