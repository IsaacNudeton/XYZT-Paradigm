/*
 * sonify.c — Convert L-field topology to audio
 *
 * Each node's voxel neighborhood has a natural cavity frequency
 * determined by the L-field: f = alpha / (2 * width * sqrt(L_wire)).
 * Map these to audio. The engine's knowledge becomes a chord.
 *
 * Isaac Oravec, Claude (CC + Web), March 2026
 */

#include "engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern int yee_download_L(float *h_L, int n);

#define SAMPLE_RATE 44100
#define YEE_GX 64
#define YEE_GY 64
#define YEE_GZ 64
#define YEE_TOTAL (YEE_GX * YEE_GY * YEE_GZ)

typedef struct {
    char riff[4]; uint32_t file_size; char wave[4]; char fmt[4];
    uint32_t fmt_size; uint16_t format; uint16_t channels;
    uint32_t sample_rate; uint32_t byte_rate;
    uint16_t block_align; uint16_t bits_per_sample;
    char data[4]; uint32_t data_size;
} WavHeader;

static void write_wav(const char *path, int16_t *samples, int n) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    WavHeader h;
    memcpy(h.riff, "RIFF", 4); h.file_size = 36 + n * 2;
    memcpy(h.wave, "WAVE", 4); memcpy(h.fmt, "fmt ", 4);
    h.fmt_size = 16; h.format = 1; h.channels = 1;
    h.sample_rate = SAMPLE_RATE; h.byte_rate = SAMPLE_RATE * 2;
    h.block_align = 2; h.bits_per_sample = 16;
    memcpy(h.data, "data", 4); h.data_size = n * 2;
    fwrite(&h, sizeof(h), 1, f);
    fwrite(samples, sizeof(int16_t), n, f);
    fclose(f);
}

int sonify_engine(Engine *eng, const char *chord_path, const char *dream_path) {
    Graph *g0 = &eng->shells[0].g;
    if (g0->n_nodes == 0) return -1;

    float *h_L = (float *)malloc(YEE_TOTAL * sizeof(float));
    if (!h_L) return -1;
    yee_download_L(h_L, YEE_TOTAL);

    /* Measure each node's local cavity: count consecutive low-L voxels */
    typedef struct { double freq; double amp; double sustain; char name[32]; } Voice;
    Voice voices[64];
    int n_voices = 0;

    for (int i = 0; i < g0->n_nodes && n_voices < 64; i++) {
        Node *n = &g0->nodes[i];
        if (!n->alive || n->layer_zero) continue;
        if (n->name[0] == '_') continue;  /* skip self-observations */

        int gx = coord_x(n->coord) % YEE_GX;
        int gy = coord_y(n->coord) % YEE_GY;
        int gz = coord_z(n->coord) % YEE_GZ;
        int vid = gx + gy * YEE_GX + gz * YEE_GX * YEE_GY;

        float L_center = h_L[vid];

        /* Measure cavity width: count low-L voxels along X axis */
        int width = 1;
        for (int dx = 1; dx < 16; dx++) {
            int nx = (gx + dx) % YEE_GX;
            if (h_L[nx + gy * YEE_GX + gz * YEE_GX * YEE_GY] < 1.0f) width++;
            else break;
        }
        for (int dx = 1; dx < 16; dx++) {
            int nx = (gx - dx + YEE_GX) % YEE_GX;
            if (h_L[nx + gy * YEE_GX + gz * YEE_GX * YEE_GY] < 1.0f) width++;
            else break;
        }
        if (width < 1) width = 1;

        /* Natural frequency: f = alpha / (2 * width * sqrt(L)) */
        double c_speed = 0.5 / sqrt((double)L_center);
        double f_natural = c_speed / (2.0 * width);

        /* Map to audio: [0.01, 0.1] → [80, 800] Hz */
        double f_audio = 80.0 + (f_natural - 0.01) * 720.0 / 0.09;
        if (f_audio < 40) f_audio = 40;
        if (f_audio > 2000) f_audio = 2000;

        voices[n_voices].freq = f_audio;
        voices[n_voices].amp = (double)n->valence / 255.0;
        voices[n_voices].sustain = 1.0 + (double)crystal_strength(n) / 30.0;
        strncpy(voices[n_voices].name, n->name, 31);
        n_voices++;
    }

    printf("  %d voices from L-field topology\n", n_voices);

    /* Generate chord WAV (10 seconds) */
    int chord_len = SAMPLE_RATE * 10;
    int16_t *chord = (int16_t *)calloc(chord_len, sizeof(int16_t));
    for (int s = 0; s < chord_len; s++) {
        double t = (double)s / SAMPLE_RATE;
        double sum = 0;
        for (int v = 0; v < n_voices; v++) {
            double env = voices[v].amp * exp(-t / voices[v].sustain);
            sum += env * sin(2.0 * 3.14159265 * voices[v].freq * t);
        }
        sum *= 2000.0 / (n_voices > 0 ? sqrt((double)n_voices) : 1.0);
        if (sum > 32000) sum = 32000;
        if (sum < -32000) sum = -32000;
        chord[s] = (int16_t)sum;
    }
    write_wav(chord_path, chord, chord_len);
    printf("  Chord: %s (%d voices, 10 sec)\n", chord_path, n_voices);

    /* Generate dream WAV (30 seconds — deeper voices sustain longer) */
    int dream_len = SAMPLE_RATE * 30;
    int16_t *dream = (int16_t *)calloc(dream_len, sizeof(int16_t));
    for (int s = 0; s < dream_len; s++) {
        double t = (double)s / SAMPLE_RATE;
        double sum = 0;
        for (int v = 0; v < n_voices; v++) {
            double dream_sustain = voices[v].sustain * (1.0 + 2.0 * voices[v].amp);
            double env = voices[v].amp * exp(-t / dream_sustain);
            if (env < 0.005) continue;
            sum += env * sin(2.0 * 3.14159265 * voices[v].freq * t);
        }
        sum *= 2000.0 / (n_voices > 0 ? sqrt((double)n_voices) : 1.0);
        if (sum > 32000) sum = 32000;
        if (sum < -32000) sum = -32000;
        dream[s] = (int16_t)sum;
    }
    write_wav(dream_path, dream, dream_len);
    printf("  Dream: %s (%d voices, 30 sec)\n", dream_path, n_voices);

    free(chord);
    free(dream);
    free(h_L);
    return n_voices;
}
