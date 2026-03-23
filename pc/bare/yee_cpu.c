/*
 * yee_cpu.c — Pure C Yee Grid Implementation
 *
 * Same physics as yee.cu. No CUDA. No GPU. No OS.
 * Runs on anything with memory and a clock.
 *
 * Isaac Oravec & Claude, March 2026
 */

#include "yee_cpu.h"
#include <string.h>
#include <math.h>

/* Accumulator decay: 63/64 per tick, ~44 tick half-life */
#define ACC_DECAY (63.0f / 64.0f)

void yee_cpu_init(YeeGrid *g) {
    memset(g->V,  0, sizeof(g->V));
    memset(g->Ix, 0, sizeof(g->Ix));
    memset(g->Iy, 0, sizeof(g->Iy));
    memset(g->Iz, 0, sizeof(g->Iz));
    memset(g->acc, 0, sizeof(g->acc));
    memset(g->output, 0, sizeof(g->output));
    for (int i = 0; i < YEE_N; i++)
        g->L[i] = YEE_L_WIRE;
}

void yee_cpu_clear(YeeGrid *g) {
    memset(g->V,  0, sizeof(g->V));
    memset(g->Ix, 0, sizeof(g->Ix));
    memset(g->Iy, 0, sizeof(g->Iy));
    memset(g->Iz, 0, sizeof(g->Iz));
    memset(g->acc, 0, sizeof(g->acc));
    memset(g->output, 0, sizeof(g->output));
}

void yee_cpu_tick(YeeGrid *g) {
    float inv_C = 1.0f / YEE_C;

    /* V update: dV/dt = -(1/C) * div(I) - (G/C)*V
     * Periodic boundaries: the grid is a 3-torus */
    for (int z = 0; z < YEE_GZ; z++)
        for (int y = 0; y < YEE_GY; y++)
            for (int x = 0; x < YEE_GX; x++) {
                int p = yee_idx(x, y, z);
                int xm = (x > 0) ? x - 1 : YEE_GX - 1;
                int ym = (y > 0) ? y - 1 : YEE_GY - 1;
                int zm = (z > 0) ? z - 1 : YEE_GZ - 1;

                float dIx = g->Ix[p] - g->Ix[yee_idx(xm, y, z)];
                float dIy = g->Iy[p] - g->Iy[yee_idx(x, ym, z)];
                float dIz = g->Iz[p] - g->Iz[yee_idx(x, y, zm)];

                float div_I = dIx + dIy + dIz;
                float dV = -inv_C * div_I - YEE_G * inv_C * g->V[p];
                g->V[p] += YEE_ALPHA * dV;
            }

    /* I update: dI/dt = -(1/L) * grad(V) - (R/L)*I
     * Periodic boundaries */
    for (int z = 0; z < YEE_GZ; z++)
        for (int y = 0; y < YEE_GY; y++)
            for (int x = 0; x < YEE_GX; x++) {
                int p = yee_idx(x, y, z);
                float inv_L = 1.0f / g->L[p];
                int xp = (x < YEE_GX - 1) ? x + 1 : 0;
                int yp = (y < YEE_GY - 1) ? y + 1 : 0;
                int zp = (z < YEE_GZ - 1) ? z + 1 : 0;

                float dVx = g->V[yee_idx(xp, y, z)] - g->V[p];
                g->Ix[p] += YEE_ALPHA * (-inv_L * dVx - YEE_R * inv_L * g->Ix[p]);

                float dVy = g->V[yee_idx(x, yp, z)] - g->V[p];
                g->Iy[p] += YEE_ALPHA * (-inv_L * dVy - YEE_R * inv_L * g->Iy[p]);

                float dVz = g->V[yee_idx(x, y, zp)] - g->V[p];
                g->Iz[p] += YEE_ALPHA * (-inv_L * dVz - YEE_R * inv_L * g->Iz[p]);
            }

    /* Accumulator: leaky integrator of |V| */
    for (int i = 0; i < YEE_N; i++) {
        g->acc[i] = ACC_DECAY * g->acc[i] + fabsf(g->V[i]);
    }
}

void yee_cpu_inject(YeeGrid *g, const YeeSrc *src, int n) {
    for (int i = 0; i < n; i++)
        g->V[src[i].voxel_id] += src[i].amplitude * src[i].strength;
}

void yee_cpu_sponge(YeeGrid *g, int width, float rate) {
    for (int z = 0; z < YEE_GZ; z++)
        for (int y = 0; y < YEE_GY; y++)
            for (int x = 0; x < YEE_GX; x++) {
                int dx = x < YEE_GX - 1 - x ? x : YEE_GX - 1 - x;
                int dy = y < YEE_GY - 1 - y ? y : YEE_GY - 1 - y;
                int dz = z < YEE_GZ - 1 - z ? z : YEE_GZ - 1 - z;
                int d = dx < dy ? dx : dy;
                if (dz < d) d = dz;

                if (d < width) {
                    int p = yee_idx(x, y, z);
                    float damp = 1.0f - rate * (float)(width - d) / (float)width;
                    float absorbed = g->V[p] * (1.0f - damp);
                    g->output[p] += fabsf(absorbed);
                    g->V[p]  *= damp;
                    g->Ix[p] *= damp;
                    g->Iy[p] *= damp;
                    g->Iz[p] *= damp;
                }
            }
}

void yee_cpu_hebbian(YeeGrid *g, float str_rate, float wk_rate) {
    /* Compute threshold from accumulator distribution */
    float acc_mean = 0, acc_max = 0;
    for (int i = 0; i < YEE_N; i++) {
        acc_mean += g->acc[i];
        if (g->acc[i] > acc_max) acc_max = g->acc[i];
    }
    acc_mean /= YEE_N;
    float threshold = (acc_mean + acc_max) * 0.5f;
    if (threshold < 0.01f) threshold = 0.01f;

    /* Strengthen where active, weaken where quiet */
    for (int i = 0; i < YEE_N; i++) {
        if (g->acc[i] > threshold) {
            float headroom = (g->L[i] - YEE_L_MIN) / (YEE_L_VAC - YEE_L_MIN);
            if (headroom < 0) headroom = 0;
            g->L[i] -= str_rate * headroom;
            if (g->L[i] < YEE_L_MIN) g->L[i] = YEE_L_MIN;
        } else {
            g->L[i] += wk_rate;
            if (g->L[i] > YEE_L_MAX) g->L[i] = YEE_L_MAX;
        }
    }
}

float yee_cpu_energy(const YeeGrid *g) {
    float e = 0;
    for (int i = 0; i < YEE_N; i++)
        e += g->V[i] * g->V[i];
    return e;
}

void yee_cpu_clear_output(YeeGrid *g) {
    memset(g->output, 0, sizeof(g->output));
}

void yee_cpu_retina_inject(YeeGrid *g, const uint8_t *data, int len, float amp) {
    int max_bytes = len < 64 ? len : 64;
    for (int z = 0; z < YEE_GZ; z += 2)
        for (int y = 0; y < YEE_GY; y += 2) {
            float val = 0;
            for (int i = 0; i < max_bytes; i++) {
                float freq = 6.2832f * (float)(i + 1) / (float)YEE_GY;
                float phase = (float)data[i] * 6.2832f / 256.0f;
                float amp_i = ((float)data[i] - 128.0f) / 128.0f;
                val += amp_i * sinf(freq * y + phase) *
                               cosf(freq * z + phase * 0.7f);
            }
            val *= amp / (float)max_bytes;
            if (fabsf(val) > 0.001f)
                g->V[yee_idx(0, y, z)] += val;
        }
}
