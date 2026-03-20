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

/* Generate a short audio snapshot (1 second) of the current L-field state.
 * Appends to an open WAV file. Call repeatedly during learning to build
 * a real-time audio trace of the engine's knowledge evolving. */
int sonify_snapshot(Engine *eng, FILE *wav_file, int *total_samples);

#endif
