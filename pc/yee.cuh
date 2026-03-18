/*
 * yee.cuh — 3D Yee Grid Substrate Header
 *
 * Wave physics on the GPU substrate. Replaces mark/read/co-presence
 * with coupled V/I propagation through a 3D impedance field.
 *
 * V (scalar) at voxel centers. I (vector, 3 components) on voxel faces.
 * L per voxel = learnable inductance = permeability.
 * C = constant = permittivity. Z = sqrt(L/C) at every voxel.
 *
 * Low L = wire (low impedance, fast propagation, signal passes easy).
 * High L = vacuum (high impedance, signal reflects at boundary).
 * Hebbian: strengthen = lower L. Weaken = raise L.
 *
 * CFL stability: alpha <= sqrt(L*C/3). For alpha=0.5, L >= 0.75.
 *
 * Isaac Oravec & Claude, March 2026
 */
#ifndef YEE_CUH
#define YEE_CUH

#include <stdint.h>
#include <cuda_runtime.h>

/* ══════════════════════════════════════════════════════════════
 * GRID DIMENSIONS
 *
 * Global voxel grid: 64x64x64 = 262,144 voxels.
 * Maps to substrate: 16x16x16 cubes × 4x4x4 voxels/cube.
 * gx = cx*4 + lx, gy = cy*4 + ly, gz = cz*4 + lz.
 * ══════════════════════════════════════════════════════════════ */

#define YEE_GX  64    /* global X voxels: VOL_X * CUBE_DIM */
#define YEE_GY  64    /* global Y voxels */
#define YEE_GZ  64    /* global Z voxels */
#define YEE_N   (YEE_GX * YEE_GY * YEE_GZ)  /* 262144 */

/* Physics constants */
#define YEE_ALPHA   0.5f    /* CFL / smoothing (proven stable in 1D+3D proofs) */
#define YEE_R       0.02f   /* series resistance (loss) */
#define YEE_G       0.01f   /* shunt conductance (loss) */
#define YEE_C       1.0f    /* capacitance (fixed, not learnable) */
#define YEE_L_WIRE  1.0f    /* low L = wire = strengthened */
#define YEE_L_VAC   9.0f    /* high L = vacuum = default */
#define YEE_L_MIN   0.75f   /* CFL floor: alpha <= sqrt(L*C/3) */
#define YEE_L_MAX   16.0f   /* maximum inductance (hard barrier) */

/* CUDA launch config */
#define YEE_BLOCK   256     /* threads per block */
#define YEE_GRID    ((YEE_N + YEE_BLOCK - 1) / YEE_BLOCK)

/* ══════════════════════════════════════════════════════════════
 * INJECTION SOURCE — CPU tells GPU where to inject
 * ══════════════════════════════════════════════════════════════ */

#define YEE_MAX_SOURCES 512

typedef struct {
    int   voxel_id;    /* global voxel index */
    float amplitude;   /* node val / VAL_CEILING */
    float strength;    /* node valence / 255.0 (injection strength) */
} YeeSource;

/* ══════════════════════════════════════════════════════════════
 * INDEXING
 * ══════════════════════════════════════════════════════════════ */

static inline __host__ __device__
int yee_idx(int gx, int gy, int gz) {
    return gx + gy * YEE_GX + gz * YEE_GX * YEE_GY;
}

static inline __host__ __device__
void yee_coords(int idx, int *gx, int *gy, int *gz) {
    *gx = idx % YEE_GX;
    *gy = (idx / YEE_GX) % YEE_GY;
    *gz = idx / (YEE_GX * YEE_GY);
}

/* Convert between global voxel coords and (cube_id, local_pos) */
static inline __host__ __device__
int yee_to_cube(int gx, int gy, int gz) {
    int cx = gx / 4, cy = gy / 4, cz = gz / 4;
    return cx + cy * 16 + cz * 16 * 16;
}

static inline __host__ __device__
int yee_to_local(int gx, int gy, int gz) {
    int lx = gx % 4, ly = gy % 4, lz = gz % 4;
    return lx + ly * 4 + lz * 16;
}

/* ══════════════════════════════════════════════════════════════
 * HOST-SIDE API
 * ══════════════════════════════════════════════════════════════ */

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize Yee grid on GPU. Allocates V, Ix, Iy, Iz, L arrays.
 * Sets all L to L_VAC (empty space). Returns 0 on success. */
int yee_init(void);

/* Destroy GPU resources */
void yee_destroy(void);

/* One propagation tick: kernel_yee_V then kernel_yee_I.
 * Two kernel launches with sync between. */
int yee_tick(void);

/* Inject sources: CPU provides array of (voxel, amplitude, strength).
 * V[voxel] += amplitude * strength. */
int yee_inject(const YeeSource *sources, int n_sources);

/* Hebbian update: lower L where |V| has been consistently high.
 * Runs periodically (not every tick). */
int yee_hebbian(float strengthen_rate, float weaken_rate);
int yee_hebbian_adaptive(float strengthen_rate, float weaken_rate);
int yee_hebbian_ex(float strengthen_rate, float weaken_rate, float threshold);

/* Set L at a specific voxel (for testing / manual wiring) */
int yee_set_L(int voxel_id, float L_val);

/* Set L for a rectangular region (for channel/wall tests) */
int yee_set_L_region(int x0, int y0, int z0, int x1, int y1, int z1, float L_val);

/* Download V array to host (for observation / child retina) */
int yee_download_V(float *h_V, int n);

/* Download L array to host */
int yee_download_L(float *h_L, int n);

/* Bridge: download leaky accumulator as uint8_t (0-255).
 * Replaces old substrate[] interface. Normalized so sustained
 * drive → ~200, quiet → 0. CPU engine reads this. */
int yee_download_acc(uint8_t *h_substrate, int n);

/* Total energy on GPU (reduction) */
double yee_energy(void);

/* Energy in a rectangular region */
double yee_region_energy(int x0, int y0, int z0, int x1, int y1, int z1);

/* Upload L array from host to GPU */
int yee_upload_L(const float *h_L, int n);

/* Upload raw float accumulator from host to GPU */
int yee_upload_acc(const float *h_acc, int n);

/* Download raw float accumulator (lossless, for save/load) */
int yee_download_acc_raw(float *h_acc, int n);

/* Check if Yee grid is initialized */
int yee_is_initialized(void);

/* Reset all fields to zero (V, I). Keep L. */
int yee_clear_fields(void);

#ifdef __cplusplus
}
#endif

#endif /* YEE_CUH */
