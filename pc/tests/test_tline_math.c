/*
 * test_tline_math.c
 * Verifies transmission line propagation math.
 */

#include <stdio.h>
#include "../tline.h"

void print_cells(const TLine *tl, int tick) {
    printf("Tick %3d: ", tick);
    for (int i = 0; i < tl->n_cells; i++) {
        printf("%7.2f ", tl->V[i]);
    }
    printf("| out=%.2f\n", tline_read(tl));
}

int main() {
    printf("=== TLINE MATHEMATICS VERIFICATION ===\n\n");

    /* Test 1: DC step response (255 held constant) */
    printf("TEST 1: DC Step Response (N=4, R=0.15, G=0.02, Lc=0.1)\n");
    printf("Expected: Signal ramps up over ~5-6 ticks\n\n");

    TLine tl_dc;
    tline_init(&tl_dc, 4, 1.0);  /* z0=1.0, R=0.15, G=0.02 */

    /* Set Lc to 0.1 (crystallized, low loss) */
    for (int i = 0; i < 4; i++) tl_dc.Lc[i] = 0.1;

    /* Inject 255 at cell 0 and hold */
    tline_inject(&tl_dc, 255.0);

    for (int tick = 1; tick <= 10; tick++) {
        print_cells(&tl_dc, tick);
        tline_step(&tl_dc);
        /* Re-inject to hold at 255 */
        tline_inject(&tl_dc, 255.0);
    }

    printf("\n");

    /* Test 2: AC alternating signal (255, 0, 255, 0...) */
    printf("TEST 2: AC Alternating Signal (255, 0, 255, 0...)\n");
    printf("Expected: Heavy attenuation, averages toward ~127\n\n");

    TLine tl_ac;
    tline_init(&tl_ac, 4, 1.0);
    for (int i = 0; i < 4; i++) tl_ac.Lc[i] = 0.1;

    int ac_val = 255;
    for (int tick = 1; tick <= 10; tick++) {
        printf("Tick %3d: inject=%3d  ", tick, ac_val);
        tline_inject(&tl_ac, (double)ac_val);
        tline_step(&tl_ac);
        print_cells(&tl_ac, tick);
        ac_val = (ac_val == 255) ? 0 : 255;  /* Toggle */
    }

    printf("\n");

    /* Test 3: Long edge (N=32) DC response */
    printf("TEST 3: Long Edge DC Response (N=32)\n");
    printf("Expected: ~30+ ticks to reach far end\n\n");

    TLine tl_long;
    tline_init(&tl_long, 32, 1.0);
    for (int i = 0; i < 32; i++) tl_long.Lc[i] = 0.1;

    tline_inject(&tl_long, 255.0);

    for (int tick = 1; tick <= 40; tick++) {
        tline_step(&tl_long);
        tline_inject(&tl_long, 255.0);
        if (tick % 5 == 0 || tick <= 5) {
            printf("Tick %3d: out=%.2f  (cell[16]=%.2f, cell[24]=%.2f)\n",
                   tick, tline_read(&tl_long),
                   tline_read_at(&tl_long, 16),
                   tline_read_at(&tl_long, 24));
        }
    }

    printf("\n");

    /* Test 4: Attenuation product (weight calculation) */
    printf("TEST 4: Attenuation Product (Weight)\n");

    TLine tl_w;
    tline_init(&tl_w, 8, 1.0);

    /* Perfect edge (Lc=0, only R=0.15 loss) */
    for (int i = 0; i < 8; i++) tl_w.Lc[i] = 0.0;
    printf("Perfect edge (Lc=0): weight=%d (atten per cell = %.3f)\n",
           tline_weight(&tl_w), 1.0 - 0.15);

    /* Crystallized edge (Lc=0.1) */
    for (int i = 0; i < 8; i++) tl_w.Lc[i] = 0.1;
    printf("Crystallized (Lc=0.1): weight=%d (atten per cell = %.3f)\n",
           tline_weight(&tl_w), 1.0 - (0.15 + 0.02 * 0.1));

    /* High impedance edge (Lc=5.0) */
    for (int i = 0; i < 8; i++) tl_w.Lc[i] = 5.0;
    printf("High impedance (Lc=5.0): weight=%d (atten per cell = %.3f)\n",
           tline_weight(&tl_w), 1.0 - (0.15 + 0.02 * 5.0));

    return 0;
}
