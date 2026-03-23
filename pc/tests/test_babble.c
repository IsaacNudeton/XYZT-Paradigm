/*
 * test_babble.c — Two Engines, One Wire
 *
 * Engine A learns structure (log data). Engine B starts empty.
 * A's sponge output feeds B's retina. B's sponge output feeds A's retina.
 * After babbling cycles, does B respond differently to structured
 * data vs noise? If yes, A's voice carried structure. Gap 5 is T1.
 *
 * The wire between them is just bytes. No protocol. No format.
 * Raw impedance. The Hebbian on both sides carves whatever
 * waveguide makes the signal intelligible.
 *
 * Isaac Oravec & Claude, March 2026
 */

#include "test.h"
#include <stdlib.h>

/* Yee API */
extern int yee_init(void);
extern void yee_destroy(void);
extern int yee_tick(void);
extern int yee_clear_fields(void);
extern int yee_apply_sponge(int width, float rate);
extern int yee_is_initialized(void);
extern int yee_download_output(float *h_output, int n);
extern int yee_clear_output(void);
extern int yee_download_L(float *h_L, int n);
extern int yee_upload_L(const float *h_L, int n);
extern int yee_hebbian_adaptive(float str, float wk);
extern double yee_energy(void);

#define YEE_GX 64
#define YEE_GY 64
#define YEE_GZ 64
#define YEE_N  (YEE_GX * YEE_GY * YEE_GZ)

#define BABBLE_TICKS    60   /* ticks per utterance */
#define BABBLE_CYCLES   5    /* back-and-forth cycles */

void run_babble_test(void) {
    printf("\n=== BABBLE TEST: TWO ENGINES, ONE WIRE ===\n");

    if (!yee_is_initialized()) {
        if (yee_init() != 0) {
            printf("  Yee init failed — skipping\n");
            return;
        }
    }

    /* Two L-fields: A's carved impedance and B's blank slate */
    float *L_a = (float *)malloc(YEE_N * sizeof(float));
    float *L_b = (float *)malloc(YEE_N * sizeof(float));
    float *output = (float *)malloc(YEE_N * sizeof(float));
    uint8_t voice_a[64], voice_b[64];

    /* ── Phase 1: Train engine A on structured data ── */
    printf("  Training engine A on log data...\n");
    yee_clear_fields();

    const uint8_t *training[] = {
        (const uint8_t *)"Event[7001]: Service started OK",
        (const uint8_t *)"Event[7036]: Performance counter",
        (const uint8_t *)"Event[1014]: DNS resolution fail",
        (const uint8_t *)"Event[4624]: Logon successful.",
        (const uint8_t *)"Event[7040]: Service state change",
    };

    for (int pass = 0; pass < 3; pass++) {
        for (int s = 0; s < 5; s++) {
            wire_retina_inject(training[s], 31, 0.5f);
            for (int t = 0; t < 60; t++) {
                yee_tick();
                yee_apply_sponge(4, 0.03f);
            }
        }
        yee_hebbian_adaptive(0.01f, 0.005f);
    }

    /* Save A's carved L-field */
    yee_download_L(L_a, YEE_N);
    printf("  Engine A trained (3 passes × 5 samples)\n");

    /* Initialize B's L-field as uniform (blank) */
    for (int i = 0; i < YEE_N; i++) L_b[i] = 1.0f;  /* L_WIRE = uniform */

    /* ── Phase 2: Babbling loop ── */
    printf("  Babbling: A speaks → B listens → B speaks → A listens\n");

    for (int cycle = 0; cycle < BABBLE_CYCLES; cycle++) {
        /* A speaks: load A's L-field, inject training, read sponge */
        yee_clear_fields();
        yee_clear_output();
        yee_upload_L(L_a, YEE_N);
        wire_retina_inject(training[cycle % 5], 31, 0.5f);
        for (int t = 0; t < BABBLE_TICKS; t++) {
            yee_tick();
            yee_apply_sponge(4, 0.15f);
        }
        wire_output_decode(voice_a, 64);
        yee_download_L(L_a, YEE_N);  /* save any Hebbian changes */

        /* B listens: load B's L-field, inject A's voice through retina */
        yee_clear_fields();
        yee_clear_output();
        yee_upload_L(L_b, YEE_N);
        wire_retina_inject(voice_a, 64, 0.5f);
        for (int t = 0; t < BABBLE_TICKS; t++) {
            yee_tick();
            yee_apply_sponge(4, 0.03f);  /* light sponge — B is learning */
        }
        yee_hebbian_adaptive(0.01f, 0.005f);
        wire_output_decode(voice_b, 64);
        yee_download_L(L_b, YEE_N);

        /* A listens to B's response (closes the loop) */
        yee_clear_fields();
        yee_clear_output();
        yee_upload_L(L_a, YEE_N);
        wire_retina_inject(voice_b, 64, 0.3f);
        for (int t = 0; t < BABBLE_TICKS; t++) {
            yee_tick();
            yee_apply_sponge(4, 0.03f);
        }
        yee_hebbian_adaptive(0.01f, 0.005f);
        yee_download_L(L_a, YEE_N);

        printf("  cycle %d: A→B→A complete\n", cycle + 1);
    }

    /* ── Phase 3: Test B's discrimination ── */
    printf("  Testing if B learned from A's voice...\n");

    /* B responds to structured data (similar to A's training) */
    yee_clear_fields();
    yee_clear_output();
    yee_upload_L(L_b, YEE_N);
    const uint8_t *test_struct = (const uint8_t *)"Event[5001]: Disk write complete";
    wire_retina_inject(test_struct, 31, 0.5f);
    for (int t = 0; t < BABBLE_TICKS; t++) {
        yee_tick();
        yee_apply_sponge(4, 0.15f);
    }
    float *out_struct = (float *)calloc(YEE_N, sizeof(float));
    yee_download_output(out_struct, YEE_N);
    double energy_struct = 0;
    for (int i = 0; i < YEE_N; i++) energy_struct += out_struct[i];

    /* B responds to noise (random bytes) */
    yee_clear_fields();
    yee_clear_output();
    yee_upload_L(L_b, YEE_N);
    uint8_t noise[31];
    uint32_t rng = 0xDEADBEEF;
    for (int i = 0; i < 31; i++) {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        noise[i] = (uint8_t)(rng & 0xFF);
    }
    wire_retina_inject(noise, 31, 0.5f);
    for (int t = 0; t < BABBLE_TICKS; t++) {
        yee_tick();
        yee_apply_sponge(4, 0.15f);
    }
    float *out_noise = (float *)calloc(YEE_N, sizeof(float));
    yee_download_output(out_noise, YEE_N);
    double energy_noise = 0;
    for (int i = 0; i < YEE_N; i++) energy_noise += out_noise[i];

    /* Compare: does B respond differently? */
    printf("  B's response to structured data: %.4f\n", energy_struct);
    printf("  B's response to noise:           %.4f\n", energy_noise);
    double ratio = (energy_noise > 0.001) ? energy_struct / energy_noise : 0;
    printf("  Discrimination ratio: %.2fx\n", ratio);

    CHECK("babble: B responds to input", energy_struct > 0.001 || energy_noise > 0.001);
    CHECK("babble: B discriminates structure from noise",
          fabs(energy_struct - energy_noise) > 0.001);

    /* Check that B's L-field differentiated (not still uniform) */
    float l_min = 999, l_max = 0;
    for (int i = 0; i < YEE_N; i++) {
        if (L_b[i] < l_min) l_min = L_b[i];
        if (L_b[i] > l_max) l_max = L_b[i];
    }
    printf("  B's L-field range: [%.4f, %.4f]\n", l_min, l_max);
    CHECK("babble: B's L-field carved by A's voice", l_max > l_min + 0.01f);

    free(L_a); free(L_b); free(output);
    free(out_struct); free(out_noise);
}
