/*
 * sweep_tracking.c — The test the sweep needed.
 *
 * Both sweeps failed because they tested convergence. Dump data in, wait,
 * measure the corpse. N is irrelevant for digestion — every N reaches the
 * same fixed point. The engine needs a MOVING TARGET to reveal whether its
 * observation rate matches its propagation rate.
 *
 * This benchmark does:
 * 1. Ingest 10 "ground truth" sentences → stabilize → verify crystals
 * 2. Ingest 5 "contradiction" sentences that conflict with specific truths
 * 3. Tick while the engine adapts
 * 4. Ingest 5 MORE contradictions before the first wave fully resolves
 * 5. Score: did the RIGHT nodes dissolve? Did the WRONG nodes survive?
 *
 * At the right N: engine catches each contradiction within one observation
 * cycle and resolves it before the next wave arrives. Precision and recall
 * are both high.
 *
 * At wrong N:
 * - Too low: responds to noise mid-propagation, erodes correct nodes (low precision)
 * - Too high: doesn't notice contradiction fast enough, damage spreads (low recall)
 *
 * Score = (correctly dissolved / should dissolve) × (correctly survived / should survive)
 *       = recall × specificity
 *
 * Sweep this across N. If there's a cliff, it shows up here.
 *
 * Build: Include with engine sources. Run: xyzt_pc.exe tracking
 *
 * Isaac Oravec & Claude, February 28 2026
 */

#include "engine.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ═══════════════════════════════════════════════════════════
 * CORPUS DESIGN
 *
 * Ground truth: 10 sentences forming two clusters.
 * Cluster A (5 sentences): fox/dog/river theme
 * Cluster B (5 sentences): element/chemistry theme
 *
 * Contradictions wave 1 (5 sentences): directly conflict with cluster A.
 * Same words but contradictory structure (fox is NOT by the river, etc.)
 * These share identity overlap with cluster A nodes, forcing the engine
 * to choose: keep old crystal or dissolve and rewire.
 *
 * Contradictions wave 2 (5 sentences): conflict with cluster B.
 * Arrives while the engine is still processing wave 1.
 * This tests TRACKING — can it handle two simultaneous adaptation fronts?
 *
 * Expected correct behavior:
 * - Cluster A nodes dissolve (contradicted by wave 1)
 * - Cluster B nodes dissolve (contradicted by wave 2)
 * - Contradiction nodes survive and crystallize
 * - Edges between contradicted and contradiction nodes form
 *
 * Measured: which nodes dissolved, which survived, which rewired.
 * ═══════════════════════════════════════════════════════════ */

static const char *ground_A[] = {
    "the quick brown fox jumps over the lazy dog by the river",
    "a brown fox runs through the forest near the winding river",
    "the lazy dog sleeps under the oak tree beside the river bank",
    "fox and dog rest together on the grassy river bank at dawn",
    "the river flows past the oak where the fox watches the dog sleep",
};

static const char *ground_B[] = {
    "hydrogen bonds form between water molecules in liquid state",
    "carbon atoms create diamond lattice under extreme pressure",
    "oxygen reacts with iron producing rust on exposed metal surfaces",
    "nitrogen makes up most of the atmosphere above sea level",
    "helium atoms resist bonding due to full electron shell configuration",
};

static const char *contra_A[] = {
    "the fox never goes near the river or the dog anymore",
    "no brown fox has been seen in the forest or by any river",
    "the dog left the oak tree and the river bank permanently",
    "fox and dog separated and never returned to the river bank",
    "the river dried up and the oak died taking the fox den with it",
};

static const char *contra_B[] = {
    "hydrogen bonds break completely in supercritical fluid phase",
    "carbon atoms form graphene sheets not diamond under new conditions",
    "oxygen no longer reacts with iron when chromium coating applied",
    "nitrogen depleted from atmosphere by industrial capture process",
    "helium atoms form exotic bonds under extreme magnetic confinement",
};

/* Track node IDs for scoring */
typedef struct {
    int ground_a[5];
    int ground_b[5];
    int contra_a[5];
    int contra_b[5];
} TrackingIDs;

static void ingest_corpus(Engine *eng, const char **texts, int n, int *ids) {
    for (int i = 0; i < n; i++) {
        ids[i] = engine_ingest_text(eng, texts[i], texts[i]);
    }
}

typedef struct {
    /* Recall: fraction of contradicted nodes that actually dissolved */
    double recall_a;    /* cluster A contradicted by wave 1 */
    double recall_b;    /* cluster B contradicted by wave 2 */
    double recall;      /* combined */

    /* Specificity: fraction of contradiction nodes that survived */
    double spec_a;      /* wave 1 nodes survived */
    double spec_b;      /* wave 2 nodes survived */
    double spec;        /* combined */

    /* Tracking score: geometric mean of recall and specificity */
    double score;

    /* Raw counts */
    int ga_dissolved, gb_dissolved;   /* ground truth dissolved */
    int ca_survived, cb_survived;     /* contradictions survived */
    int ga_crystalized, gb_crystalized; /* ground truth that re-crystallized (bad) */
    int ca_crystalized, cb_crystalized; /* contradictions that crystallized (good) */

    /* Topology */
    int total_alive, total_crystal, total_edges;
    int32_t total_error;
    int grow_interval, prune_interval;
} TrackingScore;

static TrackingScore run_tracking(int n_adapt_cycles) {
    Engine eng;
    engine_init(&eng);
    Graph *g0 = &eng.shells[0].g;
    TrackingIDs ids;
    TrackingScore sc;
    memset(&sc, 0, sizeof(sc));

    /* ── Phase 1: Ingest ground truth, stabilize ── */
    ingest_corpus(&eng, ground_A, 5, ids.ground_a);
    ingest_corpus(&eng, ground_B, 5, ids.ground_b);

    /* Stabilize: run enough cycles for crystallization.
     * 5 SUBSTRATE_INT cycles should be enough for boredom to max valence. */
    for (int t = 0; t < (int)SUBSTRATE_INT * 5; t++)
        engine_tick(&eng);

    /* Verify ground truth crystallized */
    int ga_cryst = 0, gb_cryst = 0;
    for (int i = 0; i < 5; i++) {
        if (ids.ground_a[i] >= 0 && g0->nodes[ids.ground_a[i]].alive
            && g0->nodes[ids.ground_a[i]].valence >= 200) ga_cryst++;
        if (ids.ground_b[i] >= 0 && g0->nodes[ids.ground_b[i]].alive
            && g0->nodes[ids.ground_b[i]].valence >= 200) gb_cryst++;
    }

    /* ── Phase 2: Inject contradiction wave 1 (attacks cluster A) ── */
    ingest_corpus(&eng, contra_A, 5, ids.contra_a);

    /* Let the engine process for n_adapt_cycles SUBSTRATE_INT intervals.
     * This is where N matters: how many observations does the engine get
     * to detect and resolve the contradiction? */
    for (int t = 0; t < (int)SUBSTRATE_INT * n_adapt_cycles; t++)
        engine_tick(&eng);

    /* ── Phase 3: Inject contradiction wave 2 (attacks cluster B)
     *             WHILE wave 1 is still being processed ── */
    ingest_corpus(&eng, contra_B, 5, ids.contra_b);

    /* Let the engine process both waves */
    for (int t = 0; t < (int)SUBSTRATE_INT * n_adapt_cycles; t++)
        engine_tick(&eng);

    /* ── Phase 4: Score ── */

    /* Ground A: should have dissolved (contradicted by wave 1) */
    sc.ga_dissolved = 0;
    sc.ga_crystalized = 0;
    for (int i = 0; i < 5; i++) {
        int id = ids.ground_a[i];
        if (id < 0) continue;
        Node *n = &g0->nodes[id];
        if (!n->alive || n->valence < 100) sc.ga_dissolved++;
        if (n->alive && n->valence >= 200) sc.ga_crystalized++;
    }

    /* Ground B: should have dissolved (contradicted by wave 2) */
    sc.gb_dissolved = 0;
    sc.gb_crystalized = 0;
    for (int i = 0; i < 5; i++) {
        int id = ids.ground_b[i];
        if (id < 0) continue;
        Node *n = &g0->nodes[id];
        if (!n->alive || n->valence < 100) sc.gb_dissolved++;
        if (n->alive && n->valence >= 200) sc.gb_crystalized++;
    }

    /* Contra A: should have survived and crystallized */
    sc.ca_survived = 0;
    sc.ca_crystalized = 0;
    for (int i = 0; i < 5; i++) {
        int id = ids.contra_a[i];
        if (id < 0) continue;
        Node *n = &g0->nodes[id];
        if (n->alive && n->valence > 50) sc.ca_survived++;
        if (n->alive && n->valence >= 200) sc.ca_crystalized++;
    }

    /* Contra B: should have survived and crystallized */
    sc.cb_survived = 0;
    sc.cb_crystalized = 0;
    for (int i = 0; i < 5; i++) {
        int id = ids.contra_b[i];
        if (id < 0) continue;
        Node *n = &g0->nodes[id];
        if (n->alive && n->valence > 50) sc.cb_survived++;
        if (n->alive && n->valence >= 200) sc.cb_crystalized++;
    }

    /* Compute scores */
    sc.recall_a = sc.ga_dissolved / 5.0;
    sc.recall_b = sc.gb_dissolved / 5.0;
    sc.recall = (sc.ga_dissolved + sc.gb_dissolved) / 10.0;

    sc.spec_a = sc.ca_survived / 5.0;
    sc.spec_b = sc.cb_survived / 5.0;
    sc.spec = (sc.ca_survived + sc.cb_survived) / 10.0;

    sc.score = sqrt(sc.recall * sc.spec);  /* geometric mean */

    /* Topology snapshot */
    sc.total_alive = 0;
    sc.total_crystal = 0;
    sc.total_edges = 0;
    sc.total_error = 0;
    for (int i = 0; i < g0->n_nodes; i++) {
        if (g0->nodes[i].alive && !g0->nodes[i].layer_zero) {
            sc.total_alive++;
            if (g0->nodes[i].valence >= 200) sc.total_crystal++;
        }
    }
    for (int e = 0; e < g0->n_edges; e++)
        if (g0->edges[e].weight > 0) sc.total_edges++;

    for (int c = 0; c < 5; c++)
        sc.total_error += abs(eng.onetwo.feedback[7]);

    sc.grow_interval = g0->grow_interval;
    sc.prune_interval = g0->prune_interval;

    engine_destroy(&eng);
    return sc;
}

void run_tracking_sweep(void) {
    printf("=== TRACKING SWEEP: adaptation quality vs SUBSTRATE_INT ===\n");
    printf("N\tscore\trecall\tspec\tga_dis\tgb_dis\tca_sur\tcb_sur\t"
           "ga_cry\tgb_cry\tca_cry\tcb_cry\talive\tcrystal\tedges\t"
           "grow_int\tprune_int\n");

    /* The test gives each N the same number of adaptation CYCLES (not ticks).
     * 3 cycles per wave = 6 total. That's enough for frustration to fire
     * and boredom to potentially re-crystallize. Short enough that a wrong N
     * can't compensate by just running longer. */
    int adapt_cycles = 3;

    /* Dense near 137, sparse at edges */
    int sweep_values[] = {
        80, 90, 100, 110, 117, 120, 125, 127,
        130, 131, 132, 133, 134, 135, 136, 137, 138, 139,
        140, 141, 142, 143, 145, 147, 150, 155, 160, 170, 180, 200
    };
    int n_values = sizeof(sweep_values) / sizeof(sweep_values[0]);

    for (int v = 0; v < n_values; v++) {
        /* NOTE: This function is compiled at a FIXED SUBSTRATE_INT.
         * To sweep, this file must be recompiled with -DSUBSTRATE_INT=N
         * for each value. The Python sweep script handles this.
         *
         * When called directly (xyzt_pc.exe tracking), it runs at
         * the compiled SUBSTRATE_INT and prints one row. */
        (void)sweep_values;
        (void)n_values;

        TrackingScore sc = run_tracking(adapt_cycles);

        printf("%u\t%.3f\t%.3f\t%.3f\t%d\t%d\t%d\t%d\t"
               "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
               SUBSTRATE_INT,
               sc.score, sc.recall, sc.spec,
               sc.ga_dissolved, sc.gb_dissolved,
               sc.ca_survived, sc.cb_survived,
               sc.ga_crystalized, sc.gb_crystalized,
               sc.ca_crystalized, sc.cb_crystalized,
               sc.total_alive, sc.total_crystal, sc.total_edges,
               sc.grow_interval, sc.prune_interval);

        break;  /* Only one run per compilation. Sweep script handles iteration. */
    }
}
