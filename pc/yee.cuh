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

/* ── Physics constants ──
 *
 * {2} = distinction: two states exist (wire and wall, L_MIN and L_MAX)
 * {3} = substrate: the L-field gradient between them
 * Observer = alpha: how fast the system samples its own physics
 *
 * DERIVED from alpha (CFL stability):
 *   L_MIN = 3 * alpha² / C = 0.75  (below this → numerically unstable)
 *   Wave speed at L_WIRE: c = alpha / sqrt(L*C) = 0.5 cells/tick
 *   Accumulator half-life: ln(0.5)/ln(63/64) = 44 ticks
 *   EARLY_READ ≈ half-life (40 ticks)
 *   SUBSTRATE_INT ≈ 3.5 × half-life (155 ticks)
 *
 * NOT derived (engineering choices):
 *   L_WIRE = 1.0 (unit impedance — convention)
 *   L_VAC = 9.0 (default vacuum — chosen for dynamic range)
 *   L_MAX = 16.0 (hard ceiling — chosen for practical stability)
 *   R, G = loss terms (tuned empirically)
 *
 * The EXISTENCE of two states ({2}) is the axiom.
 * The SPECIFIC values are consequences of the grid.
 * Different hardware → different values → same computation.
 */
#define YEE_ALPHA   0.5f    /* Observer: CFL timestep. {2,3}'s free parameter. */
#define YEE_C       1.0f    /* capacitance (unit, fixed) */
#define YEE_R       0.02f   /* series resistance (loss, empirical) */
#define YEE_G       0.01f   /* shunt conductance (loss, empirical) */
#define YEE_L_WIRE  1.0f    /* wire state: unit impedance (convention) */
#define YEE_L_VAC   9.0f    /* wall state: high impedance (engineering choice) */
#define YEE_L_MIN   0.75f   /* DERIVED: 3 * alpha² / C (CFL floor) */
#define YEE_L_MAX   16.0f   /* hard ceiling (engineering choice) */

/* Verify L_MIN = 3 * alpha² / C (CFL derivation) */
/* If someone changes alpha, L_MIN must change too. */
#define YEE_L_MIN_CHECK (3.0f * YEE_ALPHA * YEE_ALPHA / YEE_C)

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
int yee_tick_async(void);  /* launch kernels, return immediately */
int yee_sync(void);        /* wait for async tick to complete */

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

/* Download signed accumulator (coherence field).
 * |signed|/energy ratio = coherence: 1.0 = steady, 0.0 = oscillating */
int yee_download_signed(float *h_signed, int n);

/* Download autocorrelation field (Z=4 observer).
 * Negative = anti-correlated = oscillating = standing wave fringe.
 * Sees through the dead zone where amplitude observers go blind. */
int yee_download_autocorr(float *h_autocorr, int n);

/* Sponge layer: damp V/I at boundary voxels (absorbing BC for inference).
 * Absorbed energy accumulates in d_V_output — the engine's voice. */
int yee_apply_sponge(int width, float rate);

/* Download sponge output accumulator (absorbed energy at boundaries).
 * This IS the engine's expression — the inverse of the retina. */
int yee_download_output(float *h_output, int n);

/* Clear output accumulator (between readings) */
int yee_clear_output(void);

/* Check if Yee grid is initialized */
int yee_is_initialized(void);

/* Reset all fields to zero (V, I). Keep L. */
int yee_clear_fields(void);

/* ── Direct pointer access (unified memory) ──
 * After yee_sync(), CPU reads these directly. No download.
 * Returns NULL if yee_init() hasn't been called. */
float *yee_ptr_V(void);
float *yee_ptr_L(void);
float *yee_ptr_accum(void);
float *yee_ptr_signed(void);
float *yee_ptr_output(void);
float *yee_ptr_autocorr(void);

#ifdef __cplusplus
}
#endif

#endif /* YEE_CUH */
