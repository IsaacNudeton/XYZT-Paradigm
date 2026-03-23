/*
 * yee_cpu.h — Pure C Yee Grid (no CUDA, no GPU, no OS)
 *
 * The same wave physics as yee.cu, on any CPU.
 * Arrays live in regular memory. Tick is a nested loop.
 * Slow but universal. Runs on x86, ARM, Pico, anything.
 *
 * The grid is a 3-torus. Energy wraps on all faces.
 * The sponge captures boundary absorption (the voice).
 *
 * Isaac Oravec & Claude, March 2026
 */
#ifndef YEE_CPU_H
#define YEE_CPU_H

#include <stdint.h>

/* Grid dimensions — configurable per substrate.
 * 64³ on PC (1MB per array). 16³ on Pico (16KB per array). */
#ifndef YEE_GX
#define YEE_GX 64
#endif
#ifndef YEE_GY
#define YEE_GY 64
#endif
#ifndef YEE_GZ
#define YEE_GZ 64
#endif
#define YEE_N (YEE_GX * YEE_GY * YEE_GZ)

/* Physics constants — same as yee.cuh */
#define YEE_ALPHA  0.5f
#define YEE_C      1.0f
#define YEE_R      0.02f
#define YEE_G      0.01f
#define YEE_L_WIRE 1.0f
#define YEE_L_VAC  9.0f
#define YEE_L_MIN  0.75f
#define YEE_L_MAX  16.0f

/* Injection source */
typedef struct {
    int   voxel_id;
    float amplitude;
    float strength;
} YeeSrc;

/* The grid state — everything in one struct */
typedef struct {
    float V[YEE_N];       /* voltage at voxel centers */
    float Ix[YEE_N];      /* current x-component */
    float Iy[YEE_N];      /* current y-component */
    float Iz[YEE_N];      /* current z-component */
    float L[YEE_N];       /* inductance (learnable impedance) */
    float acc[YEE_N];     /* accumulator (leaky energy integrator) */
    float output[YEE_N];  /* sponge absorption (the voice) */
} YeeGrid;

/* Indexing */
static inline int yee_idx(int x, int y, int z) {
    return x + y * YEE_GX + z * YEE_GX * YEE_GY;
}

/* Lifecycle */
void yee_cpu_init(YeeGrid *g);
void yee_cpu_clear(YeeGrid *g);

/* One tick: V update + I update (periodic boundaries) */
void yee_cpu_tick(YeeGrid *g);

/* Inject sources */
void yee_cpu_inject(YeeGrid *g, const YeeSrc *src, int n);

/* Sponge: damp boundaries, capture voice */
void yee_cpu_sponge(YeeGrid *g, int width, float rate);

/* Hebbian: lower L where accumulator is high */
void yee_cpu_hebbian(YeeGrid *g, float str_rate, float wk_rate);

/* Total energy */
float yee_cpu_energy(const YeeGrid *g);

/* Clear output accumulator */
void yee_cpu_clear_output(YeeGrid *g);

/* Retina: holographic Fourier injection on x=0 face */
void yee_cpu_retina_inject(YeeGrid *g, const uint8_t *data, int len, float amp);

#endif /* YEE_CPU_H */
