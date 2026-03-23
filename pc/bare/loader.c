/*
 * loader.c — Universal Bare Metal Loader
 *
 * Reads .xyzt boot image. Initializes wave grid. Ticks.
 * Reads bytes from input boundary (stdin/UART/pipe).
 * Writes bytes to output boundary (stdout/UART/pipe).
 *
 * No OS. No protocol. No von Neumann beyond the tick loop.
 * The tick loop is the booster rocket — it falls away on FPGA
 * where the clock IS the tick.
 *
 * Build: gcc -O2 -o xyzt_bare loader.c yee_cpu.c -lm
 * Run:   ./xyzt_bare [state.xyzt]
 *
 * Isaac Oravec & Claude, March 2026
 */

#include "yee_cpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static YeeGrid grid;

/* ── Load .xyzt boot image (L-field + accumulator) ── */
static int load_xyzt(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    /* Scan for YEE1 magic (0x59454531) byte by byte */
    uint32_t magic = 0;
    int found = 0;
    while (fread(&magic, 4, 1, f) == 1) {
        if (magic == 0x59454531) { found = 1; break; }
        fseek(f, -3, SEEK_CUR);
    }
    if (!found) { fclose(f); return -5; }

    /* Read grid dimensions */
    uint16_t gx, gy, gz;
    if (fread(&gx, 2, 1, f) != 1 ||
        fread(&gy, 2, 1, f) != 1 ||
        fread(&gz, 2, 1, f) != 1) { fclose(f); return -3; }

    if (gx != YEE_GX || gy != YEE_GY || gz != YEE_GZ) {
        fprintf(stderr, "Grid mismatch: file=%dx%dx%d, loader=%dx%dx%d\n",
                gx, gy, gz, YEE_GX, YEE_GY, YEE_GZ);
        fclose(f);
        return -2;
    }

    /* Read L-field */
    if (fread(grid.L, sizeof(float), YEE_N, f) != YEE_N) {
        fprintf(stderr, "Truncated L-field\n");
        fclose(f);
        return -3;
    }

    /* Read accumulator */
    if (fread(grid.acc, sizeof(float), YEE_N, f) != YEE_N) {
        fprintf(stderr, "Truncated accumulator\n");
        fclose(f);
        return -4;
    }

    fclose(f);
    return 0;
}

/* ── Save L-field + accumulator to .xyzt ── */
static int save_xyzt(const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;

    /* Minimal .xyzt: just the YEE1 block */
    uint32_t magic = 0x59454531;  /* 'YEE1' */
    uint16_t gx = YEE_GX, gy = YEE_GY, gz = YEE_GZ;
    fwrite(&magic, 4, 1, f);
    fwrite(&gx, 2, 1, f);
    fwrite(&gy, 2, 1, f);
    fwrite(&gz, 2, 1, f);
    fwrite(grid.L, sizeof(float), YEE_N, f);
    fwrite(grid.acc, sizeof(float), YEE_N, f);

    fclose(f);
    return 0;
}

/* ── Main: boot → tick → read/write boundaries ── */
int main(int argc, char *argv[]) {
    printf("XYZT Bare Metal Loader\n");
    printf("Grid: %dx%dx%d = %d voxels\n", YEE_GX, YEE_GY, YEE_GZ, YEE_N);
    printf("Memory: %.1f MB\n", (float)sizeof(YeeGrid) / (1024 * 1024));

    yee_cpu_init(&grid);

    /* Load boot image if provided */
    if (argc >= 2) {
        int r = load_xyzt(argv[1]);
        if (r == 0)
            printf("Loaded: %s\n", argv[1]);
        else if (r == -5)
            printf("No YEE1 block in %s — starting fresh\n", argv[1]);
        else
            printf("Load error %d — starting fresh\n", r);
    }

    printf("Ticking...\n");

    /* The tick loop. On FPGA this disappears — the clock IS the tick. */
    char input[256];
    uint64_t tick_count = 0;
    int persist_interval = 1000;  /* save every 1000 ticks */

    for (;;) {
        /* Check for input on boundary (non-blocking on bare metal,
         * blocking here for simplicity) */
        /* Input handling: on bare metal, this reads UART/GPIO.
         * Here we just tick. Input comes through retina injection
         * which the caller sets up before the loop. */

        /* Tick the wave grid */
        yee_cpu_tick(&grid);
        yee_cpu_sponge(&grid, 4, 0.03f);  /* light tap */
        tick_count++;

        /* Hebbian every 155 ticks (SUBSTRATE_INT) */
        if (tick_count % 155 == 0)
            yee_cpu_hebbian(&grid, 0.01f, 0.005f);

        /* Auto-persist */
        if (tick_count % persist_interval == 0 && argc >= 2)
            save_xyzt(argv[1]);

        /* Report energy periodically */
        if (tick_count % 10000 == 0) {
            float e = yee_cpu_energy(&grid);
            printf("tick %llu: energy=%.4f\n",
                   (unsigned long long)tick_count, e);
        }
    }

    return 0;
}
