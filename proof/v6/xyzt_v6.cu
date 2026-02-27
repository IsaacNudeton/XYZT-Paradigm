/*
 * xyzt_v6.cu — CUDA Co-Presence Engine for RTX 2080 Super
 *
 * Maps XYZT v6 tiled architecture directly to GPU:
 *   Tile          → Thread Block
 *   Position      → Thread (or shared memory slot)
 *   tick()        → Kernel launch
 *   Mesh routing  → Global memory inter-block communication
 *   Observer      → __popc() intrinsic
 *
 * RTX 2080 Super: 3072 CUDA cores, 8GB GDDR6, 496 GB/s
 * Each tile is 16 bytes of mark state → millions of tile-ticks/sec
 *
 * Compile: nvcc -O2 -arch=sm_75 -o xyzt_v6_gpu xyzt_v6.cu
 * Run:     ./xyzt_v6_gpu
 *
 * Isaac Oravec & Claude, February 2026
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <cuda_runtime.h>

/* ══════════════════════════════════════════════════════════════
 * CONFIGURATION
 * ══════════════════════════════════════════════════════════════ */

#define TILE_SIZE       16      /* Positions per tile */
#define THREADS_PER_TILE 16     /* One thread per position */
#define MAX_TILES       4096    /* Up to 4096 tiles = 65536 positions */

/* Tile state in global memory (coalesced access pattern) */
typedef struct {
    uint8_t  marked[TILE_SIZE];     /* Mark state */
    uint16_t reads[TILE_SIZE];      /* Topology: bitmask of read sources */
    uint16_t co_present[TILE_SIZE]; /* Result after tick */
    uint8_t  active[TILE_SIZE];     /* Which positions are wired */
} TileState;

/* Gateway state for inter-tile routing */
typedef struct {
    uint8_t  lane[6];           /* 6 directional lanes */
    uint8_t  stall;             /* Backpressure flag */
    int      held_dst;          /* Destination for held signal */
    uint8_t  held_value;        /* Buffered value */
} GatewayState;

/* ══════════════════════════════════════════════════════════════
 * KERNEL 1: TILE TICK
 *
 * Each thread block = one tile (16 threads).
 * Each thread handles one position.
 * Shared memory holds the tile's mark snapshot.
 *
 * This IS tick(). Zero arithmetic. Just routing.
 * ══════════════════════════════════════════════════════════════ */

__global__ void kernel_tick(TileState *tiles, int n_tiles) {
    int tile_id = blockIdx.x;
    int pos = threadIdx.x;

    if (tile_id >= n_tiles || pos >= TILE_SIZE) return;

    TileState *t = &tiles[tile_id];

    /* Load marks into shared memory (snapshot) */
    __shared__ uint8_t snap[TILE_SIZE];
    snap[pos] = t->marked[pos];
    __syncthreads();

    /* Only active positions compute co-presence */
    if (!t->active[pos]) {
        t->co_present[pos] = 0;
        return;
    }

    /* Read topology, build co-presence set */
    uint16_t rd = t->reads[pos];
    uint16_t present = 0;

    /* Check each bit in reads mask */
    #pragma unroll
    for (int b = 0; b < TILE_SIZE; b++) {
        if ((rd >> b) & 1) {
            if (snap[b]) {
                present |= (1 << b);
            }
        }
    }

    /* Write co-presence result */
    t->co_present[pos] = present;
}

/* ══════════════════════════════════════════════════════════════
 * KERNEL 2: OBSERVER
 *
 * Apply observer function to co-presence results.
 * Each thread = one position in one tile.
 * Uses __popc() intrinsic for single-instruction counting.
 * ══════════════════════════════════════════════════════════════ */

#define OBS_ANY    0
#define OBS_ALL    1
#define OBS_PARITY 2
#define OBS_COUNT  3
#define OBS_GE2    4

__global__ void kernel_observe(
    TileState *tiles, int n_tiles,
    int *results,       /* Output: one result per tile position */
    int obs_type,       /* Which observer to apply */
    int target_pos      /* Which position to observe (-1 for all) */
) {
    int tile_id = blockIdx.x;
    int pos = threadIdx.x;

    if (tile_id >= n_tiles || pos >= TILE_SIZE) return;
    if (target_pos >= 0 && pos != target_pos) return;

    TileState *t = &tiles[tile_id];
    uint16_t cp = t->co_present[pos];
    uint16_t rd = t->reads[pos];
    int result = 0;

    switch (obs_type) {
        case OBS_ANY:
            result = (cp != 0) ? 1 : 0;
            break;
        case OBS_ALL:
            result = ((cp & rd) == rd) ? 1 : 0;
            break;
        case OBS_PARITY:
            result = __popc((unsigned int)cp) & 1;  /* Single instruction! */
            break;
        case OBS_COUNT:
            result = __popc((unsigned int)cp);       /* Single instruction! */
            break;
        case OBS_GE2:
            result = (__popc((unsigned int)cp) >= 2) ? 1 : 0;
            break;
    }

    results[tile_id * TILE_SIZE + pos] = result;
}

/* ══════════════════════════════════════════════════════════════
 * KERNEL 3: MESH ROUTING
 *
 * Propagate signals between tiles through gateway lanes.
 * Each thread block = one tile's gateway logic.
 * Global memory for inter-tile communication.
 * ══════════════════════════════════════════════════════════════ */

__global__ void kernel_route_step(
    GatewayState *gateways,
    int *neighbor,      /* neighbor[tile*4 + dir] = neighbor tile id */
    int n_tiles,
    int mesh_width
) {
    int tile_id = blockIdx.x;
    if (tile_id >= n_tiles) return;

    /* Only thread 0 per block handles routing logic */
    if (threadIdx.x != 0) return;

    GatewayState *gw = &gateways[tile_id];

    /* Snapshot current lanes */
    uint8_t snap[6];
    #pragma unroll
    for (int i = 0; i < 6; i++) snap[i] = gw->lane[i];

    /* For each occupied lane, forward to neighbor */
    /* Lanes 0-3: E/W/N/S primary, 4-5: secondary */
    for (int dir = 0; dir < 4; dir++) {
        if (!snap[dir]) continue;

        int next = neighbor[tile_id * 4 + dir];
        if (next < 0) continue;

        GatewayState *next_gw = &gateways[next];

        /* Try primary lane first, then secondary */
        if (!next_gw->lane[dir]) {
            next_gw->lane[dir] = snap[dir];
            gw->lane[dir] = 0;  /* Clear source */
        } else if (dir < 4 && !next_gw->lane[4]) {
            /* Secondary lane (use lane 4 as overflow) */
            next_gw->lane[4] = snap[dir];
            gw->lane[dir] = 0;
        } else {
            /* Stall — backpressure */
            gw->stall = 1;
        }
    }
}

/* ══════════════════════════════════════════════════════════════
 * KERNEL 4: BULK TICK + OBSERVE (FUSED)
 *
 * For benchmarking: tick + observe in one kernel.
 * Reduces kernel launch overhead.
 * ══════════════════════════════════════════════════════════════ */

__global__ void kernel_tick_and_observe(
    TileState *tiles, int n_tiles,
    int *results, int obs_type, int target_pos
) {
    int tile_id = blockIdx.x;
    int pos = threadIdx.x;

    if (tile_id >= n_tiles || pos >= TILE_SIZE) return;

    TileState *t = &tiles[tile_id];

    /* ── TICK phase ── */
    __shared__ uint8_t snap[TILE_SIZE];
    snap[pos] = t->marked[pos];
    __syncthreads();

    uint16_t present = 0;
    if (t->active[pos]) {
        uint16_t rd = t->reads[pos];
        #pragma unroll
        for (int b = 0; b < TILE_SIZE; b++) {
            if ((rd >> b) & 1) {
                if (snap[b]) present |= (1 << b);
            }
        }
    }
    t->co_present[pos] = present;
    __syncthreads();

    /* ── OBSERVE phase ── */
    if (target_pos >= 0 && pos != target_pos) return;

    uint16_t cp = present;
    uint16_t rd = t->reads[pos];
    int result = 0;

    switch (obs_type) {
        case OBS_ANY:    result = (cp != 0) ? 1 : 0; break;
        case OBS_ALL:    result = ((cp & rd) == rd) ? 1 : 0; break;
        case OBS_PARITY: result = __popc((unsigned int)cp) & 1; break;
        case OBS_COUNT:  result = __popc((unsigned int)cp); break;
        case OBS_GE2:    result = (__popc((unsigned int)cp) >= 2) ? 1 : 0; break;
    }

    results[tile_id * TILE_SIZE + pos] = result;
}

/* ══════════════════════════════════════════════════════════════
 * HOST-SIDE HELPERS
 * ══════════════════════════════════════════════════════════════ */

#define CUDA_CHECK(call) { \
    cudaError_t err = call; \
    if (err != cudaSuccess) { \
        fprintf(stderr, "CUDA error at %s:%d: %s\n", \
                __FILE__, __LINE__, cudaGetErrorString(err)); \
        exit(1); \
    } \
}

/* ══════════════════════════════════════════════════════════════
 * TEST FRAMEWORK
 * ══════════════════════════════════════════════════════════════ */

static int g_pass = 0, g_fail = 0;
static void check(const char *name, int exp, int got) {
    if (exp == got) g_pass++;
    else { g_fail++; printf("  FAIL %s: expected %d, got %d\n", name, exp, got); }
}

/* ══════════════════════════════════════════════════════════════
 * TEST: Boolean operations on GPU
 * ══════════════════════════════════════════════════════════════ */

void test_gpu_boolean(void) {
    printf("═══════════════════════════════════════════════════\n");
    printf("  GPU Boolean Operations\n");
    printf("═══════════════════════════════════════════════════\n\n");

    /* Allocate 4 tiles (one for each a,b combination) */
    int n_tiles = 4;
    TileState *d_tiles;
    int *d_results;
    TileState h_tiles[4];
    int h_results[4 * TILE_SIZE];

    memset(h_tiles, 0, sizeof(h_tiles));

    /* Set up 4 tiles: (0,0), (0,1), (1,0), (1,1) */
    int idx = 0;
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++) {
            h_tiles[idx].marked[0] = a;
            h_tiles[idx].marked[1] = b;
            h_tiles[idx].reads[2] = (1 << 0) | (1 << 1);  /* pos 2 reads pos 0,1 */
            h_tiles[idx].active[2] = 1;
            idx++;
        }

    CUDA_CHECK(cudaMalloc(&d_tiles, n_tiles * sizeof(TileState)));
    CUDA_CHECK(cudaMalloc(&d_results, n_tiles * TILE_SIZE * sizeof(int)));
    CUDA_CHECK(cudaMemcpy(d_tiles, h_tiles, n_tiles * sizeof(TileState), cudaMemcpyHostToDevice));

    /* Test OR */
    kernel_tick_and_observe<<<n_tiles, TILE_SIZE>>>(d_tiles, n_tiles, d_results, OBS_ANY, 2);
    CUDA_CHECK(cudaDeviceSynchronize());
    CUDA_CHECK(cudaMemcpy(h_results, d_results, n_tiles * TILE_SIZE * sizeof(int), cudaMemcpyDeviceToHost));

    idx = 0;
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++) {
            char n[32]; snprintf(n, 32, "GPU OR(%d,%d)", a, b);
            check(n, a | b, h_results[idx * TILE_SIZE + 2]);
            idx++;
        }

    /* Test AND — need to re-upload and re-tick since co_present was overwritten */
    CUDA_CHECK(cudaMemcpy(d_tiles, h_tiles, n_tiles * sizeof(TileState), cudaMemcpyHostToDevice));
    kernel_tick_and_observe<<<n_tiles, TILE_SIZE>>>(d_tiles, n_tiles, d_results, OBS_ALL, 2);
    CUDA_CHECK(cudaDeviceSynchronize());
    CUDA_CHECK(cudaMemcpy(h_results, d_results, n_tiles * TILE_SIZE * sizeof(int), cudaMemcpyDeviceToHost));

    idx = 0;
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++) {
            char n[32]; snprintf(n, 32, "GPU AND(%d,%d)", a, b);
            check(n, a & b, h_results[idx * TILE_SIZE + 2]);
            idx++;
        }

    /* Test XOR */
    CUDA_CHECK(cudaMemcpy(d_tiles, h_tiles, n_tiles * sizeof(TileState), cudaMemcpyHostToDevice));
    kernel_tick_and_observe<<<n_tiles, TILE_SIZE>>>(d_tiles, n_tiles, d_results, OBS_PARITY, 2);
    CUDA_CHECK(cudaDeviceSynchronize());
    CUDA_CHECK(cudaMemcpy(h_results, d_results, n_tiles * TILE_SIZE * sizeof(int), cudaMemcpyDeviceToHost));

    idx = 0;
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++) {
            char n[32]; snprintf(n, 32, "GPU XOR(%d,%d)", a, b);
            check(n, a ^ b, h_results[idx * TILE_SIZE + 2]);
            idx++;
        }

    cudaFree(d_tiles);
    cudaFree(d_results);
    printf("\n");
}

/* ══════════════════════════════════════════════════════════════
 * TEST: Full adder on GPU
 * ══════════════════════════════════════════════════════════════ */

void test_gpu_full_adder(void) {
    printf("═══════════════════════════════════════════════════\n");
    printf("  GPU Full Adder\n");
    printf("═══════════════════════════════════════════════════\n\n");

    int n_tiles = 8;  /* 2^3 combinations */
    TileState *d_tiles;
    int *d_results;
    TileState h_tiles[8];
    int h_results[8 * TILE_SIZE];

    memset(h_tiles, 0, sizeof(h_tiles));

    int idx = 0;
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++)
            for (int cin = 0; cin <= 1; cin++) {
                h_tiles[idx].marked[0] = a;
                h_tiles[idx].marked[1] = b;
                h_tiles[idx].marked[2] = cin;
                h_tiles[idx].reads[3] = (1<<0)|(1<<1)|(1<<2);
                h_tiles[idx].active[3] = 1;
                idx++;
            }

    CUDA_CHECK(cudaMalloc(&d_tiles, n_tiles * sizeof(TileState)));
    CUDA_CHECK(cudaMalloc(&d_results, n_tiles * TILE_SIZE * sizeof(int)));

    /* Sum = parity */
    CUDA_CHECK(cudaMemcpy(d_tiles, h_tiles, n_tiles * sizeof(TileState), cudaMemcpyHostToDevice));
    kernel_tick_and_observe<<<n_tiles, TILE_SIZE>>>(d_tiles, n_tiles, d_results, OBS_PARITY, 3);
    CUDA_CHECK(cudaDeviceSynchronize());
    CUDA_CHECK(cudaMemcpy(h_results, d_results, n_tiles * TILE_SIZE * sizeof(int), cudaMemcpyDeviceToHost));

    int sum_results[8];
    for (int i = 0; i < 8; i++) sum_results[i] = h_results[i * TILE_SIZE + 3];

    /* Carry = count >= 2 */
    CUDA_CHECK(cudaMemcpy(d_tiles, h_tiles, n_tiles * sizeof(TileState), cudaMemcpyHostToDevice));
    kernel_tick_and_observe<<<n_tiles, TILE_SIZE>>>(d_tiles, n_tiles, d_results, OBS_GE2, 3);
    CUDA_CHECK(cudaDeviceSynchronize());
    CUDA_CHECK(cudaMemcpy(h_results, d_results, n_tiles * TILE_SIZE * sizeof(int), cudaMemcpyDeviceToHost));

    idx = 0;
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++)
            for (int cin = 0; cin <= 1; cin++) {
                int total = a + b + cin;
                char n[32];
                snprintf(n, 32, "GPU FA sum(%d,%d,%d)", a, b, cin);
                check(n, total & 1, sum_results[idx]);
                snprintf(n, 32, "GPU FA carry(%d,%d,%d)", a, b, cin);
                check(n, total >> 1, h_results[idx * TILE_SIZE + 3]);
                idx++;
            }

    cudaFree(d_tiles);
    cudaFree(d_results);
    printf("\n");
}

/* ══════════════════════════════════════════════════════════════
 * BENCHMARK: Massive parallel tick
 * ══════════════════════════════════════════════════════════════ */

void benchmark_parallel_tick(void) {
    printf("═══════════════════════════════════════════════════\n");
    printf("  Benchmark: Massive Parallel Tick\n");
    printf("═══════════════════════════════════════════════════\n\n");

    int tile_counts[] = {64, 256, 1024, 4096};

    for (int tc = 0; tc < 4; tc++) {
        int n_tiles = tile_counts[tc];
        int total_positions = n_tiles * TILE_SIZE;

        /* Allocate and initialize */
        TileState *h_tiles = (TileState *)calloc(n_tiles, sizeof(TileState));
        int *h_results = (int *)calloc(n_tiles * TILE_SIZE, sizeof(int));

        /* Set up each tile with some wiring */
        for (int t = 0; t < n_tiles; t++) {
            h_tiles[t].marked[0] = 1;
            h_tiles[t].marked[1] = (t % 2);
            h_tiles[t].reads[2] = (1 << 0) | (1 << 1);
            h_tiles[t].active[2] = 1;
        }

        TileState *d_tiles;
        int *d_results;
        CUDA_CHECK(cudaMalloc(&d_tiles, n_tiles * sizeof(TileState)));
        CUDA_CHECK(cudaMalloc(&d_results, n_tiles * TILE_SIZE * sizeof(int)));
        CUDA_CHECK(cudaMemcpy(d_tiles, h_tiles, n_tiles * sizeof(TileState), cudaMemcpyHostToDevice));

        /* Warm up */
        kernel_tick_and_observe<<<n_tiles, TILE_SIZE>>>(d_tiles, n_tiles, d_results, OBS_ANY, 2);
        CUDA_CHECK(cudaDeviceSynchronize());

        /* Benchmark: 1000 tick-observe cycles */
        cudaEvent_t start, stop;
        cudaEventCreate(&start);
        cudaEventCreate(&stop);

        int iterations = 1000;
        cudaEventRecord(start);
        for (int i = 0; i < iterations; i++) {
            kernel_tick_and_observe<<<n_tiles, TILE_SIZE>>>(d_tiles, n_tiles, d_results, OBS_PARITY, 2);
        }
        cudaEventRecord(stop);
        CUDA_CHECK(cudaDeviceSynchronize());

        float ms = 0;
        cudaEventElapsedTime(&ms, start, stop);

        double ticks_per_sec = (double)n_tiles * iterations / (ms / 1000.0);
        double positions_per_sec = ticks_per_sec * TILE_SIZE;

        printf("  %4d tiles (%5d pos): %8.1f ms for %dk ticks = %.1f M tile-ticks/s = %.1f M pos-ticks/s\n",
               n_tiles, total_positions, ms, iterations,
               ticks_per_sec / 1e6, positions_per_sec / 1e6);

        /* Verify correctness */
        CUDA_CHECK(cudaMemcpy(h_results, d_results, n_tiles * TILE_SIZE * sizeof(int), cudaMemcpyDeviceToHost));

        int correct = 0;
        for (int t = 0; t < n_tiles; t++) {
            int a = 1, b = (t % 2);
            int expected_xor = a ^ b;
            if (h_results[t * TILE_SIZE + 2] == expected_xor) correct++;
        }

        char n[64]; snprintf(n, 64, "bench %d tiles correct", n_tiles);
        check(n, n_tiles, correct);

        cudaEventDestroy(start);
        cudaEventDestroy(stop);
        cudaFree(d_tiles);
        cudaFree(d_results);
        free(h_tiles);
        free(h_results);
    }
    printf("\n");
}

/* ══════════════════════════════════════════════════════════════
 * DEVICE INFO
 * ══════════════════════════════════════════════════════════════ */

void print_device_info(void) {
    cudaDeviceProp prop;
    CUDA_CHECK(cudaGetDeviceProperties(&prop, 0));

    printf("═══════════════════════════════════════════════════\n");
    printf("  GPU: %s\n", prop.name);
    printf("  CUDA cores: %d SMs × %d = ~%d cores\n",
           prop.multiProcessorCount,
           (prop.major >= 7 ? 64 : 128),
           prop.multiProcessorCount * (prop.major >= 7 ? 64 : 128));
    printf("  Memory: %.1f GB\n", prop.totalGlobalMem / 1e9);
    printf("  Shared memory per block: %zu bytes\n", prop.sharedMemPerBlock);
    printf("  Max threads per block: %d\n", prop.maxThreadsPerBlock);
    printf("  Compute capability: %d.%d\n", prop.major, prop.minor);
    printf("═══════════════════════════════════════════════════\n\n");

    printf("  XYZT mapping:\n");
    printf("    Tile size: %d positions = %d threads per block\n", TILE_SIZE, TILE_SIZE);
    printf("    Shared memory per tile: %lu bytes\n", sizeof(uint8_t) * TILE_SIZE);
    printf("    Max tiles per launch: %d\n", prop.maxGridSize[0]);
    printf("    Tiles that fit in memory: ~%d\n",
           (int)(prop.totalGlobalMem / sizeof(TileState)));
    printf("\n");
}

/* ══════════════════════════════════════════════════════════════ */

int main(void) {
    printf("==========================================================\n");
    printf("  XYZT v6 CUDA — Co-Presence on GPU\n");
    printf("  Tile = Thread Block. tick() = Kernel. Observer = __popc.\n");
    printf("==========================================================\n\n");

    print_device_info();
    test_gpu_boolean();
    test_gpu_full_adder();
    benchmark_parallel_tick();

    printf("==========================================================\n");
    printf("  RESULTS: %d passed, %d failed, %d total\n",
           g_pass, g_fail, g_pass + g_fail);
    printf("==========================================================\n");

    if (g_fail == 0) {
        printf("\n  ALL PASS.\n\n");
        printf("  GPU mapping:\n");
        printf("    Tile         → Thread Block (16 threads)\n");
        printf("    tick()       → Kernel launch (all tiles parallel)\n");
        printf("    obs_count    → __popc() (single SM instruction)\n");
        printf("    obs_parity   → __popc() & 1 (single instruction)\n");
        printf("    Shared mem   → Tile-local snapshot (16 bytes)\n");
        printf("    Global mem   → Inter-tile gateway routing\n\n");
        printf("  Same engine. Same observers. Same co-presence.\n");
        printf("  3072 CUDA cores ticking tiles in parallel.\n");
        printf("  Position IS value. On the GPU too.\n\n");
    } else {
        printf("\n  FAILURES DETECTED.\n\n");
    }

    return g_fail;
}
