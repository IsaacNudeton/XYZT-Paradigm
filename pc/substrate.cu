/*
 * substrate.cu — 3D GPU Substrate Implementation
 *
 * Thread IS position. 4x4x4 cubes. Shift registers. Threshold tap.
 * Merges xyzt_v6.cu tiling + v3 spatial coords into 3D volume.
 *
 * Isaac Oravec & Claude, February 2026
 */

#include <stdio.h>
#include <string.h>
#include "substrate.cuh"

/* Portable count-trailing-zeros for host code */
static inline int host_ctzll(uint64_t x) {
    if (x == 0) return 64;
    int n = 0;
    if ((x & 0xFFFFFFFF) == 0) { n += 32; x >>= 32; }
    if ((x & 0xFFFF) == 0) { n += 16; x >>= 16; }
    if ((x & 0xFF) == 0) { n += 8; x >>= 8; }
    if ((x & 0xF) == 0) { n += 4; x >>= 4; }
    if ((x & 0x3) == 0) { n += 2; x >>= 2; }
    if ((x & 0x1) == 0) { n += 1; }
    return n;
}

/* ══════════════════════════════════════════════════════════════
 * GPU GLOBAL STATE
 * ══════════════════════════════════════════════════════════════ */

static CubeState     *d_cubes     = NULL;
static GatewayState3D *d_gateways = NULL;
static int           *d_results   = NULL;
static int           *d_neighbors = NULL;  /* neighbor[cube * N_DIRS + dir] = neighbor cube id */
static int            d_allocated = 0;

/* ══════════════════════════════════════════════════════════════
 * KERNEL 1: CUBE TICK (3D)
 *
 * Each thread block = one cube (64 threads).
 * Each thread = one (lx, ly, lz) position.
 * Shared memory holds the cube's mark snapshot.
 * ══════════════════════════════════════════════════════════════ */

__global__ void kernel_cube_tick(CubeState *cubes, int n_cubes) {
    int cube_id = blockIdx.x;
    int pos = threadIdx.x;

    if (cube_id >= n_cubes || pos >= CUBE_SIZE) return;

    CubeState *c = &cubes[cube_id];

    /* Load marks into shared memory (snapshot) */
    __shared__ uint8_t snap[CUBE_SIZE];
    snap[pos] = c->marked[pos];
    __syncthreads();

    if (!c->active[pos]) {
        c->co_present[pos] = 0;
        return;
    }

    /* Build co-presence: check each source in reads mask */
    uint64_t rd = c->reads[pos];
    uint64_t present = 0;

    /* Iterate set bits in reads mask */
    uint64_t bits = rd;
    while (bits) {
        int b = __ffsll(bits) - 1;  /* find first set bit */
        if (b < CUBE_SIZE && snap[b]) {
            present |= (1ULL << b);
        }
        bits &= bits - 1;  /* clear lowest set bit */
    }

    c->co_present[pos] = present;

    /* Update shift register: shift left, OR in "was I active?" */
    uint32_t sr = c->shift_reg[pos];
    sr = (sr << 1) | (present != 0 ? 1 : 0);
    c->shift_reg[pos] = sr;

    /* Substrate update: strengthen if active, proportional decay if not.
     * 63/64 decay (~1.6% per tick). Proven with T3 PASS at this rate.
     * MUST match kernel_cube_tick_observe — audit caught inconsistency. */
    if (present != 0) {
        int nw = (int)c->substrate[pos] + 1;
        c->substrate[pos] = nw > 255 ? 255 : (uint8_t)nw;
    } else {
        int cur = (int)c->substrate[pos];
        int nw = cur - (cur / 64 + (cur > 0 ? 1 : 0));  /* floor(cur * 63/64) */
        c->substrate[pos] = nw < 0 ? 0 : (uint8_t)nw;
    }
}

/* ══════════════════════════════════════════════════════════════
 * KERNEL 2: OBSERVE
 * ══════════════════════════════════════════════════════════════ */

__global__ void kernel_cube_observe(
    CubeState *cubes, int n_cubes,
    int *results, int obs_type, int target_pos
) {
    int cube_id = blockIdx.x;
    int pos = threadIdx.x;

    if (cube_id >= n_cubes || pos >= CUBE_SIZE) return;
    if (target_pos >= 0 && pos != target_pos) return;

    CubeState *c = &cubes[cube_id];
    uint64_t cp = c->co_present[pos];
    uint64_t rd = c->reads[pos];
    int result = 0;

    switch (obs_type) {
        case OBS_ANY:
            result = (cp != 0) ? 1 : 0;
            break;
        case OBS_ALL:
            result = ((cp & rd) == rd) ? 1 : 0;
            break;
        case OBS_PARITY:
            result = __popcll(cp) & 1;
            break;
        case OBS_COUNT:
            result = __popcll(cp);
            break;
        case OBS_GE2:
            result = (__popcll(cp) >= 2) ? 1 : 0;
            break;
    }

    results[cube_id * CUBE_SIZE + pos] = result;
}

/* ══════════════════════════════════════════════════════════════
 * KERNEL 3: FUSED TICK + OBSERVE
 * ══════════════════════════════════════════════════════════════ */

__global__ void kernel_cube_tick_observe(
    CubeState *cubes, int n_cubes,
    int *results, int obs_type, int target_pos
) {
    int cube_id = blockIdx.x;
    int pos = threadIdx.x;

    if (cube_id >= n_cubes || pos >= CUBE_SIZE) return;

    CubeState *c = &cubes[cube_id];

    /* TICK phase */
    __shared__ uint8_t snap[CUBE_SIZE];
    snap[pos] = c->marked[pos];
    __syncthreads();

    uint64_t present = 0;
    if (c->active[pos]) {
        uint64_t bits = c->reads[pos];
        while (bits) {
            int b = __ffsll(bits) - 1;
            if (b < CUBE_SIZE && snap[b])
                present |= (1ULL << b);
            bits &= bits - 1;
        }
    }
    c->co_present[pos] = present;

    /* Shift register update */
    uint32_t sr = c->shift_reg[pos];
    sr = (sr << 1) | (present != 0 ? 1 : 0);
    c->shift_reg[pos] = sr;

    /* Substrate — proportional 63/64 decay. MUST match kernel_cube_tick. */
    if (present != 0) {
        int nw = (int)c->substrate[pos] + 1;
        c->substrate[pos] = nw > 255 ? 255 : (uint8_t)nw;
    } else {
        int cur = (int)c->substrate[pos];
        int nw = cur - (cur / 64 + (cur > 0 ? 1 : 0));
        c->substrate[pos] = nw < 0 ? 0 : (uint8_t)nw;
    }

    __syncthreads();

    /* OBSERVE phase */
    if (target_pos >= 0 && pos != target_pos) return;

    uint64_t cp = present;
    uint64_t rd = c->reads[pos];
    int result = 0;

    switch (obs_type) {
        case OBS_ANY:    result = (cp != 0) ? 1 : 0; break;
        case OBS_ALL:    result = ((cp & rd) == rd) ? 1 : 0; break;
        case OBS_PARITY: result = __popcll(cp) & 1; break;
        case OBS_COUNT:  result = __popcll(cp); break;
        case OBS_GE2:    result = (__popcll(cp) >= 2) ? 1 : 0; break;
    }

    results[cube_id * CUBE_SIZE + pos] = result;
}

/* ══════════════════════════════════════════════════════════════
 * KERNEL 4: 3D ROUTE STEP
 *
 * Propagate gateway signals between neighboring cubes in 3D.
 * Directions: 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
 * ══════════════════════════════════════════════════════════════ */

__global__ void kernel_route_3d(
    GatewayState3D *gateways,
    int *neighbors,     /* neighbors[cube * N_DIRS + dir] = neighbor cube id */
    int n_cubes
) {
    int cube_id = blockIdx.x;
    if (cube_id >= n_cubes) return;
    if (threadIdx.x != 0) return;  /* Only one thread per cube for routing */

    GatewayState3D *gw = &gateways[cube_id];

    uint8_t snap[N_DIRS];
    #pragma unroll
    for (int i = 0; i < N_DIRS; i++) snap[i] = gw->lane[i];

    for (int dir = 0; dir < N_DIRS; dir++) {
        if (!snap[dir]) continue;

        int next = neighbors[cube_id * N_DIRS + dir];
        if (next < 0) continue;

        GatewayState3D *next_gw = &gateways[next];

        /* Try to forward into same directional lane */
        if (!next_gw->lane[dir]) {
            next_gw->lane[dir] = snap[dir];
            gw->lane[dir] = 0;
        } else {
            /* Stall: backpressure */
            gw->stall = 1;
        }
    }
}

/* ══════════════════════════════════════════════════════════════
 * KERNEL 5: THRESHOLD TAP
 *
 * One crossing from substrate (arithmetic) to topology (binary).
 * substrate[pos] >= SUB_ALIVE -> marked[pos] = 1, else 0.
 * Called once per tick to refresh the topology layer.
 * ══════════════════════════════════════════════════════════════ */

__global__ void kernel_threshold_tap(CubeState *cubes, int n_cubes) {
    int cube_id = blockIdx.x;
    int pos = threadIdx.x;

    if (cube_id >= n_cubes || pos >= CUBE_SIZE) return;

    CubeState *c = &cubes[cube_id];
    c->marked[pos] = (c->substrate[pos] >= SUB_ALIVE) ? 1 : 0;
}

/* ══════════════════════════════════════════════════════════════
 * KERNEL 6: AUTO-WIRE FROM PROXIMITY
 *
 * Each position reads from its 6 face-neighbors within the cube.
 * This creates local spatial correlation structure.
 * ══════════════════════════════════════════════════════════════ */

__global__ void kernel_auto_wire_local(CubeState *cubes, int n_cubes) {
    int cube_id = blockIdx.x;
    int pos = threadIdx.x;

    if (cube_id >= n_cubes || pos >= CUBE_SIZE) return;

    CubeState *c = &cubes[cube_id];

    int lx, ly, lz;
    lx = pos % CUBE_DIM;
    ly = (pos / CUBE_DIM) % CUBE_DIM;
    lz = pos / (CUBE_DIM * CUBE_DIM);

    uint64_t rd = 0;
    /* 6-connected neighbors within cube */
    int dx[] = {1, -1, 0, 0, 0, 0};
    int dy[] = {0, 0, 1, -1, 0, 0};
    int dz[] = {0, 0, 0, 0, 1, -1};

    for (int d = 0; d < 6; d++) {
        int nx = lx + dx[d], ny = ly + dy[d], nz = lz + dz[d];
        if (nx >= 0 && nx < CUBE_DIM && ny >= 0 && ny < CUBE_DIM && nz >= 0 && nz < CUBE_DIM) {
            int nidx = nx + ny * CUBE_DIM + nz * CUBE_DIM * CUBE_DIM;
            rd |= (1ULL << nidx);
        }
    }

    c->reads[pos] = rd;
    c->active[pos] = (rd != 0) ? 1 : 0;
}

/* ══════════════════════════════════════════════════════════════
 * KERNEL 7: GATEWAY INJECTION
 *
 * Inject inter-cube gateway signals into substrate (not topology).
 * Keeps threshold tap as the single crossing point.
 * Signal traveling in direction D entered from the opposite face.
 *
 * Call order: route_step → inject_gateways → tick (tap → co-presence)
 * ══════════════════════════════════════════════════════════════ */

__global__ void kernel_inject_gateways(
    CubeState *cubes, GatewayState3D *gateways, int n_cubes
) {
    int cube_id = blockIdx.x;
    int pos = threadIdx.x;
    if (cube_id >= n_cubes || pos >= CUBE_SIZE) return;

    int lx, ly, lz;
    local_coords(pos, &lx, &ly, &lz);
    GatewayState3D *gw = &gateways[cube_id];

    /* Gateway signal → substrate boost at receiving face.
     * +X travel (lane[0]) → entered at lx=0 (from -X neighbor)
     * -X travel (lane[1]) → entered at lx=CUBE_DIM-1 (from +X neighbor)
     * +Y travel (lane[2]) → entered at ly=0
     * -Y travel (lane[3]) → entered at ly=CUBE_DIM-1
     * +Z travel (lane[4]) → entered at lz=0
     * -Z travel (lane[5]) → entered at lz=CUBE_DIM-1 */
    uint8_t boost = 0;
    if      (lx == 0            && gw->lane[0]) boost = gw->lane[0];
    else if (lx == CUBE_DIM - 1 && gw->lane[1]) boost = gw->lane[1];
    else if (ly == 0            && gw->lane[2]) boost = gw->lane[2];
    else if (ly == CUBE_DIM - 1 && gw->lane[3]) boost = gw->lane[3];
    else if (lz == 0            && gw->lane[4]) boost = gw->lane[4];
    else if (lz == CUBE_DIM - 1 && gw->lane[5]) boost = gw->lane[5];

    if (boost > 0) {
        int nw = (int)cubes[cube_id].substrate[pos] + (int)boost;
        cubes[cube_id].substrate[pos] = nw > 255 ? 255 : (uint8_t)nw;
    }
}

/* ══════════════════════════════════════════════════════════════
 * HOST-SIDE IMPLEMENTATION
 * ══════════════════════════════════════════════════════════════ */

static void build_neighbor_table(int *h_neighbors, int n_cubes) {
    /* Direction offsets: +X, -X, +Y, -Y, +Z, -Z */
    int dcx[] = {1, -1, 0, 0, 0, 0};
    int dcy[] = {0, 0, 1, -1, 0, 0};
    int dcz[] = {0, 0, 0, 0, 1, -1};

    for (int c = 0; c < n_cubes; c++) {
        int cx, cy, cz;
        cube_coords(c, &cx, &cy, &cz);
        for (int d = 0; d < N_DIRS; d++) {
            h_neighbors[c * N_DIRS + d] = cube_id_from(cx + dcx[d], cy + dcy[d], cz + dcz[d]);
        }
    }
}

extern "C" int substrate_init(int n_cubes) {
    if (n_cubes > MAX_CUBES) n_cubes = MAX_CUBES;

    cudaError_t err;

    err = cudaMalloc(&d_cubes, n_cubes * sizeof(CubeState));
    if (err != cudaSuccess) return -1;
    err = cudaMemset(d_cubes, 0, n_cubes * sizeof(CubeState));
    if (err != cudaSuccess) return -1;

    err = cudaMalloc(&d_gateways, n_cubes * sizeof(GatewayState3D));
    if (err != cudaSuccess) return -1;
    err = cudaMemset(d_gateways, 0, n_cubes * sizeof(GatewayState3D));
    if (err != cudaSuccess) return -1;

    err = cudaMalloc(&d_results, n_cubes * CUBE_SIZE * sizeof(int));
    if (err != cudaSuccess) return -1;

    /* Build and upload neighbor table */
    int *h_neighbors = (int *)malloc(n_cubes * N_DIRS * sizeof(int));
    build_neighbor_table(h_neighbors, n_cubes);
    err = cudaMalloc(&d_neighbors, n_cubes * N_DIRS * sizeof(int));
    if (err != cudaSuccess) { free(h_neighbors); return -1; }
    err = cudaMemcpy(d_neighbors, h_neighbors, n_cubes * N_DIRS * sizeof(int), cudaMemcpyHostToDevice);
    free(h_neighbors);
    if (err != cudaSuccess) return -1;

    /* Auto-wire all cubes with local 3D proximity */
    kernel_auto_wire_local<<<n_cubes, CUBE_SIZE>>>(d_cubes, n_cubes);
    err = cudaDeviceSynchronize();
    if (err != cudaSuccess) return -1;

    d_allocated = n_cubes;
    return 0;
}

extern "C" void substrate_destroy(void) {
    if (d_cubes) cudaFree(d_cubes);
    if (d_gateways) cudaFree(d_gateways);
    if (d_results) cudaFree(d_results);
    if (d_neighbors) cudaFree(d_neighbors);
    d_cubes = NULL; d_gateways = NULL; d_results = NULL; d_neighbors = NULL;
    d_allocated = 0;
}

extern "C" int substrate_upload(const CubeState *h_cubes, int n_cubes) {
    if (!d_cubes || n_cubes > d_allocated) return -1;
    cudaError_t err = cudaMemcpy(d_cubes, h_cubes, n_cubes * sizeof(CubeState), cudaMemcpyHostToDevice);
    return (err == cudaSuccess) ? 0 : -1;
}

extern "C" int substrate_download(CubeState *h_cubes, int n_cubes) {
    if (!d_cubes || n_cubes > d_allocated) return -1;
    cudaError_t err = cudaMemcpy(h_cubes, d_cubes, n_cubes * sizeof(CubeState), cudaMemcpyDeviceToHost);
    return (err == cudaSuccess) ? 0 : -1;
}

extern "C" int substrate_tick(int n_cubes) {
    if (!d_cubes || n_cubes > d_allocated) return -1;

    /* Threshold tap: substrate -> marks */
    kernel_threshold_tap<<<n_cubes, CUBE_SIZE>>>(d_cubes, n_cubes);

    /* Tick: marks -> co-presence */
    kernel_cube_tick<<<n_cubes, CUBE_SIZE>>>(d_cubes, n_cubes);

    cudaError_t err = cudaDeviceSynchronize();
    return (err == cudaSuccess) ? 0 : -1;
}

extern "C" int substrate_tick_and_observe(int n_cubes, int obs_type, int target_pos,
                                           int *h_results) {
    if (!d_cubes || n_cubes > d_allocated) return -1;

    kernel_threshold_tap<<<n_cubes, CUBE_SIZE>>>(d_cubes, n_cubes);
    kernel_cube_tick_observe<<<n_cubes, CUBE_SIZE>>>(d_cubes, n_cubes, d_results, obs_type, target_pos);

    cudaError_t err = cudaDeviceSynchronize();
    if (err != cudaSuccess) return -1;

    err = cudaMemcpy(h_results, d_results, n_cubes * CUBE_SIZE * sizeof(int), cudaMemcpyDeviceToHost);
    return (err == cudaSuccess) ? 0 : -1;
}

extern "C" int substrate_route_step(int n_cubes) {
    if (!d_gateways || n_cubes > d_allocated) return -1;

    kernel_route_3d<<<n_cubes, 1>>>(d_gateways, d_neighbors, n_cubes);

    cudaError_t err = cudaDeviceSynchronize();
    return (err == cudaSuccess) ? 0 : -1;
}

extern "C" int substrate_inject_gateways(int n_cubes) {
    if (!d_cubes || !d_gateways || n_cubes > d_allocated) return -1;

    kernel_inject_gateways<<<n_cubes, CUBE_SIZE>>>(d_cubes, d_gateways, n_cubes);

    cudaError_t err = cudaDeviceSynchronize();
    return (err == cudaSuccess) ? 0 : -1;
}

extern "C" void substrate_hebbian_update(CubeState *cubes, int n_cubes) {
    /* CPU-side Hebbian: correlate co-presence results between neighbors.
     * Bidirectional: co-present → strengthen, active-but-not-co-present → weaken.
     * Without weakening, all positions saturate to 255 and spatial
     * information is destroyed. */
    for (int c = 0; c < n_cubes; c++) {
        CubeState *cube = &cubes[c];
        for (int p = 0; p < CUBE_SIZE; p++) {
            if (!cube->active[p]) continue;
            uint64_t cp = cube->co_present[p];
            uint64_t rd = cube->reads[p];

            if (cp != 0) {
                /* Strengthen: co-present neighbors → boost substrate.
                 * +1 (not +2). Matches GPU kernel rate. CC proved at this level. */
                uint64_t bits = cp;
                while (bits) {
                    int b = host_ctzll(bits);
                    int nw = (int)cube->substrate[b] + 1;
                    cube->substrate[b] = nw > 255 ? 255 : (uint8_t)nw;
                    bits &= bits - 1;
                }
            }

            /* Weaken: wired neighbors that are NOT co-present → decay substrate.
             * This is the missing half of Hebb's rule. Without it, everything
             * saturates regardless of activity pattern. */
            uint64_t absent = rd & ~cp;
            uint64_t bits2 = absent;
            while (bits2) {
                int b = host_ctzll(bits2);
                int nw = (int)cube->substrate[b] - 1;
                cube->substrate[b] = nw < 0 ? 0 : (uint8_t)nw;
                bits2 &= bits2 - 1;
            }
        }
    }
}

extern "C" void substrate_device_info(char *buf, int buflen) {
    cudaDeviceProp prop;
    cudaGetDeviceProperties(&prop, 0);

    snprintf(buf, buflen,
        "GPU: %s\n"
        "  CUDA cores: %d SMs x %d = ~%d cores\n"
        "  Memory: %.1f GB\n"
        "  Compute: sm_%d%d\n"
        "  XYZT mapping: %d positions/cube, %d cubes max = %d voxels\n",
        prop.name,
        prop.multiProcessorCount,
        (prop.major >= 7 ? 64 : 128),
        prop.multiProcessorCount * (prop.major >= 7 ? 64 : 128),
        prop.totalGlobalMem / 1e9,
        prop.major, prop.minor,
        CUBE_SIZE, MAX_CUBES, CUBE_SIZE * MAX_CUBES);
}
