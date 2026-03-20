/*
 * sonify.h — Convert L-field topology to audio WAV files
 */
#ifndef SONIFY_H
#define SONIFY_H
#include "engine.h"

/* Generate chord (all knowledge) and dream (fading) WAV files
 * from the engine's current L-field topology.
 * Returns number of voices (nodes converted to tones). */
int sonify_engine(Engine *eng, const char *chord_path, const char *dream_path);

#endif
