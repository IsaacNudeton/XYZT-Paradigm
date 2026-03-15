/*
 * yee.cu — 3D Yee Grid CUDA Kernels
 *
 * Kernel 1 (kernel_yee_V): Update V from I divergence.
 * Kernel 2 (kernel_yee_I): Update I from new V gradient.
 * Kernel 3 (kernel_yee_inject): Add source amplitudes to V.
 * Kernel 4 (kernel_yee_hebbian): Lower L where |V| is high.
 *
 * All arrays are flat global: V[262144], Ix/Iy/Iz[262144], L[262144].
 * One thread per voxel. No shared memory needed (global reads only).
 * Sync between V and I kernels ensures leapfrog stagger correctness.
 *
 * Isaac Oravec & Claude, March 2026
 */

#include <stdio.h>
#include <math.h>
#include "yee.cuh"

/* ══════════════════════════════════════════════════════════════
 * DEVICE STATE
 * ══════════════════════════════════════════════════════════════ */

static float *d_V  = NULL;   /* voltage (scalar at centers) */
static float *d_Ix = NULL;   /* current x-component (on x-faces) */
static float *d_Iy = NULL;   /* current y-component (on y-faces) */
static float *d_Iz = NULL;   /* current z-component (on z-faces) */
static float *d_L  = NULL;   /* inductance per voxel (learnable) */

/* Accumulator for Hebbian: tracks |V| integrated over time */
static float *d_V_accum = NULL;

/* Host-side scratch for reductions */
static float *h_scratch = NULL;

#define YEE_CHECK(call) { \
    cudaError_t err = call; \
    if (err != cudaSuccess) { \
        fprintf(stderr, "YEE CUDA error at %s:%d: %s\n", \
                __FILE__, __LINE__, cudaGetErrorString(err)); \
        return -1; \
    } \
}

/* ══════════════════════════════════════════════════════════════
 * KERNEL 1: UPDATE V FROM I DIVERGENCE
 *
 * dV/dt = -(1/C) * div(I) - (G/C)*V
 * div(I) = (Ix[x,y,z] - Ix[x-1,y,z]) + (Iy - Iy[y-1]) + (Iz - Iz[z-1])
 * Boundary: I outside grid = 0 (open circuit)
 * ══════════════════════════════════════════════════════════════ */

__global__ void kernel_yee_V(float *V, const float *Ix, const float *Iy,
                              const float *Iz, int n) {
    int p = blockIdx.x * blockDim.x + threadIdx.x;
    if (p >= n) return;

    int gx, gy, gz;
    yee_coords(p, &gx, &gy, &gz);

    float inv_C = 1.0f / YEE_C;

    /* dIx/dx: Ix[x,y,z] - Ix[x-1,y,z] */
    float dIx = Ix[p];
    if (gx > 0) dIx -= Ix[yee_idx(gx-1, gy, gz)];

    /* dIy/dy */
    float dIy = Iy[p];
    if (gy > 0) dIy -= Iy[yee_idx(gx, gy-1, gz)];

    /* dIz/dz */
    float dIz = Iz[p];
    if (gz > 0) dIz -= Iz[yee_idx(gx, gy, gz-1)];

    float div_I = dIx + dIy + dIz;
    float dV = -inv_C * div_I - YEE_G * inv_C * V[p];
    V[p] += YEE_ALPHA * dV;
}

/* ══════════════════════════════════════════════════════════════
 * KERNEL 2: UPDATE I FROM NEW V GRADIENT
 *
 * dIx/dt = -(1/L) * dV/dx - (R/L)*Ix
 * dV/dx at face (x,y,z) = V[x+1,y,z] - V[x,y,z]
 * Boundary: V outside grid = 0 (grounded)
 *
 * Uses NEW V from kernel 1 (leapfrog stagger).
 * ══════════════════════════════════════════════════════════════ */

__global__ void kernel_yee_I(const float *V, float *Ix, float *Iy,
                              float *Iz, const float *L, int n) {
    int p = blockIdx.x * blockDim.x + threadIdx.x;
    if (p >= n) return;

    int gx, gy, gz;
    yee_coords(p, &gx, &gy, &gz);

    float inv_L = 1.0f / L[p];

    /* Ix: dV/dx = V[x+1] - V[x] */
    if (gx < YEE_GX - 1) {
        float dVx = V[yee_idx(gx+1, gy, gz)] - V[p];
        Ix[p] += YEE_ALPHA * (-inv_L * dVx - YEE_R * inv_L * Ix[p]);
    }

    /* Iy: dV/dy = V[y+1] - V[y] */
    if (gy < YEE_GY - 1) {
        float dVy = V[yee_idx(gx, gy+1, gz)] - V[p];
        Iy[p] += YEE_ALPHA * (-inv_L * dVy - YEE_R * inv_L * Iy[p]);
    }

    /* Iz: dV/dz = V[z+1] - V[z] */
    if (gz < YEE_GZ - 1) {
        float dVz = V[yee_idx(gx, gy, gz+1)] - V[p];
        Iz[p] += YEE_ALPHA * (-inv_L * dVz - YEE_R * inv_L * Iz[p]);
    }
}

/* ══════════════════════════════════════════════════════════════
 * KERNEL 3: INJECT SOURCES
 *
 * V[voxel] += amplitude * strength
 * amplitude = node->val / VAL_CEILING (signal)
 * strength = node->valence / 255.0 (how hard it drives)
 * ══════════════════════════════════════════════════════════════ */

__global__ void kernel_yee_inject(float *V, const YeeSource *sources, int n_src) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n_src) return;

    int vid = sources[i].voxel_id;
    if (vid >= 0 && vid < YEE_N) {
        atomicAdd(&V[vid], sources[i].amplitude * sources[i].strength);
    }
}

/* ══════════════════════════════════════════════════════════════
 * KERNEL 4: LEAKY INTEGRATOR + HEBBIAN
 *
 * Every tick: acc[p] = acc[p] * decay + |V[p]|
 *
 * Decay = 63/64 ≈ 0.984375. After 137 ticks: 0.984^137 ≈ 0.12.
 * Effective window ≈ 50 ticks — smooths wave oscillations,
 * tracks which paths are actually active.
 *
 * This replaces the old substrate[] (0-255 Hebbian weight).
 * CPU reads acc via yee_download_acc(), normalized to 0-255.
 *
 * Hebbian (periodic): where acc is high, lower L (strengthen).
 *   Where acc is low, raise L (weaken).
 * ══════════════════════════════════════════════════════════════ */

#ifndef YEE_ACC_DECAY
#define YEE_ACC_DECAY  (63.0f / 64.0f)   /* ~1.6% per tick, 50-tick window */
#endif
#define YEE_ACC_SCALE  256.0f            /* calibrated: acc_ss~0.7 at V=0.01, *256→~180 */

__global__ void kernel_yee_accum(float *V_accum, const float *V, int n) {
    int p = blockIdx.x * blockDim.x + threadIdx.x;
    if (p >= n) return;
    V_accum[p] = V_accum[p] * YEE_ACC_DECAY + fabsf(V[p]);
}

__global__ void kernel_yee_hebbian(float *L, const float *V_accum,
                                    float strengthen_rate, float weaken_rate,
                                    float threshold, int n) {
    int p = blockIdx.x * blockDim.x + threadIdx.x;
    if (p >= n) return;

    float acc = V_accum[p];
    float l = L[p];

    if (acc > threshold) {
        /* Co-presence: lower L = strengthen = wire */
        l *= (1.0f - strengthen_rate);
        if (l < YEE_L_MIN) l = YEE_L_MIN;
    } else {
        /* Absence: raise L = weaken = dissolve wire */
        l *= (1.0f + weaken_rate);
        if (l > YEE_L_MAX) l = YEE_L_MAX;
    }
    L[p] = l;
}

/* ══════════════════════════════════════════════════════════════
 * KERNEL 5: ENERGY REDUCTION (for measurement)
 * ══════════════════════════════════════════════════════════════ */

__global__ void kernel_yee_energy(const float *V, const float *Ix,
                                   const float *Iy, const float *Iz,
                                   const float *L, float *out, int n) {
    __shared__ float sdata[YEE_BLOCK];
    int p = blockIdx.x * blockDim.x + threadIdx.x;
    int tid = threadIdx.x;

    float e = 0;
    if (p < n) {
        e += V[p] * V[p] * YEE_C;         /* electric */
        e += Ix[p] * Ix[p] * L[p];        /* magnetic x */
        e += Iy[p] * Iy[p] * L[p];        /* magnetic y */
        e += Iz[p] * Iz[p] * L[p];        /* magnetic z */
        e *= 0.5f;
    }
    sdata[tid] = e;
    __syncthreads();

    /* Block-level reduction */
    for (int s = blockDim.x / 2; s > 0; s >>= 1) {
        if (tid < s) sdata[tid] += sdata[tid + s];
        __syncthreads();
    }
    if (tid == 0) out[blockIdx.x] = sdata[0];
}

/* ══════════════════════════════════════════════════════════════
 * HOST API IMPLEMENTATION
 * ══════════════════════════════════════════════════════════════ */

extern "C" int yee_init(void) {
    size_t sz = YEE_N * sizeof(float);

    YEE_CHECK(cudaMalloc(&d_V,  sz));
    YEE_CHECK(cudaMalloc(&d_Ix, sz));
    YEE_CHECK(cudaMalloc(&d_Iy, sz));
    YEE_CHECK(cudaMalloc(&d_Iz, sz));
    YEE_CHECK(cudaMalloc(&d_L,  sz));
    YEE_CHECK(cudaMalloc(&d_V_accum, sz));

    /* Zero all field arrays */
    YEE_CHECK(cudaMemset(d_V,  0, sz));
    YEE_CHECK(cudaMemset(d_Ix, 0, sz));
    YEE_CHECK(cudaMemset(d_Iy, 0, sz));
    YEE_CHECK(cudaMemset(d_Iz, 0, sz));
    YEE_CHECK(cudaMemset(d_V_accum, 0, sz));

    /* Initialize L to wire (low impedance — fully conductive).
     * Hebbian will RAISE L where there's no activity (creating vacuum).
     * Starting from vacuum deadlocks: no signal → no Hebbian → no wires. */
    float *h_L = (float *)malloc(sz);
    if (!h_L) return -1;
    for (int i = 0; i < YEE_N; i++) h_L[i] = YEE_L_WIRE;
    YEE_CHECK(cudaMemcpy(d_L, h_L, sz, cudaMemcpyHostToDevice));
    free(h_L);

    h_scratch = (float *)malloc(YEE_GRID * sizeof(float));
    /* leaky integrator — no tick counter needed */

    printf("  Yee grid: %dx%dx%d = %d voxels\n", YEE_GX, YEE_GY, YEE_GZ, YEE_N);
    printf("  Memory: %.1f MB (5 arrays × %d × 4B)\n",
           5.0f * sz / (1024 * 1024), YEE_N);
    printf("  CUDA: %d blocks × %d threads\n", YEE_GRID, YEE_BLOCK);
    return 0;
}

extern "C" void yee_destroy(void) {
    if (d_V)       { cudaFree(d_V);       d_V = NULL; }
    if (d_Ix)      { cudaFree(d_Ix);      d_Ix = NULL; }
    if (d_Iy)      { cudaFree(d_Iy);      d_Iy = NULL; }
    if (d_Iz)      { cudaFree(d_Iz);      d_Iz = NULL; }
    if (d_L)       { cudaFree(d_L);       d_L = NULL; }
    if (d_V_accum) { cudaFree(d_V_accum); d_V_accum = NULL; }
    if (h_scratch)  { free(h_scratch);     h_scratch = NULL; }
}

extern "C" int yee_tick(void) {
    /* Step 1: Update V from I divergence */
    kernel_yee_V<<<YEE_GRID, YEE_BLOCK>>>(d_V, d_Ix, d_Iy, d_Iz, YEE_N);
    YEE_CHECK(cudaGetLastError());

    /* Sync: V must be fully updated before I reads new V */
    YEE_CHECK(cudaDeviceSynchronize());

    /* Step 2: Update I from new V gradient */
    kernel_yee_I<<<YEE_GRID, YEE_BLOCK>>>(d_V, d_Ix, d_Iy, d_Iz, d_L, YEE_N);
    YEE_CHECK(cudaGetLastError());
    YEE_CHECK(cudaDeviceSynchronize());

    /* Accumulate |V| for Hebbian */
    kernel_yee_accum<<<YEE_GRID, YEE_BLOCK>>>(d_V_accum, d_V, YEE_N);
    YEE_CHECK(cudaGetLastError());
    /* accum_ticks not needed — leaky integrator is self-regulating */

    return 0;
}

extern "C" int yee_inject(const YeeSource *sources, int n_sources) {
    if (n_sources <= 0) return 0;
    if (n_sources > YEE_MAX_SOURCES) n_sources = YEE_MAX_SOURCES;

    YeeSource *d_src;
    size_t sz = n_sources * sizeof(YeeSource);
    YEE_CHECK(cudaMalloc(&d_src, sz));
    YEE_CHECK(cudaMemcpy(d_src, sources, sz, cudaMemcpyHostToDevice));

    int blocks = (n_sources + YEE_BLOCK - 1) / YEE_BLOCK;
    kernel_yee_inject<<<blocks, YEE_BLOCK>>>(d_V, d_src, n_sources);
    YEE_CHECK(cudaGetLastError());
    YEE_CHECK(cudaDeviceSynchronize());

    cudaFree(d_src);
    return 0;
}

extern "C" int yee_hebbian(float strengthen_rate, float weaken_rate) {
    /* Accumulator is leaky (63/64 decay per tick), self-regulating.
     * Threshold: acc > this means "active path, strengthen."
     * With continuous drive, acc_ss at driven voxel ≈ 0.7 (V≈0.01).
     * Threshold 0.1 catches actively driven paths while ignoring noise. */
    float threshold = 0.1f;

    kernel_yee_hebbian<<<YEE_GRID, YEE_BLOCK>>>(
        d_L, d_V_accum, strengthen_rate, weaken_rate, threshold, YEE_N);
    YEE_CHECK(cudaGetLastError());
    YEE_CHECK(cudaDeviceSynchronize());

    /* No reset — leaky integrator decays naturally */
    return 0;
}

/* ══════════════════════════════════════════════════════════════
 * BRIDGE: Download accumulator as uint8_t (0-255)
 *
 * CPU engine reads this instead of old substrate[].
 * Normalized: acc * YEE_ACC_SCALE, clamped to [0, 255].
 * Sustained drive → ~200. Quiet → 0. Same range as old interface.
 * ══════════════════════════════════════════════════════════════ */

extern "C" int yee_download_acc(uint8_t *h_substrate, int n) {
    if (n > YEE_N) n = YEE_N;

    /* Download raw float accumulator */
    float *h_acc = (float *)malloc(YEE_N * sizeof(float));
    if (!h_acc) return -1;
    YEE_CHECK(cudaMemcpy(h_acc, d_V_accum, YEE_N * sizeof(float),
                          cudaMemcpyDeviceToHost));

    /* Normalize to 0-255 */
    for (int i = 0; i < n; i++) {
        float val = h_acc[i] * YEE_ACC_SCALE;
        if (val < 0) val = 0;
        if (val > 255.0f) val = 255.0f;
        h_substrate[i] = (uint8_t)val;
    }

    free(h_acc);
    return 0;
}

extern "C" int yee_set_L(int voxel_id, float L_val) {
    if (voxel_id < 0 || voxel_id >= YEE_N) return -1;
    if (L_val < YEE_L_MIN) L_val = YEE_L_MIN;
    if (L_val > YEE_L_MAX) L_val = YEE_L_MAX;
    YEE_CHECK(cudaMemcpy(d_L + voxel_id, &L_val, sizeof(float),
                          cudaMemcpyHostToDevice));
    return 0;
}

extern "C" int yee_set_L_region(int x0, int y0, int z0,
                                 int x1, int y1, int z1, float L_val) {
    if (L_val < YEE_L_MIN) L_val = YEE_L_MIN;
    if (L_val > YEE_L_MAX) L_val = YEE_L_MAX;

    /* Upload L values for the region */
    for (int gz = z0; gz < z1 && gz < YEE_GZ; gz++)
        for (int gy = y0; gy < y1 && gy < YEE_GY; gy++)
            for (int gx = x0; gx < x1 && gx < YEE_GX; gx++) {
                int p = yee_idx(gx, gy, gz);
                YEE_CHECK(cudaMemcpy(d_L + p, &L_val, sizeof(float),
                                      cudaMemcpyHostToDevice));
            }
    return 0;
}

extern "C" int yee_download_V(float *h_V, int n) {
    if (n > YEE_N) n = YEE_N;
    YEE_CHECK(cudaMemcpy(h_V, d_V, n * sizeof(float), cudaMemcpyDeviceToHost));
    return 0;
}

extern "C" int yee_download_L(float *h_L, int n) {
    if (n > YEE_N) n = YEE_N;
    YEE_CHECK(cudaMemcpy(h_L, d_L, n * sizeof(float), cudaMemcpyDeviceToHost));
    return 0;
}

extern "C" double yee_energy(void) {
    float *d_block_sums;
    YEE_CHECK(cudaMalloc(&d_block_sums, YEE_GRID * sizeof(float)));

    kernel_yee_energy<<<YEE_GRID, YEE_BLOCK>>>(
        d_V, d_Ix, d_Iy, d_Iz, d_L, d_block_sums, YEE_N);
    YEE_CHECK(cudaGetLastError());

    YEE_CHECK(cudaMemcpy(h_scratch, d_block_sums, YEE_GRID * sizeof(float),
                          cudaMemcpyDeviceToHost));
    cudaFree(d_block_sums);

    double total = 0;
    for (int i = 0; i < YEE_GRID; i++) total += h_scratch[i];
    return total;
}

extern "C" double yee_region_energy(int x0, int y0, int z0,
                                     int x1, int y1, int z1) {
    /* Download V and I for the region and compute on CPU.
     * For now, download full arrays. Optimize later if needed. */
    float *h_V  = (float *)calloc(YEE_N, sizeof(float));
    float *h_Ix = (float *)calloc(YEE_N, sizeof(float));
    float *h_Iy = (float *)calloc(YEE_N, sizeof(float));
    float *h_Iz = (float *)calloc(YEE_N, sizeof(float));
    float *h_L  = (float *)calloc(YEE_N, sizeof(float));

    cudaMemcpy(h_V,  d_V,  YEE_N * sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(h_Ix, d_Ix, YEE_N * sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(h_Iy, d_Iy, YEE_N * sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(h_Iz, d_Iz, YEE_N * sizeof(float), cudaMemcpyDeviceToHost);
    cudaMemcpy(h_L,  d_L,  YEE_N * sizeof(float), cudaMemcpyDeviceToHost);

    double e = 0;
    for (int gz = z0; gz < z1 && gz < YEE_GZ; gz++)
        for (int gy = y0; gy < y1 && gy < YEE_GY; gy++)
            for (int gx = x0; gx < x1 && gx < YEE_GX; gx++) {
                int p = yee_idx(gx, gy, gz);
                e += h_V[p] * h_V[p] * YEE_C;
                e += h_Ix[p] * h_Ix[p] * h_L[p];
                e += h_Iy[p] * h_Iy[p] * h_L[p];
                e += h_Iz[p] * h_Iz[p] * h_L[p];
            }

    free(h_V); free(h_Ix); free(h_Iy); free(h_Iz); free(h_L);
    return e * 0.5;
}

extern "C" int yee_upload_L(const float *h_L, int n) {
    if (n > YEE_N) n = YEE_N;
    YEE_CHECK(cudaMemcpy(d_L, h_L, n * sizeof(float), cudaMemcpyHostToDevice));
    return 0;
}

extern "C" int yee_upload_acc(const float *h_acc, int n) {
    if (n > YEE_N) n = YEE_N;
    YEE_CHECK(cudaMemcpy(d_V_accum, h_acc, n * sizeof(float), cudaMemcpyHostToDevice));
    return 0;
}

extern "C" int yee_download_acc_raw(float *h_acc, int n) {
    if (n > YEE_N) n = YEE_N;
    YEE_CHECK(cudaMemcpy(h_acc, d_V_accum, n * sizeof(float), cudaMemcpyDeviceToHost));
    return 0;
}

extern "C" int yee_is_initialized(void) {
    return (d_V != NULL) ? 1 : 0;
}

extern "C" int yee_clear_fields(void) {
    size_t sz = YEE_N * sizeof(float);
    YEE_CHECK(cudaMemset(d_V,  0, sz));
    YEE_CHECK(cudaMemset(d_Ix, 0, sz));
    YEE_CHECK(cudaMemset(d_Iy, 0, sz));
    YEE_CHECK(cudaMemset(d_Iz, 0, sz));
    YEE_CHECK(cudaMemset(d_V_accum, 0, sz));
    /* leaky integrator — no tick counter needed */
    return 0;
}
