/*
 * test_lfield.c — L-field differentiation diagnostic
 *
 * The critical question: does Hebbian learning on the Yee substrate
 * carve distinct impedance patterns for structurally different inputs?
 * If yes, the substrate is doing real spatial computation.
 * If no, everything homogenizes and the Yee swap was cosmetic.
 *
 * Method: ingest two structurally different corpora, run Hebbian,
 * download L-field, compare neighborhoods around each corpus's nodes.
 *
 * Isaac Oravec & Claude, March 2026
 */

#include "test.h"

/* Yee GPU functions — pure C */
extern int yee_init(void);
extern void yee_destroy(void);
extern int yee_tick(void);
extern int yee_hebbian(float strengthen_rate, float weaken_rate);
extern int yee_hebbian_ex(float strengthen_rate, float weaken_rate, float threshold);
extern int yee_download_acc_raw(float *h_acc, int n);
extern int yee_download_L(float *h_L, int n);
extern int yee_download_acc(uint8_t *h_substrate, int n);

/* Wire bridge */
extern int wire_engine_to_yee(const Engine *eng);
extern int wire_yee_to_engine(Engine *eng);
extern int wire_yee_retinas(Engine *eng, uint8_t *yee_substrate);

#define YEE_GX 64
#define YEE_GY 64
#define YEE_GZ 64
#define YEE_TOTAL (YEE_GX * YEE_GY * YEE_GZ)

static int yee_vid(int gx, int gy, int gz) {
    return gx + gy * YEE_GX + gz * YEE_GX * YEE_GY;
}

/* Compute L statistics in a radius-r neighborhood around a voxel */
typedef struct {
    float mean;
    float min;
    float max;
    float variance;
    int   n_wire;   /* L < 0.95 (carved toward wire) */
    int   n_vac;    /* L > 5.0 (pushed toward vacuum) */
    int   n_total;
} LStats;

static LStats l_neighborhood(const float *L, int cx, int cy, int cz, int r) {
    LStats s = {0};
    s.min = 999.0f;
    s.max = 0.0f;
    float sum = 0, sum2 = 0;
    int count = 0;

    for (int dz = -r; dz <= r; dz++)
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                int gx = (cx + dx + YEE_GX) % YEE_GX;
                int gy = (cy + dy + YEE_GY) % YEE_GY;
                int gz = (cz + dz + YEE_GZ) % YEE_GZ;
                float l = L[yee_vid(gx, gy, gz)];
                sum += l;
                sum2 += l * l;
                if (l < s.min) s.min = l;
                if (l > s.max) s.max = l;
                if (l < 0.95f) s.n_wire++;
                if (l > 5.0f) s.n_vac++;
                count++;
            }

    s.n_total = count;
    s.mean = sum / count;
    s.variance = (sum2 / count) - (s.mean * s.mean);
    return s;
}

/* ══════════════════════════════════════════════════════════════ */

void run_lfield_test(void) {
    printf("\n=== L-FIELD DIFFERENTIATION TEST ===\n\n");

    Engine eng;
    engine_init(&eng);

    if (yee_init() != 0) {
        printf("  Yee init failed — skipping L-field test\n");
        engine_destroy(&eng);
        return;
    }

    /* ── Phase 1: Ingest two structurally different corpora ── */

    /* Corpus A: natural language, animals, spatial */
    static const char *corpus_a[] = {
        "the quick brown fox jumps over the lazy dog by the river",
        "a brown fox runs through the forest near the winding river",
        "the lazy dog sleeps under the oak tree beside the river bank",
        "fox and dog rest together on the grassy river bank at dawn",
        "the river flows past the oak where the fox watches the dog",
        "brown fox and lazy dog share the riverbank meadow morning",
        "the oak tree shades the sleeping dog while fox hunts nearby",
        "fog rises from the river as the fox trots past the old oak",
    };

    /* Corpus B: technical, chemistry, no overlap with A */
    static const char *corpus_b[] = {
        "hydrogen bonds form between water molecules in liquid state",
        "carbon atoms create diamond lattice under extreme pressure",
        "oxygen reacts with iron producing rust on exposed surfaces",
        "nitrogen makes up most of the atmosphere above sea level",
        "helium atoms resist bonding due to full electron shell config",
        "silicon crystals conduct electricity when doped with phosphorus",
        "chlorine gas dissolves in water producing hydrochloric acid",
        "sodium metal reacts violently with water releasing hydrogen",
    };

    int ids_a[8], ids_b[8];
    BitStream bs;

    printf("  Ingesting corpus A (natural language)...\n");
    for (int i = 0; i < 8; i++) {
        encode_bytes(&bs, (const uint8_t *)corpus_a[i],
                     (int)strlen(corpus_a[i]));
        char name[32];
        snprintf(name, sizeof(name), "lf_a_%d", i);
        ids_a[i] = engine_ingest(&eng, name, &bs);
    }

    printf("  Ingesting corpus B (chemistry)...\n");
    for (int i = 0; i < 8; i++) {
        encode_bytes(&bs, (const uint8_t *)corpus_b[i],
                     (int)strlen(corpus_b[i]));
        char name[32];
        snprintf(name, sizeof(name), "lf_b_%d", i);
        ids_b[i] = engine_ingest(&eng, name, &bs);
    }

    /* Record node positions */
    Graph *g0 = &eng.shells[0].g;
    printf("\n  Node positions (hash → voxel):\n");
    printf("  %-6s %-20s %3s %3s %3s\n", "ID", "Name", "X", "Y", "Z");
    for (int i = 0; i < 8; i++) {
        if (ids_a[i] >= 0) {
            Node *n = &g0->nodes[ids_a[i]];
            printf("  %-6d %-20s %3d %3d %3d\n", ids_a[i], n->name,
                   coord_x(n->coord) % 64, coord_y(n->coord) % 64,
                   coord_z(n->coord) % 64);
        }
    }
    for (int i = 0; i < 8; i++) {
        if (ids_b[i] >= 0) {
            Node *n = &g0->nodes[ids_b[i]];
            printf("  %-6d %-20s %3d %3d %3d\n", ids_b[i], n->name,
                   coord_x(n->coord) % 64, coord_y(n->coord) % 64,
                   coord_z(n->coord) % 64);
        }
    }

    /* ── Phase 2: Download initial L-field (should be uniform 1.0) ── */
    float *L_init = (float *)malloc(YEE_TOTAL * sizeof(float));
    yee_download_L(L_init, YEE_TOTAL);

    float l_init_mean = 0;
    for (int i = 0; i < YEE_TOTAL; i++) l_init_mean += L_init[i];
    l_init_mean /= YEE_TOTAL;
    printf("\n  L-field before ticking: mean=%.4f (should be ~1.0)\n", l_init_mean);

    /* ── Phase 3: Warm up — run 1 cycle to build accumulator, then measure ── */
    uint8_t *yee_sub = (uint8_t *)calloc(YEE_TOTAL, 1);
    float *h_acc = (float *)malloc(YEE_TOTAL * sizeof(float));

    printf("  Warming up (1 SUBSTRATE_INT cycle)...\n");
    for (int t = 0; t < (int)SUBSTRATE_INT; t++) {
        wire_engine_to_yee(&eng);
        yee_tick();
        engine_tick(&eng);
    }

    /* Measure accumulator distribution to set threshold */
    yee_download_acc_raw(h_acc, YEE_TOTAL);
    float acc_min = 1e9f, acc_max = 0, acc_mean = 0;
    float acc_at_a0 = 0, acc_at_b0 = 0;
    for (int i = 0; i < YEE_TOTAL; i++) {
        acc_mean += h_acc[i];
        if (h_acc[i] < acc_min) acc_min = h_acc[i];
        if (h_acc[i] > acc_max) acc_max = h_acc[i];
    }
    acc_mean /= YEE_TOTAL;

    if (ids_a[0] >= 0) {
        int ax = coord_x(g0->nodes[ids_a[0]].coord) % 64;
        int ay = coord_y(g0->nodes[ids_a[0]].coord) % 64;
        int az = coord_z(g0->nodes[ids_a[0]].coord) % 64;
        acc_at_a0 = h_acc[yee_vid(ax, ay, az)];
    }
    if (ids_b[0] >= 0) {
        int bx = coord_x(g0->nodes[ids_b[0]].coord) % 64;
        int by = coord_y(g0->nodes[ids_b[0]].coord) % 64;
        int bz = coord_z(g0->nodes[ids_b[0]].coord) % 64;
        acc_at_b0 = h_acc[yee_vid(bx, by, bz)];
    }

    /* Histogram: count voxels in accumulator bins */
    int bin_counts[10] = {0};
    for (int i = 0; i < YEE_TOTAL; i++) {
        int bin = (int)(h_acc[i] / (acc_max / 10.0f + 1e-10f));
        if (bin >= 10) bin = 9;
        if (bin < 0) bin = 0;
        bin_counts[bin]++;
    }

    printf("\n  === ACCUMULATOR DISTRIBUTION (after 1 cycle) ===\n");
    printf("  min=%.4f  mean=%.4f  max=%.4f\n", acc_min, acc_mean, acc_max);
    printf("  acc at A[0]=%.4f  acc at B[0]=%.4f\n", acc_at_a0, acc_at_b0);
    printf("  histogram (10 bins, 0..%.2f):\n", acc_max);
    for (int b = 0; b < 10; b++) {
        float lo = b * acc_max / 10.0f;
        float hi = (b + 1) * acc_max / 10.0f;
        printf("    [%.3f-%.3f]: %6d (%.1f%%)\n",
               lo, hi, bin_counts[b], 100.0f * bin_counts[b] / YEE_TOTAL);
    }

    /* Set threshold at midpoint between bulk and source peaks.
     * Bulk ≈ acc_mean. Sources ≈ acc_max. Midpoint separates them.
     * Voxels near injection sites (acc > threshold) → strengthen (lower L).
     * Background (acc < threshold) → weaken (raise L toward vacuum). */
    float threshold = (acc_mean + acc_max) / 2.0f;
    printf("\n  Using adaptive threshold: %.4f (midpoint of mean %.4f and max %.4f)\n",
           threshold, acc_mean, acc_max);

    /* ── Phase 4: Run Hebbian with adaptive threshold and aggressive rates ── */
    int n_cycles = 20;
    float str_rate = 0.01f;   /* 10x more aggressive than before */
    float weak_rate = 0.005f;

    printf("  Running %d cycles, strengthen=%.3f weaken=%.3f threshold=%.4f\n",
           n_cycles, str_rate, weak_rate, threshold);

    for (int cycle = 0; cycle < n_cycles; cycle++) {
        for (int t = 0; t < (int)SUBSTRATE_INT; t++) {
            wire_engine_to_yee(&eng);
            yee_tick();
            engine_tick(&eng);
        }
        /* Sync + Hebbian with adaptive threshold */
        yee_download_acc(yee_sub, YEE_TOTAL);
        wire_yee_retinas(&eng, yee_sub);
        wire_yee_to_engine(&eng);
        yee_hebbian_ex(str_rate, weak_rate, threshold);

        /* Progress every 5 cycles */
        if ((cycle + 1) % 5 == 0) {
            float *L_snap = (float *)malloc(YEE_TOTAL * sizeof(float));
            yee_download_L(L_snap, YEE_TOTAL);

            float snap_min = 999, snap_max = 0;
            int snap_wire = 0, snap_vac = 0;
            for (int i = 0; i < YEE_TOTAL; i++) {
                if (L_snap[i] < snap_min) snap_min = L_snap[i];
                if (L_snap[i] > snap_max) snap_max = L_snap[i];
                if (L_snap[i] < 0.95f) snap_wire++;
                if (L_snap[i] > 1.05f) snap_vac++;
            }

            float la = 0, lb = 0;
            if (ids_a[0] >= 0) {
                int ax = coord_x(g0->nodes[ids_a[0]].coord) % 64;
                int ay = coord_y(g0->nodes[ids_a[0]].coord) % 64;
                int az = coord_z(g0->nodes[ids_a[0]].coord) % 64;
                la = L_snap[yee_vid(ax, ay, az)];
            }
            if (ids_b[0] >= 0) {
                int bx = coord_x(g0->nodes[ids_b[0]].coord) % 64;
                int by = coord_y(g0->nodes[ids_b[0]].coord) % 64;
                int bz = coord_z(g0->nodes[ids_b[0]].coord) % 64;
                lb = L_snap[yee_vid(bx, by, bz)];
            }
            printf("  cycle %2d: L[a0]=%.4f L[b0]=%.4f  range=[%.4f, %.4f]  wire=%d vac=%d\n",
                   cycle + 1, la, lb, snap_min, snap_max, snap_wire, snap_vac);
            free(L_snap);
        }
    }
    free(h_acc);

    /* ── Phase 4: Download final L-field and analyze ── */
    float *L_final = (float *)malloc(YEE_TOTAL * sizeof(float));
    yee_download_L(L_final, YEE_TOTAL);

    /* Global L statistics */
    float gl_mean = 0, gl_min = 999, gl_max = 0;
    int gl_wire = 0, gl_vac = 0, gl_changed = 0;
    for (int i = 0; i < YEE_TOTAL; i++) {
        float l = L_final[i];
        gl_mean += l;
        if (l < gl_min) gl_min = l;
        if (l > gl_max) gl_max = l;
        if (l < 0.95f) gl_wire++;
        if (l > 5.0f) gl_vac++;
        if (fabsf(l - 1.0f) > 0.001f) gl_changed++;
    }
    gl_mean /= YEE_TOTAL;

    printf("\n  === GLOBAL L-FIELD ===\n");
    printf("  mean=%.4f  min=%.4f  max=%.4f\n", gl_mean, gl_min, gl_max);
    printf("  wire (L<0.95): %d/%d (%.1f%%)\n",
           gl_wire, YEE_TOTAL, 100.0f * gl_wire / YEE_TOTAL);
    printf("  vacuum (L>5.0): %d/%d (%.1f%%)\n",
           gl_vac, YEE_TOTAL, 100.0f * gl_vac / YEE_TOTAL);
    printf("  changed from 1.0: %d/%d (%.1f%%)\n",
           gl_changed, YEE_TOTAL, 100.0f * gl_changed / YEE_TOTAL);

    /* Per-corpus neighborhood analysis (radius 3) */
    printf("\n  === PER-CORPUS NEIGHBORHOODS (r=3) ===\n");
    printf("  %-8s %7s %7s %7s %8s %5s %5s\n",
           "Node", "mean", "min", "max", "var", "wire", "vac");

    float a_mean_sum = 0, b_mean_sum = 0;
    float a_var_sum = 0, b_var_sum = 0;
    int a_wire_sum = 0, b_wire_sum = 0;
    int a_count = 0, b_count = 0;

    for (int i = 0; i < 8; i++) {
        if (ids_a[i] < 0) continue;
        int gx = coord_x(g0->nodes[ids_a[i]].coord) % 64;
        int gy = coord_y(g0->nodes[ids_a[i]].coord) % 64;
        int gz = coord_z(g0->nodes[ids_a[i]].coord) % 64;
        LStats s = l_neighborhood(L_final, gx, gy, gz, 3);
        printf("  A[%d]    %7.4f %7.4f %7.4f %8.6f %5d %5d\n",
               i, s.mean, s.min, s.max, s.variance, s.n_wire, s.n_vac);
        a_mean_sum += s.mean;
        a_var_sum += s.variance;
        a_wire_sum += s.n_wire;
        a_count++;
    }

    for (int i = 0; i < 8; i++) {
        if (ids_b[i] < 0) continue;
        int gx = coord_x(g0->nodes[ids_b[i]].coord) % 64;
        int gy = coord_y(g0->nodes[ids_b[i]].coord) % 64;
        int gz = coord_z(g0->nodes[ids_b[i]].coord) % 64;
        LStats s = l_neighborhood(L_final, gx, gy, gz, 3);
        printf("  B[%d]    %7.4f %7.4f %7.4f %8.6f %5d %5d\n",
               i, s.mean, s.min, s.max, s.variance, s.n_wire, s.n_vac);
        b_mean_sum += s.mean;
        b_var_sum += s.variance;
        b_wire_sum += s.n_wire;
        b_count++;
    }

    /* ── Phase 5: Verdict ── */
    float a_avg_mean = a_count > 0 ? a_mean_sum / a_count : 1.0f;
    float b_avg_mean = b_count > 0 ? b_mean_sum / b_count : 1.0f;
    float a_avg_var = a_count > 0 ? a_var_sum / a_count : 0.0f;
    float b_avg_var = b_count > 0 ? b_var_sum / b_count : 0.0f;
    int a_avg_wire = a_count > 0 ? a_wire_sum / a_count : 0;
    int b_avg_wire = b_count > 0 ? b_wire_sum / b_count : 0;

    printf("\n  === SUMMARY ===\n");
    printf("  Corpus A avg: mean=%.4f  var=%.6f  wire/neighborhood=%d\n",
           a_avg_mean, a_avg_var, a_avg_wire);
    printf("  Corpus B avg: mean=%.4f  var=%.6f  wire/neighborhood=%d\n",
           b_avg_mean, b_avg_var, b_avg_wire);
    printf("  Global:       mean=%.4f  changed=%d/%d\n",
           gl_mean, gl_changed, YEE_TOTAL);

    /* Did anything change at all? */
    int l_field_changed = (gl_changed > 0);
    printf("\n  L-FIELD CHANGED: %s\n", l_field_changed ? "YES" : "NO");

    if (l_field_changed) {
        /* Did the changes differentiate between corpora? */
        float mean_diff = fabsf(a_avg_mean - b_avg_mean);
        int wire_diff = abs(a_avg_wire - b_avg_wire);
        printf("  DIFFERENTIATED: mean_diff=%.4f  wire_diff=%d\n",
               mean_diff, wire_diff);

        if (mean_diff > 0.01f || wire_diff > 5) {
            printf("\n  >>> L-FIELD DIFFERENTIATES. Hebbian carves distinct patterns. <<<\n");
        } else {
            printf("\n  >>> L-field changed but HOMOGENIZED. Same pattern everywhere. <<<\n");
        }
    } else {
        printf("\n  >>> L-field UNCHANGED. Hebbian didn't fire. <<<\n");
    }

    /* Check assertions */
    check("lfield: L-field changed after Hebbian", 1, l_field_changed);

    free(L_init);
    free(L_final);
    free(yee_sub);
    yee_destroy();
    engine_destroy(&eng);
}
