/*
 * substrate.cuh — 3D GPU Substrate Header
 *
 * Thread IS position. One CUDA thread per (x,y,z) voxel.
 * 4x4x4 = 64 positions per tile (cube). Shift registers for temporal history.
 * Threshold bit tap: one crossing from substrate to topology.
 *
 * Merges: xyzt_v6.cu (tile architecture) + v3 spatial coords + 3D volume.
 *
 * Target: RTX 2080 Super (sm_75, 3072 cores, 8GB VRAM)
 *
 * Isaac Oravec & Claude, February 2026
 */

#ifndef SUBSTRATE_CUH
#define SUBSTRATE_CUH

#include <stdint.h>
#include <cuda_runtime.h>

/* ══════════════════════════════════════════════════════════════
 * CONFIGURATION
 * ══════════════════════════════════════════════════════════════ */

#define CUBE_DIM        4       /* 4x4x4 = 64 positions per cube/tile */
#define CUBE_SIZE       64      /* CUBE_DIM^3 */
#define MAX_CUBES       4096    /* Up to 4096 cubes = 262144 voxels */
#define THREADS_PER_CUBE 64     /* One thread per position in cube */

/* Volume dimensions (in cubes) */
#define VOL_X           16
#define VOL_Y           16
#define VOL_Z           16      /* 16x16x16 = 4096 cubes, 262144 voxels */

/* Connectivity: 6-face (von Neumann) neighbors in 3D */
#define N_DIRS          6       /* +X, -X, +Y, -Y, +Z, -Z */

/* Substrate threshold: above this = ALIVE (topology), below = dead */
#define SUB_ALIVE       64

/* Shift register depth for temporal history */
#define SHIFT_DEPTH     32

/* ══════════════════════════════════════════════════════════════
 * CUBE STATE — one tile in the 3D volume
 * ══════════════════════════════════════════════════════════════ */

typedef struct {
    uint8_t  marked[CUBE_SIZE];         /* Mark state per position */
    uint64_t reads[CUBE_SIZE];          /* Topology: 64-bit bitmask of read sources */
    uint64_t co_present[CUBE_SIZE];     /* Result after tick */
    uint8_t  active[CUBE_SIZE];         /* Which positions are wired */
    uint8_t  substrate[CUBE_SIZE];      /* Substrate weight (0-255, Hebbian) */
    uint32_t shift_reg[CUBE_SIZE];      /* Temporal history: 1 bit per tick */
    int      cube_id;                   /* Global cube index */
} CubeState;

/* Gateway state for inter-cube routing in 3D */
typedef struct {
    uint8_t  lane[N_DIRS];              /* One lane per direction */
    uint8_t  stall;                     /* Backpressure flag */
    uint8_t  held_value;                /* Buffered value */
    int      held_dst;                  /* Destination cube for held signal */
} GatewayState3D;

/* Observer types */
#define OBS_ANY    0
#define OBS_ALL    1
#define OBS_PARITY 2
#define OBS_COUNT  3
#define OBS_GE2    4

/* ══════════════════════════════════════════════════════════════
 * 3D INDEXING — position within cube ↔ (lx, ly, lz)
 * ══════════════════════════════════════════════════════════════ */

/* Local position within cube: (lx, ly, lz) -> flat index */
static inline __host__ __device__
int local_idx(int lx, int ly, int lz) {
    return lx + ly * CUBE_DIM + lz * CUBE_DIM * CUBE_DIM;
}

/* Flat index -> local coords */
static inline __host__ __device__
void local_coords(int idx, int *lx, int *ly, int *lz) {
    *lx = idx % CUBE_DIM;
    *ly = (idx / CUBE_DIM) % CUBE_DIM;
    *lz = idx / (CUBE_DIM * CUBE_DIM);
}

/* Cube ID -> volume coords */
static inline __host__ __device__
void cube_coords(int cube_id, int *cx, int *cy, int *cz) {
    *cx = cube_id % VOL_X;
    *cy = (cube_id / VOL_X) % VOL_Y;
    *cz = cube_id / (VOL_X * VOL_Y);
}

/* Volume coords -> cube ID */
static inline __host__ __device__
int cube_id_from(int cx, int cy, int cz) {
    if (cx < 0 || cx >= VOL_X || cy < 0 || cy >= VOL_Y || cz < 0 || cz >= VOL_Z)
        return -1;
    return cx + cy * VOL_X + cz * VOL_X * VOL_Y;
}

/* Global voxel address: (cube_id, local_pos) */
static inline __host__ __device__
int global_addr(int cube_id, int local_pos) {
    return cube_id * CUBE_SIZE + local_pos;
}

/* ══════════════════════════════════════════════════════════════
 * HOST-SIDE API
 * ══════════════════════════════════════════════════════════════ */

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize substrate on GPU. Returns 0 on success. */
int substrate_init(int n_cubes);

/* Destroy GPU resources */
void substrate_destroy(void);

/* Upload cube states from host to GPU */
int substrate_upload(const CubeState *h_cubes, int n_cubes);

/* Download cube states from GPU to host */
int substrate_download(CubeState *h_cubes, int n_cubes);

/* Run one tick on GPU: snapshot marks -> compute co-presence */
int substrate_tick(int n_cubes);

/* Run tick + observe in one fused kernel */
int substrate_tick_and_observe(int n_cubes, int obs_type, int target_pos,
                               int *h_results);

/* Run route step: propagate signals between cubes */
int substrate_route_step(int n_cubes);

/* Inject gateway signals into substrate at receiving faces.
 * Call after route_step, before tick. */
int substrate_inject_gateways(int n_cubes);

/* Seed gateway lanes from CPU engine nodes at cube boundaries.
 * Call before substrate_tick, after wire_engine_to_gpu. */
int substrate_seed_gateways(const Engine *eng, int n_cubes);

/* Seed a single gateway lane directly (for testing).
 * direction: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z */
int substrate_seed_gateway_lane(int cube_id, int direction, uint8_t value);

/* Update substrate weights (Hebbian, called from CPU) */
void substrate_hebbian_update(CubeState *cubes, int n_cubes);

/* Get device info string */
void substrate_device_info(char *buf, int buflen);

#ifdef __cplusplus
}
#endif

/* CUDA error check macro */
#define CUDA_CHECK(call) { \
    cudaError_t err = call; \
    if (err != cudaSuccess) { \
        fprintf(stderr, "CUDA error at %s:%d: %s\n", \
                __FILE__, __LINE__, cudaGetErrorString(err)); \
        return -1; \
    } \
}

#endif /* SUBSTRATE_CUH */
