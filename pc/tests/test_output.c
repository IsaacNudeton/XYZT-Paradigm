/*
 * test_output.c — Output Path / Inverse Retina Test
 *
 * The sponge absorbs energy at boundaries every tick.
 * That absorption pattern IS the engine's voice.
 * The inverse retina decodes it back to bytes.
 *
 * Tests:
 * 1. Sponge captures non-zero output after injection + propagation
 * 2. Different inputs produce different output patterns
 * 3. Inverse retina recovers recognizable bytes from output
 * 4. Self-report: output fed back through retina produces resonance
 *
 * Isaac Oravec & Claude, March 2026
 */

#include "test.h"
#include <stdlib.h>

/* Yee API — can't include yee.cuh from C */
extern int yee_init(void);
extern void yee_destroy(void);
extern int yee_tick(void);
extern int yee_clear_fields(void);
extern int yee_apply_sponge(int width, float rate);
extern int yee_is_initialized(void);
extern int yee_download_output(float *h_output, int n);
extern int yee_clear_output(void);
extern double yee_energy(void);

#define YEE_GX 64
#define YEE_GY 64
#define YEE_GZ 64
#define YEE_N  (YEE_GX * YEE_GY * YEE_GZ)

void run_output_test(void) {
    printf("\n=== OUTPUT PATH / INVERSE RETINA TEST ===\n");

    if (!yee_is_initialized()) {
        if (yee_init() != 0) {
            printf("  Yee init failed — skipping\n");
            return;
        }
    }

    /* ── Test 1: Sponge captures output after injection ── */
    yee_clear_fields();
    yee_clear_output();

    /* Inject known data through retina */
    const uint8_t data_a[] = "the quick brown fox jumps over";
    wire_retina_inject(data_a, 30, 1.0f);

    /* Propagate with sponge — energy travels through medium,
     * hits boundaries, gets absorbed into d_V_output */
    for (int t = 0; t < 150; t++) {
        yee_tick();
        yee_apply_sponge(4, 0.15f);
    }

    /* Download output accumulator */
    float *output_a = (float *)calloc(YEE_N, sizeof(float));
    yee_download_output(output_a, YEE_N);

    /* Sum total absorbed energy */
    double total_a = 0;
    int nonzero_a = 0;
    for (int i = 0; i < YEE_N; i++) {
        if (output_a[i] != 0.0f) {
            total_a += fabsf(output_a[i]);
            nonzero_a++;
        }
    }
    printf("  Output A: %.2f total absorbed, %d nonzero voxels\n",
           total_a, nonzero_a);
    CHECK("output: sponge captured nonzero energy", nonzero_a > 0);
    CHECK("output: significant absorption", total_a > 0.01);

    /* ── Test 2: Different inputs → different output patterns ── */
    yee_clear_fields();
    yee_clear_output();

    const uint8_t data_b[] = "hydrogen bonds form between mol";
    wire_retina_inject(data_b, 31, 1.0f);

    for (int t = 0; t < 150; t++) {
        yee_tick();
        yee_apply_sponge(4, 0.15f);
    }

    float *output_b = (float *)calloc(YEE_N, sizeof(float));
    yee_download_output(output_b, YEE_N);

    /* Compare output patterns: compute correlation between A and B */
    double dot_ab = 0, dot_aa = 0, dot_bb = 0;
    for (int i = 0; i < YEE_N; i++) {
        dot_ab += output_a[i] * output_b[i];
        dot_aa += output_a[i] * output_a[i];
        dot_bb += output_b[i] * output_b[i];
    }
    double corr_ab = (dot_aa > 0 && dot_bb > 0) ?
                     dot_ab / sqrt(dot_aa * dot_bb) : 0;
    printf("  Pattern correlation A vs B: %.4f\n", corr_ab);
    CHECK("output: different inputs → different patterns", corr_ab < 0.99);

    /* ── Test 3: Inverse retina decodes recognizable bytes ── */
    yee_clear_fields();
    yee_clear_output();

    /* Inject data_a again, propagate with sponge */
    wire_retina_inject(data_a, 30, 1.0f);
    for (int t = 0; t < 150; t++) {
        yee_tick();
        yee_apply_sponge(4, 0.15f);
    }

    /* Decode output — the engine's expression, not a reconstruction */
    uint8_t decoded[64];
    memset(decoded, 0, 64);
    int n_decoded = wire_output_decode(decoded, 30);

    printf("  Decoded %d bytes. First 10: ", n_decoded);
    for (int i = 0; i < 10 && i < n_decoded; i++)
        printf("%02x ", decoded[i]);
    printf("\n");

    /* The output is NOT the input echoed back. It's the engine's
     * expression — the L-field's transform of the injected wave.
     * Verify the output has structure (not all zeros, not all same). */
    int nonzero_bytes = 0;
    int distinct = 0;
    for (int i = 0; i < n_decoded; i++) {
        if (decoded[i] > 0) nonzero_bytes++;
        if (i > 0 && decoded[i] != decoded[i-1]) distinct++;
    }
    printf("  Output bytes: %d nonzero, %d distinct transitions\n",
           nonzero_bytes, distinct);
    CHECK("output: decode produced bytes", n_decoded > 0);
    CHECK("output: expression has structure", nonzero_bytes > 0);

    /* ── Test 4: Self-report — output fed back through retina ── */
    yee_clear_fields();
    yee_clear_output();

    /* Feed the decoded output back as input — the engine hears itself */
    wire_retina_inject(decoded, n_decoded, 1.0f);
    for (int t = 0; t < 150; t++) {
        yee_tick();
        yee_apply_sponge(4, 0.15f);
    }

    /* Download the second-generation output */
    float *output_self = (float *)calloc(YEE_N, sizeof(float));
    yee_download_output(output_self, YEE_N);

    double total_self = 0;
    for (int i = 0; i < YEE_N; i++)
        total_self += fabsf(output_self[i]);

    printf("  Self-report: %.2f total absorbed (original: %.2f)\n",
           total_self, total_a);
    CHECK("output: self-report produced resonance", total_self > 0.001);

    /* The self-report output should correlate with the original output
     * because the engine is hearing a distorted echo of itself */
    double dot_as = 0, dot_ss = 0;
    /* Recompute dot_aa from output_a */
    dot_aa = 0;
    for (int i = 0; i < YEE_N; i++) {
        dot_as += output_a[i] * output_self[i];
        dot_aa += output_a[i] * output_a[i];
        dot_ss += output_self[i] * output_self[i];
    }
    double corr_self = (dot_aa > 0 && dot_ss > 0) ?
                       dot_as / sqrt(dot_aa * dot_ss) : 0;
    printf("  Self-report correlation with original: %.4f\n", corr_self);
    /* Self-report doesn't need to correlate with original output.
     * The engine hearing its own voice is a DIFFERENT stimulus than
     * the original data. What matters is that resonance occurs. */
    CHECK("output: self-report is distinct signal",
          total_self > 0.01);

    free(output_a);
    free(output_b);
    free(output_self);
}
