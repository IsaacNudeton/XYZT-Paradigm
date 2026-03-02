/*
 * demo_anomaly.c — Anomaly detection via structural resonance.
 *
 * Train: feed normal data (raw encoding), let the graph crystallize.
 * Probe: encode new data the same way, classify against the graph.
 * High resonance = recognized. Low resonance = anomaly.
 *
 * gcc -O2 -o demo_anomaly demo_anomaly.c -lm
 */
#define XYZT_NO_MAIN
#include "xyzt.c"

int main(void) {
    T_init(); srand(42);
    Engine eng;
    engine_init(&eng);

    printf("═══════════════════════════════════════════════════\n");
    printf("  ANOMALY DETECTION — zero training, zero labels\n");
    printf("═══════════════════════════════════════════════════\n\n");

    /* Phase 1: Build the graph from "normal" data */
    const char *normal[] = {
        "sensor: temp=72.3 humidity=45 pressure=1013 status=OK",
        "sensor: temp=72.1 humidity=46 pressure=1013 status=OK",
        "sensor: temp=72.4 humidity=45 pressure=1014 status=OK",
        "sensor: temp=72.2 humidity=44 pressure=1013 status=OK",
        "sensor: temp=72.0 humidity=45 pressure=1013 status=OK",
        "sensor: temp=72.3 humidity=46 pressure=1014 status=OK",
        "sensor: temp=72.1 humidity=45 pressure=1013 status=OK",
        "sensor: temp=72.5 humidity=44 pressure=1013 status=OK",
    };

    printf("  LEARNING: ingesting 8 normal readings (raw encoding)\n");
    char nname[32];
    for (int i = 0; i < 8; i++) {
        snprintf(nname, 32, "normal_%d", i);
        engine_ingest_raw(&eng, nname, normal[i]);
    }

    /* Let it learn */
    printf("  CRYSTALLIZING: 1000 ticks + learning passes\n");
    for (int t = 0; t < 1000; t++) engine_tick(&eng);

    int s0_nodes = eng.shells[0].g.n_nodes;
    int s0_edges = eng.shells[0].g.n_edges;
    int s1_nodes = eng.shells[1].g.n_nodes;
    int boundary = eng.n_boundary_edges;
    printf("  graph: s0=%d nodes %d edges | s1=%d nodes | %d boundary\n\n",
           s0_nodes, s0_edges, s1_nodes, boundary);

    /* Phase 2: Probe — classify without ingesting (same encoding) */
    printf("  PROBING — classify against learned graph\n");
    printf("  %-50s  %5s  %s\n", "input", "score", "verdict");
    printf("  %-50s  %5s  %s\n", "-----", "-----", "-------");

    struct { const char *text; const char *label; int expect_anomaly; } probes[] = {
        {"sensor: temp=72.3 humidity=45 pressure=1013 status=OK",  "normal (exact match)",    0},
        {"sensor: temp=72.0 humidity=46 pressure=1014 status=OK",  "normal (slight var)",     0},
        {"sensor: temp=155.0 humidity=99 pressure=800 status=ERR", "temp spike + error",      1},
        {"SYSTEM FAILURE: all sensors offline code=0xDEAD",         "system failure msg",      1},
        {"sensor: temp=72.2 humidity=45 pressure=1013 status=OK",  "normal (recovery)",       0},
        {"01011010 10100101 11110000 00001111 DEADBEEF",            "binary garbage",          1},
        {"sensor: temp=-40.0 humidity=0 pressure=200 status=CRIT", "extreme cold + critical", 1},
        {"sensor: temp=72.4 humidity=44 pressure=1013 status=OK",  "normal (final)",          0},
    };
    int n_probes = sizeof(probes) / sizeof(probes[0]);

    /* First, find baseline: what does the best normal match score? */
    BitStream bs_baseline;
    encode_raw(&bs_baseline, normal[0], strlen(normal[0]));
    int baseline_conf;
    engine_classify(&eng, &bs_baseline, &baseline_conf);
    int threshold = baseline_conf / 2;  /* anomaly = less than half of best normal score */
    if (threshold < 5) threshold = 5;
    printf("  (baseline resonance: %d%%, threshold: %d%%)\n\n", baseline_conf, threshold);

    int correct = 0;
    for (int i = 0; i < n_probes; i++) {
        BitStream bs;
        encode_raw(&bs, probes[i].text, strlen(probes[i].text));
        int conf;
        const char *match = engine_classify(&eng, &bs, &conf);

        int is_anomaly = (conf < threshold);
        int got_it = (is_anomaly == probes[i].expect_anomaly);
        if (got_it) correct++;

        printf("  %-50s  %4d%%  %s %s\n",
               probes[i].label, conf,
               is_anomaly ? "ANOMALY" : "normal ",
               got_it ? "" : "  *** WRONG");
    }

    printf("\n  ACCURACY: %d/%d (%d%%)\n", correct, n_probes, correct * 100 / n_probes);

    /* Phase 3: Graph state */
    printf("\n  GRAPH STATE:\n");
    for (int i = 0; i < eng.shells[0].g.n_nodes && i < 10; i++) {
        Node *n = &eng.shells[0].g.nodes[i];
        if (!n->alive) continue;
        int cs = crystal_strength(n);
        printf("    [%d] val=%8d valence=%3d crystal=%3d\n",
               i, n->val, n->valence, cs);
    }

    printf("\n═══════════════════════════════════════════════════\n");
    printf("  Zero labels. Zero training. %d bytes of state.\n", (int)sizeof(Engine));
    printf("═══════════════════════════════════════════════════\n");

    return 0;
}
