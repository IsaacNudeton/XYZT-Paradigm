/*
 * test_resonance.c — Sympathetic Resonance Test
 *
 * Proves: same-shaped L-field cavities resonate at the same frequency
 * even without direct connection (no edge, no wave path).
 *
 * Two nodes that co-occurred with B get similar L-field neighborhoods.
 * They are "tuning forks" with the same shape. When the engine runs,
 * their coherence fields (V_signed / V_accum) match — they sing the
 * same note — even though no signal traveled between them.
 *
 * This is transitive association through harmony, not logic.
 * The wave field can't propagate A→B→C (18-test TLine experiment proved that).
 * But same-shaped cavities resonate identically. The coherence field detects it.
 *
 * Isaac Oravec, Claude & Gemini, March 2026
 */

#include "test.h"
#include <math.h>

extern int yee_init(void);
extern void yee_destroy(void);
extern int yee_tick(void);
extern int yee_download_acc_raw(float *h_acc, int n);
extern int yee_download_signed(float *h_signed, int n);
extern int yee_hebbian(float str, float wk);
extern int yee_download_acc(uint8_t *h_sub, int n);

extern int wire_engine_to_yee(const Engine *eng);
extern int wire_yee_to_engine(Engine *eng);
extern int wire_yee_retinas(Engine *eng, uint8_t *yee_sub);

#define YEE_TOTAL (64 * 64 * 64)

void run_resonance_test(void) {
    printf("\n=== SYMPATHETIC RESONANCE TEST ===\n");

    Engine eng;
    engine_init(&eng);
    if (yee_init() != 0) { printf("  Yee init failed\n"); engine_destroy(&eng); return; }
    BitStream bs;
    uint8_t *yee_sub = (uint8_t *)calloc(YEE_TOTAL, 1);

    /* Create three concepts:
     * A and C both co-occur with B but never with each other.
     * D is unrelated — different structure entirely. */

    /* Phase 1: A + B co-occurrence */
    uint8_t data_a[] = "concept_alpha_first_pattern_data_stream_one";
    uint8_t data_b[] = "concept_bridge_shared_context_between_both";
    encode_bytes(&bs, data_a, 43); engine_ingest(&eng, "res_A", &bs);
    encode_bytes(&bs, data_b, 43); engine_ingest(&eng, "res_B", &bs);

    /* Run together so they carve the same L-field region */
    for (int t = 0; t < (int)SUBSTRATE_INT * 3; t++) {
        wire_engine_to_yee(&eng);
        yee_tick();
        engine_tick(&eng);
    }
    yee_download_acc(yee_sub, YEE_TOTAL);
    wire_yee_retinas(&eng, yee_sub);
    wire_yee_to_engine(&eng);
    yee_hebbian(0.01f, 0.005f);

    /* Phase 2: B + C co-occurrence (A is no longer injected) */
    uint8_t data_c[] = "concept_gamma_second_pattern_data_stream_two";
    encode_bytes(&bs, data_c, 44); engine_ingest(&eng, "res_C", &bs);

    for (int t = 0; t < (int)SUBSTRATE_INT * 3; t++) {
        wire_engine_to_yee(&eng);
        yee_tick();
        engine_tick(&eng);
    }
    yee_download_acc(yee_sub, YEE_TOTAL);
    wire_yee_retinas(&eng, yee_sub);
    wire_yee_to_engine(&eng);
    yee_hebbian(0.01f, 0.005f);

    /* Phase 3: D is completely different (never co-occurred with B) */
    uint8_t data_d[] = "zzzzz_completely_unrelated_different_domain_xyz";
    encode_bytes(&bs, data_d, 47); engine_ingest(&eng, "res_D", &bs);

    for (int t = 0; t < (int)SUBSTRATE_INT * 3; t++) {
        wire_engine_to_yee(&eng);
        yee_tick();
        engine_tick(&eng);
    }
    yee_download_acc(yee_sub, YEE_TOTAL);
    wire_yee_retinas(&eng, yee_sub);
    wire_yee_to_engine(&eng);
    yee_hebbian(0.01f, 0.005f);

    /* Phase 4: Read coherence at each node's voxel */
    float *h_acc = (float *)malloc(YEE_TOTAL * sizeof(float));
    float *h_signed = (float *)malloc(YEE_TOTAL * sizeof(float));
    yee_download_acc_raw(h_acc, YEE_TOTAL);
    yee_download_signed(h_signed, YEE_TOTAL);

    Graph *g0 = &eng.shells[0].g;
    int id_a = graph_find(g0, "res_A");
    int id_c = graph_find(g0, "res_C");
    int id_d = graph_find(g0, "res_D");

    check("resonance: nodes exist", 1,
          (id_a >= 0 && id_c >= 0 && id_d >= 0) ? 1 : 0);

    if (id_a >= 0 && id_c >= 0 && id_d >= 0) {
        /* Get coherence ratio at each node's voxel */
        int va = coord_x(g0->nodes[id_a].coord) % 64 +
                 (coord_y(g0->nodes[id_a].coord) % 64) * 64 +
                 (coord_z(g0->nodes[id_a].coord) % 64) * 64 * 64;
        int vc = coord_x(g0->nodes[id_c].coord) % 64 +
                 (coord_y(g0->nodes[id_c].coord) % 64) * 64 +
                 (coord_z(g0->nodes[id_c].coord) % 64) * 64 * 64;
        int vd = coord_x(g0->nodes[id_d].coord) % 64 +
                 (coord_y(g0->nodes[id_d].coord) % 64) * 64 +
                 (coord_z(g0->nodes[id_d].coord) % 64) * 64 * 64;

        float coh_a = (h_acc[va] > 0.001f) ? fabsf(h_signed[va]) / h_acc[va] : 0;
        float coh_c = (h_acc[vc] > 0.001f) ? fabsf(h_signed[vc]) / h_acc[vc] : 0;
        float coh_d = (h_acc[vd] > 0.001f) ? fabsf(h_signed[vd]) / h_acc[vd] : 0;

        float diff_ac = fabsf(coh_a - coh_c);
        float diff_ad = fabsf(coh_a - coh_d);

        printf("  coherence A=%.4f  C=%.4f  D=%.4f\n", coh_a, coh_c, coh_d);
        printf("  A-C diff=%.4f (should be small — same-shaped cavities)\n", diff_ac);
        printf("  A-D diff=%.4f (should be large — different shapes)\n", diff_ad);

        /* A and C should have more similar coherence than A and D.
         * They were shaped by the same context (B). */
        check("resonance: A-D diff > A-C diff (sympathetic)", 1,
              diff_ad > diff_ac ? 1 : 0);
    }

    free(h_acc);
    free(h_signed);
    free(yee_sub);
    yee_destroy();
    engine_destroy(&eng);
}
