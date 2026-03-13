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
#include "sense.h"
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
    "brown fox and lazy dog share the riverbank meadow every morning",
    "the oak tree shades the sleeping dog while the fox hunts nearby",
    "fog rises from the river as the fox trots past the old oak",
    "the dog wakes and follows the fox along the winding river path",
    "beneath the oak the fox and dog drink from the cold river water",
};
#define CORPUS_SIZE 10

static const char *ground_B[] = {
    "hydrogen bonds form between water molecules in liquid state",
    "carbon atoms create diamond lattice under extreme pressure",
    "oxygen reacts with iron producing rust on exposed metal surfaces",
    "nitrogen makes up most of the atmosphere above sea level",
    "helium atoms resist bonding due to full electron shell configuration",
    "silicon crystals conduct electricity when doped with phosphorus atoms",
    "chlorine gas dissolves in water producing hydrochloric acid solution",
    "sodium metal reacts violently with water releasing hydrogen gas",
    "copper conducts heat and electricity through metallic bonding structure",
    "argon remains inert even under extreme temperature and pressure",
};

static const char *contra_A[] = {
    "the fox never goes near the river or the dog anymore",
    "no brown fox has been seen in the forest or by any river",
    "the dog no longer stays at the oak tree or the river bank",
    "fox and dog separated and never returned to the river bank",
    "the river does not flow anymore and the oak is no longer standing",
    "the riverbank meadow is empty with no fox or dog present",
    "no oak tree stands here and no dog has ever slept beneath it",
    "the river has not produced fog since the fox stopped coming",
    "the dog does not follow the fox because neither visits the river",
    "no fox or dog drinks from the river beneath any oak tree now",
};

/* Semantic contradictions: oppose ground_A meaning, zero negation keywords.
 * text_has_negation must return 0 for all of these. */
static const char *semantic_contra[] = {
    "the fox fled from the river and abandoned the lazy dog forever",
    "a brown fox vanished from the forest and the river dried up completely",
    "the dog attacked the oak tree and destroyed the river bank",
    "fox and dog fought viciously and abandoned the grassy river bank at dawn",
    "the river froze solid and the oak collapsed where the fox once watched",
    "the meadow burned and the fox escaped while the dog ran away",
    "the oak fell into the river and the fox disappeared from the bank",
    "fog vanished as the river dried and the fox abandoned the area",
    "the dog chased the fox away from the river path permanently",
    "the river froze and the oak rotted leaving the fox and dog homeless",
};

static const char *contra_B[] = {
    "hydrogen bonds do not form between water molecules in supercritical phase",
    "carbon atoms form graphene sheets not diamond under new conditions",
    "oxygen no longer reacts with iron when chromium coating applied",
    "nitrogen is no longer the majority of the atmosphere after capture",
    "helium atoms form exotic bonds under extreme magnetic confinement",
    "silicon does not conduct when impurities are removed completely",
    "chlorine no longer dissolves in water at extreme alkaline conditions",
    "sodium does not react with water when coated in mineral oil barrier",
    "copper loses conductivity when transformed into cupric oxide compound",
    "argon is no longer inert when subjected to extreme laser ionization",
};

/* Track node IDs for scoring */
typedef struct {
    int ground_a[CORPUS_SIZE];
    int ground_b[CORPUS_SIZE];
    int contra_a[CORPUS_SIZE];
    int contra_b[CORPUS_SIZE];
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

    /* SPRT accumulation — parent inner T */
    int32_t error_accum;
    int heartbeats;
    int drive;
    int total_cleaved;
    int32_t peak_ge;       /* peak graph_error during adaptation */
    int32_t peak_accum;    /* peak |error_accum| during adaptation */
    int32_t zone_a_peak_accum;  /* peak |error_accum| for ground truth A */
    int32_t zone_b_peak_accum;  /* peak |error_accum| for ground truth B */
    int32_t zone_ratio;         /* zone_a_peak / max(zone_b_peak, 1) */
} TrackingScore;

static TrackingScore run_tracking(int n_adapt_cycles) {
    Engine eng;
    engine_init(&eng);
    Graph *g0 = &eng.shells[0].g;
    TrackingIDs ids;
    TrackingScore sc;
    memset(&sc, 0, sizeof(sc));

    /* ── Phase 1: Ingest ground truth, stabilize ── */
    ingest_corpus(&eng, ground_A, CORPUS_SIZE, ids.ground_a);
    ingest_corpus(&eng, ground_B, CORPUS_SIZE, ids.ground_b);

    /* Stabilize: run enough cycles for crystallization.
     * 5 SUBSTRATE_INT cycles should be enough for boredom to max valence. */
    for (int t = 0; t < (int)SUBSTRATE_INT * 5; t++)
        engine_tick(&eng);

    /* Verify ground truth crystallized */
    int ga_cryst = 0, gb_cryst = 0;
    for (int i = 0; i < CORPUS_SIZE; i++) {
        if (ids.ground_a[i] >= 0 && g0->nodes[ids.ground_a[i]].alive
            && g0->nodes[ids.ground_a[i]].valence >= 200) ga_cryst++;
        if (ids.ground_b[i] >= 0 && g0->nodes[ids.ground_b[i]].alive
            && g0->nodes[ids.ground_b[i]].valence >= 200) gb_cryst++;
    }

    /* ── Phase 2: Inject contradiction wave 1 (attacks cluster A) ── */
    ingest_corpus(&eng, contra_A, CORPUS_SIZE, ids.contra_a);

    /* Diagnostic: check negation flags, wiring between ground and contra */
    {
        int n_negated_a = 0, n_negated_b = 0;
        for (int i = 0; i < CORPUS_SIZE; i++) {
            if (ids.contra_a[i] >= 0 && g0->nodes[ids.contra_a[i]].has_negation) n_negated_a++;
            if (ids.ground_a[i] >= 0) printf("  [diag] gA[%d] id=%d neg=%d val=%d valence=%d\n",
                i, ids.ground_a[i], g0->nodes[ids.ground_a[i]].has_negation,
                g0->nodes[ids.ground_a[i]].val, g0->nodes[ids.ground_a[i]].valence);
        }
        for (int i = 0; i < CORPUS_SIZE; i++) {
            if (ids.contra_a[i] >= 0) printf("  [diag] cA[%d] id=%d neg=%d val=%d valence=%d\n",
                i, ids.contra_a[i], g0->nodes[ids.contra_a[i]].has_negation,
                g0->nodes[ids.contra_a[i]].val, g0->nodes[ids.contra_a[i]].valence);
        }
        /* Count edges between ground_A and contra_A specifically */
        int ga_ca_edges = 0, ga_ca_inverted = 0, total_inverted = 0;
        for (int e = 0; e < g0->n_edges; e++) {
            Edge *ed = &g0->edges[e];
            if (ed->invert_a || ed->invert_b) total_inverted++;
            /* Check if this edge connects a gA node to a cA node */
            for (int gi = 0; gi < CORPUS_SIZE; gi++) {
                for (int ci = 0; ci < CORPUS_SIZE; ci++) {
                    if ((ed->src_a == ids.ground_a[gi] && ed->src_b == ids.contra_a[ci]) ||
                        (ed->src_a == ids.contra_a[ci] && ed->src_b == ids.ground_a[gi]) ||
                        (ed->src_a == ids.ground_a[gi] && ed->dst == ids.contra_a[ci]) ||
                        (ed->src_a == ids.contra_a[ci] && ed->dst == ids.ground_a[gi])) {
                        ga_ca_edges++;
                        if (ed->invert_a || ed->invert_b) ga_ca_inverted++;
                    }
                }
            }
        }
        /* Check mutual containment between gA[0] and cA[0] */
        int mc01 = -1;
        if (ids.ground_a[0] >= 0 && ids.contra_a[0] >= 0)
            mc01 = bs_mutual_contain(&g0->nodes[ids.ground_a[0]].identity,
                                     &g0->nodes[ids.contra_a[0]].identity);
        printf("  [diag] contra_A: %d/%d negated, %d inverted edges, %d total edges\n",
               n_negated_a, CORPUS_SIZE, total_inverted, g0->n_edges);
        printf("  [diag] gA↔cA: %d edges, %d inverted, mc(gA0,cA0)=%d\n",
               ga_ca_edges, ga_ca_inverted, mc01);
    }

    /* Let the engine process for n_adapt_cycles SUBSTRATE_INT intervals.
     * This is where N matters: how many observations does the engine get
     * to detect and resolve the contradiction? */
    int32_t peak_ge = 0, peak_accum = 0;
    /* Per-zone SPRT accumulators — track differential resolution rates */
    int32_t zone_a_accum = 0, zone_b_accum = 0;
    int32_t zone_a_prev = 0, zone_b_prev = 0;
    int32_t zone_a_peak = 0, zone_b_peak = 0;

    for (int t = 0; t < (int)SUBSTRATE_INT * n_adapt_cycles; t++) {
        engine_tick(&eng);
        if (eng.graph_error > peak_ge) peak_ge = eng.graph_error;
        int32_t aa = g0->error_accum < 0 ? -g0->error_accum : g0->error_accum;
        if (aa > peak_accum) peak_accum = aa;

        if (t > 0 && t % (int)SUBSTRATE_INT == 0) {
            int ga_alive = 0, gb_alive = 0;
            int ga_incoh = graph_zone_coherence(g0, ids.ground_a, CORPUS_SIZE, &ga_alive);
            int gb_incoh = graph_zone_coherence(g0, ids.ground_b, CORPUS_SIZE, &gb_alive);

            int32_t ga_err = (ga_alive > 0) ? (ga_incoh * 100 / ga_alive) : 0;
            int32_t ga_delta = ga_err - zone_a_prev;
            zone_a_accum += ga_delta;
            zone_a_accum = zone_a_accum * 7 / 8;
            zone_a_prev = ga_err;
            int32_t abs_a = zone_a_accum < 0 ? -zone_a_accum : zone_a_accum;
            if (abs_a > zone_a_peak) zone_a_peak = abs_a;

            int32_t gb_err = (gb_alive > 0) ? (gb_incoh * 100 / gb_alive) : 0;
            int32_t gb_delta = gb_err - zone_b_prev;
            zone_b_accum += gb_delta;
            zone_b_accum = zone_b_accum * 7 / 8;
            zone_b_prev = gb_err;
            int32_t abs_b = zone_b_accum < 0 ? -zone_b_accum : zone_b_accum;
            if (abs_b > zone_b_peak) zone_b_peak = abs_b;
        }
    }

    /* ── Phase 3: Inject contradiction wave 2 (attacks cluster B)
     *             WHILE wave 1 is still being processed ── */
    ingest_corpus(&eng, contra_B, CORPUS_SIZE, ids.contra_b);

    /* Let the engine process both waves */
    for (int t = 0; t < (int)SUBSTRATE_INT * n_adapt_cycles; t++) {
        engine_tick(&eng);
        if (eng.graph_error > peak_ge) peak_ge = eng.graph_error;
        int32_t aa = g0->error_accum < 0 ? -g0->error_accum : g0->error_accum;
        if (aa > peak_accum) peak_accum = aa;

        if (t > 0 && t % (int)SUBSTRATE_INT == 0) {
            int ga_alive = 0, gb_alive = 0;
            int ga_incoh = graph_zone_coherence(g0, ids.ground_a, CORPUS_SIZE, &ga_alive);
            int gb_incoh = graph_zone_coherence(g0, ids.ground_b, CORPUS_SIZE, &gb_alive);

            int32_t ga_err = (ga_alive > 0) ? (ga_incoh * 100 / ga_alive) : 0;
            int32_t ga_delta = ga_err - zone_a_prev;
            zone_a_accum += ga_delta;
            zone_a_accum = zone_a_accum * 7 / 8;
            zone_a_prev = ga_err;
            int32_t abs_a = zone_a_accum < 0 ? -zone_a_accum : zone_a_accum;
            if (abs_a > zone_a_peak) zone_a_peak = abs_a;

            int32_t gb_err = (gb_alive > 0) ? (gb_incoh * 100 / gb_alive) : 0;
            int32_t gb_delta = gb_err - zone_b_prev;
            zone_b_accum += gb_delta;
            zone_b_accum = zone_b_accum * 7 / 8;
            zone_b_prev = gb_err;
            int32_t abs_b = zone_b_accum < 0 ? -zone_b_accum : zone_b_accum;
            if (abs_b > zone_b_peak) zone_b_peak = abs_b;
        }
    }

    /* ── Phase 4: Score ── */

    /* Ground A: should have dissolved (contradicted by wave 1) */
    sc.ga_dissolved = 0;
    sc.ga_crystalized = 0;
    for (int i = 0; i < CORPUS_SIZE; i++) {
        int id = ids.ground_a[i];
        if (id < 0) continue;
        Node *n = &g0->nodes[id];
        if (!n->alive || n->valence < 100) sc.ga_dissolved++;
        if (n->alive && n->valence >= 200) sc.ga_crystalized++;
    }

    /* Ground B: should have dissolved (contradicted by wave 2) */
    sc.gb_dissolved = 0;
    sc.gb_crystalized = 0;
    for (int i = 0; i < CORPUS_SIZE; i++) {
        int id = ids.ground_b[i];
        if (id < 0) continue;
        Node *n = &g0->nodes[id];
        if (!n->alive || n->valence < 100) sc.gb_dissolved++;
        if (n->alive && n->valence >= 200) sc.gb_crystalized++;
    }

    /* Contra A: should have survived and crystallized */
    sc.ca_survived = 0;
    sc.ca_crystalized = 0;
    for (int i = 0; i < CORPUS_SIZE; i++) {
        int id = ids.contra_a[i];
        if (id < 0) continue;
        Node *n = &g0->nodes[id];
        if (n->alive && n->valence > 50) sc.ca_survived++;
        if (n->alive && n->valence >= 200) sc.ca_crystalized++;
    }

    /* Contra B: should have survived and crystallized */
    sc.cb_survived = 0;
    sc.cb_crystalized = 0;
    for (int i = 0; i < CORPUS_SIZE; i++) {
        int id = ids.contra_b[i];
        if (id < 0) continue;
        Node *n = &g0->nodes[id];
        if (n->alive && n->valence > 50) sc.cb_survived++;
        if (n->alive && n->valence >= 200) sc.cb_crystalized++;
    }

    /* Compute scores */
    sc.recall_a = sc.ga_dissolved / (double)CORPUS_SIZE;
    sc.recall_b = sc.gb_dissolved / (double)CORPUS_SIZE;
    sc.recall = (sc.ga_dissolved + sc.gb_dissolved) / (2.0 * CORPUS_SIZE);

    sc.spec_a = sc.ca_survived / (double)CORPUS_SIZE;
    sc.spec_b = sc.cb_survived / (double)CORPUS_SIZE;
    sc.spec = (sc.ca_survived + sc.cb_survived) / (2.0 * CORPUS_SIZE);

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

    /* SPRT accumulation state */
    sc.error_accum = g0->error_accum;
    sc.heartbeats = g0->local_heartbeat;
    sc.drive = g0->drive;
    sc.total_cleaved = eng.total_cleaved;
    sc.peak_ge = peak_ge;
    sc.peak_accum = peak_accum;
    sc.zone_a_peak_accum = zone_a_peak;
    sc.zone_b_peak_accum = zone_b_peak;
    sc.zone_ratio = zone_a_peak / (zone_b_peak > 0 ? zone_b_peak : 1);

    engine_destroy(&eng);
    return sc;
}

void run_tracking_sweep(void) {
    printf("=== TRACKING SWEEP: adaptation quality vs SUBSTRATE_INT ===\n");
    printf("N\tscore\trecall\tspec\tga_dis\tgb_dis\tca_sur\tcb_sur\t"
           "ga_cry\tgb_cry\tca_cry\tcb_cry\talive\tcrystal\tedges\t"
           "grow_int\tprune_int\terr_acc\theartbeats\tdrive\tcleaved\t"
           "peak_ge\tpeak_acc\tza_pk\tzb_pk\tz_ratio\n");

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
               "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t"
               "%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\n",
               SUBSTRATE_INT,
               sc.score, sc.recall, sc.spec,
               sc.ga_dissolved, sc.gb_dissolved,
               sc.ca_survived, sc.cb_survived,
               sc.ga_crystalized, sc.gb_crystalized,
               sc.ca_crystalized, sc.cb_crystalized,
               sc.total_alive, sc.total_crystal, sc.total_edges,
               sc.grow_interval, sc.prune_interval,
               sc.error_accum, sc.heartbeats, sc.drive, sc.total_cleaved,
               sc.peak_ge, sc.peak_accum,
               sc.zone_a_peak_accum, sc.zone_b_peak_accum, sc.zone_ratio);

        break;  /* Only one run per compilation. Sweep script handles iteration. */
    }
}

/* ═══════════════════════════════════════════════════════════════
 * SENSE DIAGNOSTIC: compare feature signatures for
 * normal-death vs contradiction-death scenarios.
 *
 * Does sense.c see something different when a node dies from
 * contradiction vs when it dies from normal pruning?
 * ═══════════════════════════════════════════════════════════════ */

/* Build state_buf the same way engine_tick S10 does (including passes 1-4).
 * Also fills sense_region_t array for windowed extraction. */
static int build_state_buf(Engine *eng, uint8_t *buf,
                           sense_region_t *regions, int *n_regions) {
    int pos = 0;
    *n_regions = 0;

    /* Pass 1: edge weights */
    int pass1_start = pos;
    for (int s = 0; s < eng->n_shells; s++) {
        Graph *g = &eng->shells[s].g;
        for (int e = 0; e < g->n_edges && pos < 400; e++)
            buf[pos++] = g->edges[e].weight;
    }
    if (pos > pass1_start) {
        regions[*n_regions].offset = pass1_start;
        regions[*n_regions].length = pos - pass1_start;
        regions[*n_regions].pass_id = 1;
        (*n_regions)++;
    }

    /* Pass 2: node state — 4 bytes per node */
    int pass2_start = pos;
    for (int s = 0; s < eng->n_shells; s++) {
        Graph *g = &eng->shells[s].g;
        for (int i = 0; i < g->n_nodes && pos < 500; i++) {
            if (g->nodes[i].alive && !g->nodes[i].layer_zero) {
                buf[pos++] = g->nodes[i].valence;
                if (pos < 500)
                    buf[pos++] = (uint8_t)crystal_strength(&g->nodes[i]);
                if (pos < 500)
                    buf[pos++] = g->nodes[i].val > 0 ? 255
                               : g->nodes[i].val < 0 ? 0 : 128;
                if (pos < 500) {
                    int32_t ie = g->nodes[i].I_energy;
                    buf[pos++] = (uint8_t)(ie < 0 ? 0 : (ie >> 23) > 255 ? 255 : (ie >> 23));
                }
            }
        }
    }
    if (pos > pass2_start) {
        regions[*n_regions].offset = pass2_start;
        regions[*n_regions].length = pos - pass2_start;
        regions[*n_regions].pass_id = 2;
        (*n_regions)++;
    }

    /* Pass 3: topology summary — too small for sense, not added to regions */
    for (int s = 0; s < eng->n_shells; s++) {
        Graph *g = &eng->shells[s].g;
        int n_relay = 0, n_collision = 0, n_crystal = 0;
        for (int i = 0; i < g->n_nodes; i++) {
            if (!g->nodes[i].alive || g->nodes[i].layer_zero) continue;
            if (g->nodes[i].valence >= 200) n_crystal++;
            else if (g->nodes[i].n_incoming >= 2) n_collision++;
            else n_relay++;
        }
        if (pos < 506) {
            buf[pos++] = (uint8_t)(n_relay > 255 ? 255 : n_relay);
            buf[pos++] = (uint8_t)(n_collision > 255 ? 255 : n_collision);
            buf[pos++] = (uint8_t)(n_crystal > 255 ? 255 : n_crystal);
        }
    }

    /* Pass 4: edge inversion byte — ALL edges */
    int pass4_start = pos;
    for (int s = 0; s < eng->n_shells; s++) {
        Graph *g = &eng->shells[s].g;
        for (int e = 0; e < g->n_edges && pos < 800; e++) {
            if (g->edges[e].weight == 0) continue;
            int inv = (g->edges[e].invert_a || g->edges[e].invert_b) ? 1 : 0;
            buf[pos++] = (uint8_t)((g->edges[e].weight >> 1) | (inv << 7));
        }
    }
    if (pos > pass4_start) {
        regions[*n_regions].offset = pass4_start;
        regions[*n_regions].length = pos - pass4_start;
        regions[*n_regions].pass_id = 4;
        (*n_regions)++;
    }

    return pos;
}

/* Dump sense features: name, type, val, valence, hit_count */
static void dump_sense(const char *label, Engine *eng, sense_result_t *sr) {
    Graph *g = &eng->shells[0].g;
    printf("  [%s] %d sense features:\n", label, sr->n_features);
    for (int i = 0; i < sr->n_features; i++) {
        int id = sr->node_ids[i];
        if (id < 0 || id >= g->n_nodes) continue;
        Node *n = &g->nodes[id];
        printf("    %-8s val=%-8d valence=%-3d hits=%d\n",
               n->name, n->val, n->valence, n->hit_count);
    }
}

/* Count features by type prefix */
typedef struct {
    int active, regularity, duty, correlation, asymmetry, burst, period;
    int total;
    /* Channel-level detail for active features */
    int active_ch[8];
    int duty_ch[8];
    int reg_ch[8];
} SenseProfile;

static SenseProfile profile_sense(Engine *eng, sense_result_t *sr) {
    SenseProfile p;
    memset(&p, 0, sizeof(p));
    Graph *g = &eng->shells[0].g;
    p.total = sr->n_features;
    for (int i = 0; i < sr->n_features; i++) {
        int id = sr->node_ids[i];
        if (id < 0 || id >= g->n_nodes) continue;
        const char *nm = g->nodes[id].name;
        if (nm[0] != 's' || nm[1] != ':') continue;
        char type = nm[2];
        int ch = (nm[3] >= '0' && nm[3] <= '7') ? nm[3] - '0' : -1;
        switch (type) {
            case 'A': p.active++; if (ch >= 0) p.active_ch[ch] = 1; break;
            case 'R': p.regularity++; if (ch >= 0) p.reg_ch[ch] = 1; break;
            case 'D': p.duty++; if (ch >= 0) p.duty_ch[ch] = 1; break;
            case 'C': p.correlation++; break;
            case 'S': p.asymmetry++; break;
            case 'B': p.burst++; break;
            case 'P': p.period++; break;
        }
    }
    return p;
}

static void print_profile(const char *label, SenseProfile *p) {
    printf("  [%s] total=%d  A=%d R=%d D=%d C=%d S=%d B=%d P=%d\n",
           label, p->total, p->active, p->regularity, p->duty,
           p->correlation, p->asymmetry, p->burst, p->period);
    printf("    active channels: ");
    for (int c = 0; c < 8; c++) if (p->active_ch[c]) printf("%d ", c);
    printf("\n    duty channels:   ");
    for (int c = 0; c < 8; c++) if (p->duty_ch[c]) printf("%d ", c);
    printf("\n    reg channels:    ");
    for (int c = 0; c < 8; c++) if (p->reg_ch[c]) printf("%d ", c);
    printf("\n");
}

/* Count inverted edges around a set of node IDs */
static void edge_topology(Graph *g, int *ids, int n_ids, const char *label) {
    int edges_to = 0, inverted_to = 0, weight_sum = 0;
    for (int e = 0; e < g->n_edges; e++) {
        Edge *ed = &g->edges[e];
        for (int k = 0; k < n_ids; k++) {
            if ((int)ed->dst == ids[k] || (int)ed->src_a == ids[k] || (int)ed->src_b == ids[k]) {
                edges_to++;
                weight_sum += ed->weight;
                if (ed->invert_a || ed->invert_b) inverted_to++;
                break;
            }
        }
    }
    printf("  [%s] edges touching: %d, inverted: %d, mean_weight: %d\n",
           label, edges_to, inverted_to,
           edges_to > 0 ? weight_sum / edges_to : 0);
}

void run_sense_diagnostic(void) {
    printf("\n=== SENSE DIAGNOSTIC: normal-death vs contradiction-death ===\n\n");

    /* ── SCENARIO A: Normal lifecycle (no contradictions) ── */
    printf("── SCENARIO A: Normal lifecycle ──\n");
    {
        Engine eng;
        engine_init(&eng);
        Graph *g0 = &eng.shells[0].g;

        /* Ingest only cluster A ground truth */
        int ids[CORPUS_SIZE];
        for (int i = 0; i < CORPUS_SIZE; i++)
            ids[i] = engine_ingest_text(&eng, ground_A[i], ground_A[i]);

        /* Stabilize */
        for (int t = 0; t < (int)SUBSTRATE_INT * 5; t++)
            engine_tick(&eng);

        /* Snapshot sense BEFORE any deaths */
        uint8_t sbuf[1024];
        sense_region_t regions[SENSE_MAX_REGIONS];
        int n_regions = 0;
        (void)build_state_buf(&eng, sbuf, regions, &n_regions);
        sense_result_t sr_before;
        sense_pass_windowed(&eng, sbuf, regions, n_regions, &sr_before);
        SenseProfile p_before = profile_sense(&eng, &sr_before);

        printf("  After stabilization (%d ticks):\n", (int)SUBSTRATE_INT * 5);
        print_profile("A-stable", &p_before);
        dump_sense("A-stable", &eng, &sr_before);

        /* Node state */
        printf("  Node state:\n");
        for (int i = 0; i < CORPUS_SIZE; i++) {
            if (ids[i] < 0) continue;
            Node *n = &g0->nodes[ids[i]];
            printf("    [%d] val=%-8d valence=%-3d coherent=%d contradicted=%d alive=%d\n",
                   ids[i], n->val, n->valence, n->coherent, n->contradicted, n->alive);
        }
        edge_topology(g0, ids, 5, "A-nodes");

        /* Run more ticks — some nodes may naturally decay/prune */
        for (int t = 0; t < (int)SUBSTRATE_INT * 3; t++)
            engine_tick(&eng);

        /* Snapshot sense AFTER natural evolution */
        (void)build_state_buf(&eng, sbuf, regions, &n_regions);
        sense_result_t sr_after;
        sense_pass_windowed(&eng, sbuf, regions, n_regions, &sr_after);
        SenseProfile p_after = profile_sense(&eng, &sr_after);

        printf("  After +%d more ticks (natural evolution):\n", (int)SUBSTRATE_INT * 3);
        print_profile("A-evolved", &p_after);
        dump_sense("A-evolved", &eng, &sr_after);

        printf("  Node state:\n");
        int dead_a = 0;
        for (int i = 0; i < CORPUS_SIZE; i++) {
            if (ids[i] < 0) continue;
            Node *n = &g0->nodes[ids[i]];
            printf("    [%d] val=%-8d valence=%-3d coherent=%d contradicted=%d alive=%d\n",
                   ids[i], n->val, n->valence, n->coherent, n->contradicted, n->alive);
            if (!n->alive || n->valence < 100) dead_a++;
        }
        edge_topology(g0, ids, 5, "A-nodes");
        printf("  Dead/dissolved nodes: %d/5\n", dead_a);

        /* Polarity prediction: control group (should all predict 0) */
        printf("\n  ── POLARITY PREDICTION (control) ──\n");
        for (int i = 0; i < CORPUS_SIZE; i++)
            if (ids[i] >= 0)
                engine_predict_polarity(&eng, ids[i], ground_A[i]);
        engine_polarity_summary(&eng);

        engine_destroy(&eng);
    }

    printf("\n── SCENARIO B: Contradiction-driven death ──\n");
    {
        Engine eng;
        engine_init(&eng);
        Graph *g0 = &eng.shells[0].g;

        /* Ingest cluster A ground truth */
        int gt_ids[5];
        for (int i = 0; i < CORPUS_SIZE; i++)
            gt_ids[i] = engine_ingest_text(&eng, ground_A[i], ground_A[i]);

        /* Stabilize */
        for (int t = 0; t < (int)SUBSTRATE_INT * 5; t++)
            engine_tick(&eng);

        /* Snapshot sense BEFORE contradiction */
        uint8_t sbuf[1024];
        sense_region_t regions[SENSE_MAX_REGIONS];
        int n_regions = 0;
        (void)build_state_buf(&eng, sbuf, regions, &n_regions);
        sense_result_t sr_before;
        sense_pass_windowed(&eng, sbuf, regions, n_regions, &sr_before);
        SenseProfile p_before = profile_sense(&eng, &sr_before);

        printf("  After stabilization (%d ticks):\n", (int)SUBSTRATE_INT * 5);
        print_profile("B-stable", &p_before);

        printf("  Node state before contradiction:\n");
        for (int i = 0; i < CORPUS_SIZE; i++) {
            if (gt_ids[i] < 0) continue;
            Node *n = &g0->nodes[gt_ids[i]];
            printf("    [%d] val=%-8d valence=%-3d coherent=%d contradicted=%d alive=%d\n",
                   gt_ids[i], n->val, n->valence, n->coherent, n->contradicted, n->alive);
        }

        /* Inject contradictions */
        int ct_ids[5];
        for (int i = 0; i < CORPUS_SIZE; i++)
            ct_ids[i] = engine_ingest_text(&eng, contra_A[i], contra_A[i]);

        /* Snapshot sense IMMEDIATELY after contradiction injection */
        (void)build_state_buf(&eng, sbuf, regions, &n_regions);
        sense_result_t sr_injected;
        sense_pass_windowed(&eng, sbuf, regions, n_regions, &sr_injected);
        SenseProfile p_injected = profile_sense(&eng, &sr_injected);

        printf("  Immediately after contradiction injection:\n");
        print_profile("B-injected", &p_injected);
        dump_sense("B-injected", &eng, &sr_injected);

        printf("  Ground truth state (post-injection, pre-adaptation):\n");
        for (int i = 0; i < CORPUS_SIZE; i++) {
            if (gt_ids[i] < 0) continue;
            Node *n = &g0->nodes[gt_ids[i]];
            printf("    [%d] val=%-8d valence=%-3d coherent=%d contradicted=%d I_energy=%d\n",
                   gt_ids[i], n->val, n->valence, n->coherent, n->contradicted, n->I_energy);
        }
        edge_topology(g0, gt_ids, 5, "B-ground");
        edge_topology(g0, ct_ids, 5, "B-contra");

        /* Run adaptation */
        for (int t = 0; t < (int)SUBSTRATE_INT * 3; t++)
            engine_tick(&eng);

        /* Snapshot sense AFTER adaptation (deaths should have occurred) */
        (void)build_state_buf(&eng, sbuf, regions, &n_regions);
        sense_result_t sr_after;
        sense_pass_windowed(&eng, sbuf, regions, n_regions, &sr_after);
        SenseProfile p_after = profile_sense(&eng, &sr_after);

        printf("  After adaptation (%d ticks):\n", (int)SUBSTRATE_INT * 3);
        print_profile("B-adapted", &p_after);
        dump_sense("B-adapted", &eng, &sr_after);

        printf("  Ground truth state (post-adaptation):\n");
        int dead_b = 0;
        for (int i = 0; i < CORPUS_SIZE; i++) {
            if (gt_ids[i] < 0) continue;
            Node *n = &g0->nodes[gt_ids[i]];
            printf("    [%d] val=%-8d valence=%-3d coherent=%d contradicted=%d alive=%d I_energy=%d\n",
                   gt_ids[i], n->val, n->valence, n->coherent, n->contradicted, n->alive, n->I_energy);
            if (!n->alive || n->valence < 100) dead_b++;
        }
        printf("  Contradiction node state:\n");
        for (int i = 0; i < CORPUS_SIZE; i++) {
            if (ct_ids[i] < 0) continue;
            Node *n = &g0->nodes[ct_ids[i]];
            printf("    [%d] val=%-8d valence=%-3d coherent=%d has_neg=%d alive=%d I_energy=%d\n",
                   ct_ids[i], n->val, n->valence, n->coherent, n->has_negation, n->alive, n->I_energy);
        }
        edge_topology(g0, gt_ids, 5, "B-ground");
        edge_topology(g0, ct_ids, 5, "B-contra");
        printf("  Dead/dissolved ground truth: %d/5\n", dead_b);

        /* Polarity prediction: ground truth (should predict 0 — normal) */
        printf("\n  ── POLARITY PREDICTION (ground truth) ──\n");
        for (int i = 0; i < CORPUS_SIZE; i++)
            if (gt_ids[i] >= 0)
                engine_predict_polarity(&eng, gt_ids[i], ground_A[i]);

        /* Polarity prediction: contradictions (should predict 1 — invert) */
        printf("  ── POLARITY PREDICTION (contradictions) ──\n");
        for (int i = 0; i < CORPUS_SIZE; i++)
            if (ct_ids[i] >= 0)
                engine_predict_polarity(&eng, ct_ids[i], contra_A[i]);
        engine_polarity_summary(&eng);

        engine_destroy(&eng);
    }

    /* ── SCENARIO C: Mixed workload (interleaved normal + contradiction) ── */
    printf("\n── SCENARIO C: Mixed workload stress test ──\n");
    {
        Engine eng;
        engine_init(&eng);
        Graph *g0 = &eng.shells[0].g;

        /* Phase 1: ingest 10 normal sentences from both corpora */
        int normal_ids[10];
        printf("  Phase 1: ingesting 10 normal sentences...\n");
        for (int i = 0; i < CORPUS_SIZE; i++) {
            normal_ids[i] = engine_ingest_text(&eng, ground_A[i], ground_A[i]);
            normal_ids[5 + i] = engine_ingest_text(&eng, ground_B[i], ground_B[i]);
        }

        /* Stabilize: 5 SUBSTRATE_INT cycles */
        for (int t = 0; t < (int)SUBSTRATE_INT * 5; t++)
            engine_tick(&eng);

        /* Phase 2: interleave — 2 normal, 1 contradiction, tick between each.
         * This simulates realistic mixed input where contradiction is minority. */
        int contra_ids[10];
        int n_contra = 0, n_extra_normal = 0;

        /* Extra normal sentences (no negation, different phrasing) */
        static const char *extra_normal[] = {
            "birds sing at dawn near the old stone bridge over the creek",
            "the mountain trail winds through dense pine forests to the summit",
            "rain falls gently on the tin roof of the small cabin",
            "sunlight filters through the canopy onto the mossy forest floor",
            "the cat sleeps on the warm windowsill watching snow fall outside",
        };

        printf("  Phase 2: interleaved ingestion (normal + contradiction)...\n");
        for (int round = 0; round < 5; round++) {
            /* 2 normal sentences */
            int nid = engine_ingest_text(&eng, extra_normal[round], extra_normal[round]);
            (void)nid;
            n_extra_normal++;

            /* Tick between ingestions */
            for (int t = 0; t < (int)SUBSTRATE_INT; t++)
                engine_tick(&eng);

            /* 1 contradiction */
            contra_ids[n_contra] = engine_ingest_text(&eng, contra_A[round], contra_A[round]);
            n_contra++;

            /* Tick between rounds */
            for (int t = 0; t < (int)SUBSTRATE_INT; t++)
                engine_tick(&eng);
        }

        /* Phase 3: longer adaptation — 10 SUBSTRATE_INT cycles */
        printf("  Phase 3: adapting for %d ticks...\n", (int)SUBSTRATE_INT * 10);
        for (int t = 0; t < (int)SUBSTRATE_INT * 10; t++)
            engine_tick(&eng);

        /* Predict on everything */
        printf("\n  ── POLARITY PREDICTION (original normal — should be 0) ──\n");
        int fp_normal = 0, tp_contra = 0;
        for (int i = 0; i < 10; i++) {
            if (normal_ids[i] >= 0 && g0->nodes[normal_ids[i]].alive) {
                const char *lbl = (i < 5) ? ground_A[i] : ground_B[i - 5];
                int pred = engine_predict_polarity(&eng, normal_ids[i], lbl);
                if (pred) fp_normal++;
            }
        }

        printf("  ── POLARITY PREDICTION (extra normal — should be 0) ──\n");
        for (int i = 0; i < CORPUS_SIZE; i++) {
            /* Find the node by name since we didn't save IDs carefully */
            int nid = graph_find(g0, extra_normal[i]);
            if (nid >= 0 && g0->nodes[nid].alive) {
                int pred = engine_predict_polarity(&eng, nid, extra_normal[i]);
                if (pred) fp_normal++;
            }
        }

        printf("  ── POLARITY PREDICTION (contradictions — should be 1) ──\n");
        for (int i = 0; i < n_contra; i++) {
            if (contra_ids[i] >= 0 && g0->nodes[contra_ids[i]].alive) {
                int pred = engine_predict_polarity(&eng, contra_ids[i], contra_A[i]);
                if (pred) tp_contra++;
            }
        }

        printf("\n  ── MIXED WORKLOAD SUMMARY ──\n");
        printf("  Normal nodes with false positive (burst_wt > 0): %d/15\n", fp_normal);
        printf("  Contradiction nodes detected (burst_wt > 0):     %d/%d\n", tp_contra, n_contra);
        printf("  Separation: %s\n",
               (fp_normal == 0 && tp_contra == n_contra) ? "PERFECT" :
               (fp_normal <= 2 && tp_contra >= n_contra - 1) ? "GOOD" : "DEGRADED");
        engine_polarity_summary(&eng);

        engine_destroy(&eng);
    }

    /* ── SCENARIO D: Semantic contradictions (no negation keywords) ── */
    printf("\n── SCENARIO D: Semantic contradiction (zero keyword overlap) ──\n");
    {
        Engine eng;
        engine_init(&eng);
        Graph *g0 = &eng.shells[0].g;

        /* Ingest ground truth */
        int gt_ids[5];
        for (int i = 0; i < CORPUS_SIZE; i++)
            gt_ids[i] = engine_ingest_text(&eng, ground_A[i], ground_A[i]);

        /* Stabilize */
        printf("  Stabilizing ground truth (%d ticks)...\n", (int)SUBSTRATE_INT * 5);
        for (int t = 0; t < (int)SUBSTRATE_INT * 5; t++)
            engine_tick(&eng);

        /* Snapshot ground truth state */
        printf("  Ground truth after stabilization:\n");
        for (int i = 0; i < CORPUS_SIZE; i++) {
            if (gt_ids[i] < 0) continue;
            Node *n = &g0->nodes[gt_ids[i]];
            printf("    gt[%d] val=%-8d valence=%-3d coherent=%d crystal=%d alive=%d\n",
                   gt_ids[i], n->val, n->valence, n->coherent,
                   (n->valence >= 100 ? 1 : 0), n->alive);
        }

        /* Ingest semantic contradictions */
        printf("  Ingesting semantic contradictions (zero negation keywords):\n");
        int sc_ids[5];
        for (int i = 0; i < CORPUS_SIZE; i++) {
            sc_ids[i] = engine_ingest_text(&eng, semantic_contra[i], semantic_contra[i]);
            if (sc_ids[i] >= 0) {
                Node *n = &g0->nodes[sc_ids[i]];
                printf("    sc[%d] has_neg=%d val=%-8d\n",
                       sc_ids[i], n->has_negation, n->val);
            }
        }

        /* Snapshot immediately after injection */
        printf("  Ground truth immediately after semantic injection:\n");
        for (int i = 0; i < CORPUS_SIZE; i++) {
            if (gt_ids[i] < 0) continue;
            Node *n = &g0->nodes[gt_ids[i]];
            printf("    gt[%d] val=%-8d valence=%-3d coherent=%d contradicted=%d I_energy=%d\n",
                   gt_ids[i], n->val, n->valence, n->coherent,
                   n->contradicted, n->I_energy);
        }

        /* Adaptation phase */
        printf("  Running adaptation (%d ticks)...\n", (int)SUBSTRATE_INT * 3);
        for (int t = 0; t < (int)SUBSTRATE_INT * 3; t++)
            engine_tick(&eng);

        /* Full diagnostic grid after adaptation */
        printf("\n  ── POST-ADAPTATION GRID ──\n");
        printf("  Ground truth nodes:\n");
        int gt_dead = 0;
        for (int i = 0; i < CORPUS_SIZE; i++) {
            if (gt_ids[i] < 0) continue;
            Node *n = &g0->nodes[gt_ids[i]];
            printf("    gt[%d] val=%-8d valence=%-3d coherent=%d contradicted=%d "
                   "crystal=%d alive=%d I_energy=%d\n",
                   gt_ids[i], n->val, n->valence, n->coherent,
                   n->contradicted, (n->valence >= 100 ? 1 : 0),
                   n->alive, n->I_energy);
            if (!n->alive || n->valence < 100) gt_dead++;
        }

        printf("  Semantic contradiction nodes:\n");
        int sc_dead = 0;
        for (int i = 0; i < CORPUS_SIZE; i++) {
            if (sc_ids[i] < 0) continue;
            Node *n = &g0->nodes[sc_ids[i]];
            printf("    sc[%d] val=%-8d valence=%-3d coherent=%d contradicted=%d "
                   "crystal=%d alive=%d I_energy=%d\n",
                   sc_ids[i], n->val, n->valence, n->coherent,
                   n->contradicted, (n->valence >= 100 ? 1 : 0),
                   n->alive, n->I_energy);
            if (!n->alive || n->valence < 100) sc_dead++;
        }

        /* Edge topology */
        edge_topology(g0, gt_ids, 5, "D-ground");
        edge_topology(g0, sc_ids, 5, "D-semantic");

        /* Observer predictions */
        printf("\n  ── OBSERVER PREDICTIONS ──\n");
        printf("  Ground truth (expect 0 — normal):\n");
        for (int i = 0; i < CORPUS_SIZE; i++)
            if (gt_ids[i] >= 0)
                engine_predict_polarity(&eng, gt_ids[i], ground_A[i]);

        printf("  Semantic contradictions (expect 0 — observer blind):\n");
        for (int i = 0; i < CORPUS_SIZE; i++)
            if (sc_ids[i] >= 0)
                engine_predict_polarity(&eng, sc_ids[i], semantic_contra[i]);

        /* BitStream overlap: what does the transducer actually see? */
        printf("\n  ── BITSTREAM OVERLAP ──\n");
        for (int i = 0; i < CORPUS_SIZE; i++) {
            if (gt_ids[i] < 0 || sc_ids[i] < 0) continue;
            BitStream *gt_bs = &g0->nodes[gt_ids[i]].identity;
            BitStream *sc_bs = &g0->nodes[sc_ids[i]].identity;
            int overlap = bs_mutual_contain(gt_bs, sc_bs);
            int gt_pop = bs_popcount(gt_bs);
            int sc_pop = bs_popcount(sc_bs);
            /* XOR: bits that differ */
            int xor_pop = 0;
            int n = gt_bs->len < sc_bs->len ? gt_bs->len : sc_bs->len;
            int full = n / 64;
            for (int w = 0; w < full; w++)
                xor_pop += xyzt_popcnt64(gt_bs->w[w] ^ sc_bs->w[w]);
            printf("    gt[%d] vs sc[%d]: overlap=%d%% gt_pop=%d sc_pop=%d xor_bits=%d\n",
                   i, i, overlap, gt_pop, sc_pop, xor_pop);
        }
        /* Also compare ground truth to ground truth (baseline) */
        printf("  Ground-to-ground baseline:\n");
        for (int i = 0; i < 4; i++) {
            if (gt_ids[i] < 0 || gt_ids[i+1] < 0) continue;
            BitStream *a = &g0->nodes[gt_ids[i]].identity;
            BitStream *b = &g0->nodes[gt_ids[i+1]].identity;
            int overlap = bs_mutual_contain(a, b);
            int xor_pop = 0;
            int nn = a->len < b->len ? a->len : b->len;
            int full = nn / 64;
            for (int w = 0; w < full; w++)
                xor_pop += xyzt_popcnt64(a->w[w] ^ b->w[w]);
            printf("    gt[%d] vs gt[%d]: overlap=%d%% xor_bits=%d\n",
                   i, i+1, overlap, xor_pop);
        }

        printf("\n  ── SCENARIO D SUMMARY ──\n");
        printf("  Ground truth dead/dissolved: %d/5\n", gt_dead);
        printf("  Semantic contra dead/dissolved: %d/5\n", sc_dead);
        engine_polarity_summary(&eng);

        engine_destroy(&eng);
    }

    /* ── COMPARISON ── */
    printf("\n── COMPARISON ──\n");
    printf("  Look for: different active channels, duty patterns, regularity,\n");
    printf("  burst events, correlations. If B shows features A doesn't\n");
    printf("  (or vice versa), sense.c is distinguishing the signatures.\n");
    printf("  Key differentiators: inverted edges in state_buf byte stream\n");
    printf("  change bit-level patterns that sense reads as channels.\n");
}
