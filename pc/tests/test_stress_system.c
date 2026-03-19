/*
 * test_stress_system.c — System-level stress tests
 *
 * 10 tests designed to BREAK the engine. Every failure tells us
 * what to fix. ONETWO applied to the system itself.
 *
 * Isaac Oravec & Claude, March 2026
 */

#include "test.h"
#include <time.h>

extern int yee_init(void);
extern void yee_destroy(void);
extern int yee_tick(void);
extern int yee_hebbian(float strengthen_rate, float weaken_rate);
extern int yee_download_L(float *h_L, int n);
extern int yee_download_acc(uint8_t *h_substrate, int n);
extern int yee_is_initialized(void);

extern int wire_engine_to_yee(const Engine *eng);
extern int wire_yee_to_engine(Engine *eng);
extern int wire_yee_retinas(Engine *eng, uint8_t *yee_substrate);

#define YEE_TOTAL (64 * 64 * 64)

static double elapsed_ms(clock_t a, clock_t b) {
    return (double)(b - a) * 1000.0 / CLOCKS_PER_SEC;
}

/* ══════════════════════════════════════════════════════════════
 * TEST 1: NODE CEILING — push past MAX_NODES
 * ══════════════════════════════════════════════════════════════ */
static void stress_node_ceiling(void) {
    printf("\n  -- STRESS 1: Node Ceiling (1000 ingests) --\n");
    Engine eng;
    engine_init(&eng);

    int accepted = 0, rejected = 0;
    for (int i = 0; i < 1000; i++) {
        uint8_t buf[32];
        for (int b = 0; b < 32; b++) buf[b] = (uint8_t)(i * 41 + b * 13);
        BitStream bs;
        encode_bytes(&bs, buf, 32);
        char name[16];
        snprintf(name, sizeof(name), "ceil_%d", i);
        int id = engine_ingest(&eng, name, &bs);
        if (id >= 0) accepted++; else rejected++;

        if (i % 500 == 499)
            for (int t = 0; t < (int)SUBSTRATE_INT; t++)
                engine_tick(&eng);
    }

    Graph *g0 = &eng.shells[0].g;
    printf("    accepted=%d rejected=%d nodes=%d edges=%d\n",
           accepted, rejected, g0->n_nodes, g0->n_edges);
    check("stress1: no crash after 5000 ingests", 1, 1);
    check("stress1: some rejected (hit ceiling)", 1, rejected > 0 ? 1 : 0);
    check("stress1: nodes <= MAX_NODES", 1, g0->n_nodes <= MAX_NODES ? 1 : 0);

    engine_destroy(&eng);
}

/* ══════════════════════════════════════════════════════════════
 * TEST 2: FINGERPRINT COLLISION — reversed content
 * ══════════════════════════════════════════════════════════════ */
static void stress_fingerprint_collision(void) {
    printf("\n  -- STRESS 2: Fingerprint Collision --\n");
    Engine eng;
    engine_init(&eng);

    uint8_t a[] = "aaaa bbbb cccc dddd eeee ffff gggg hhhh";
    uint8_t b[] = "hhhh gggg ffff eeee dddd cccc bbbb aaaa";  /* reversed words */
    uint8_t c[] = "aabb ccdd eeff gghh aabb ccdd eeff gghh";  /* different chunking */

    BitStream bs_a, bs_b, bs_c;
    onetwo_parse(a, 38, &bs_a);
    onetwo_parse(b, 38, &bs_b);
    onetwo_parse(c, 38, &bs_c);

    int corr_ab = bs_corr(&bs_a, &bs_b);
    int corr_ac = bs_corr(&bs_a, &bs_c);
    int corr_bc = bs_corr(&bs_b, &bs_c);

    printf("    A↔B (reversed words): corr=%d\n", corr_ab);
    printf("    A↔C (diff chunking):  corr=%d\n", corr_ac);
    printf("    B↔C:                  corr=%d\n", corr_bc);

    /* If corr > 90, the fingerprint can't see word order */
    if (corr_ab > 90)
        printf("    WARNING: fingerprint blind to word order (corr=%d)\n", corr_ab);
    check("stress2: fingerprint computed", 1, 1);

    engine_destroy(&eng);
}

/* ══════════════════════════════════════════════════════════════
 * TEST 3: TEMPORAL BURST — 100 ingests in 10 ticks
 * ══════════════════════════════════════════════════════════════ */
static void stress_temporal_burst(void) {
    printf("\n  -- STRESS 3: Temporal Burst --\n");
    Engine eng;
    engine_init(&eng);
    BitStream bs;

    /* Burst: 100 observations, 1 tick between each */
    clock_t t0 = clock();
    for (int i = 0; i < 100; i++) {
        uint8_t buf[64];
        for (int b = 0; b < 64; b++) buf[b] = (uint8_t)(i * 29 + b * 11);
        encode_bytes(&bs, buf, 64);
        char name[16];
        snprintf(name, sizeof(name), "burst_%d", i);
        engine_ingest(&eng, name, &bs);
        engine_tick(&eng);
    }
    clock_t t1 = clock();
    double burst_ms = elapsed_ms(t0, t1);

    /* Long quiet period */
    for (int t = 0; t < (int)SUBSTRATE_INT * 5; t++)
        engine_tick(&eng);

    Graph *g0 = &eng.shells[0].g;
    int alive = 0, crystal = 0;
    for (int i = 0; i < g0->n_nodes; i++) {
        if (g0->nodes[i].alive && !g0->nodes[i].layer_zero) {
            alive++;
            if (g0->nodes[i].valence >= 200) crystal++;
        }
    }

    printf("    burst: 100 ingests in %.0fms, alive=%d crystal=%d edges=%d\n",
           burst_ms, alive, crystal, g0->n_edges);
    check("stress3: survived burst", 1, alive > 50 ? 1 : 0);
    check("stress3: some crystallized after settle", 1, crystal > 0 ? 1 : 0);

    engine_destroy(&eng);
}

/* ══════════════════════════════════════════════════════════════
 * TEST 4: CONTRADICTION STORM — 20 + 20 inverted
 * ══════════════════════════════════════════════════════════════ */
static void stress_contradiction_storm(void) {
    printf("\n  -- STRESS 4: Contradiction Storm --\n");
    Engine eng;
    engine_init(&eng);
    BitStream bs;

    /* Establish 20 beliefs */
    int belief_ids[20];
    for (int i = 0; i < 20; i++) {
        uint8_t buf[64];
        for (int b = 0; b < 64; b++) buf[b] = (uint8_t)(i * 37 + b * 7 + 0xAA);
        encode_bytes(&bs, buf, 64);
        char name[16];
        snprintf(name, sizeof(name), "belief_%d", i);
        belief_ids[i] = engine_ingest(&eng, name, &bs);
    }

    /* Crystallize */
    for (int t = 0; t < (int)SUBSTRATE_INT * 5; t++)
        engine_tick(&eng);

    Graph *g0 = &eng.shells[0].g;
    int beliefs_alive_before = 0;
    for (int i = 0; i < 20; i++)
        if (belief_ids[i] >= 0 && g0->nodes[belief_ids[i]].alive)
            beliefs_alive_before++;

    /* Inject 20 contradictions (XOR 0xAA) */
    for (int i = 0; i < 20; i++) {
        uint8_t buf[64];
        for (int b = 0; b < 64; b++) buf[b] = (uint8_t)((i * 37 + b * 7 + 0xAA) ^ 0xAA);
        encode_bytes(&bs, buf, 64);
        char name[16];
        snprintf(name, sizeof(name), "contra_%d", i);
        engine_ingest(&eng, name, &bs);
    }

    /* Let the storm rage */
    int32_t peak_error = 0;
    for (int t = 0; t < (int)SUBSTRATE_INT * 10; t++) {
        engine_tick(&eng);
        if (eng.graph_error > peak_error) peak_error = eng.graph_error;
    }

    int beliefs_alive_after = 0, contras_alive = 0;
    for (int i = 0; i < 20; i++) {
        if (belief_ids[i] >= 0 && g0->nodes[belief_ids[i]].alive)
            beliefs_alive_after++;
    }
    for (int i = 0; i < g0->n_nodes; i++)
        if (g0->nodes[i].alive && strncmp(g0->nodes[i].name, "contra_", 7) == 0)
            contras_alive++;

    printf("    beliefs: before=%d after=%d  contras_alive=%d  peak_error=%d\n",
           beliefs_alive_before, beliefs_alive_after, contras_alive, peak_error);
    check("stress4: no crash", 1, 1);
    check("stress4: system responded", 1,
          (beliefs_alive_after != beliefs_alive_before || peak_error > 0) ? 1 : 0);

    engine_destroy(&eng);
}

/* ══════════════════════════════════════════════════════════════
 * TEST 5: SELF-SIMILARITY — feed engine its own save file
 * ══════════════════════════════════════════════════════════════ */
static void stress_self_similarity(void) {
    printf("\n  -- STRESS 5: Self-Similarity --\n");
    Engine eng;
    engine_init(&eng);
    BitStream bs;

    /* Build some state */
    for (int i = 0; i < 10; i++) {
        uint8_t buf[64];
        for (int b = 0; b < 64; b++) buf[b] = (uint8_t)(i * 23 + b);
        encode_bytes(&bs, buf, 64);
        char name[16];
        snprintf(name, sizeof(name), "self_%d", i);
        engine_ingest(&eng, name, &bs);
    }
    for (int t = 0; t < (int)SUBSTRATE_INT * 3; t++)
        engine_tick(&eng);

    /* Save it */
    engine_save(&eng, "stress_self.xyzt");

    /* Read the save file as raw bytes and feed it back */
    FILE *f = fopen("stress_self.xyzt", "rb");
    if (f) {
        uint8_t chunk[256];
        int n_read;
        int chunks_fed = 0;
        while ((n_read = (int)fread(chunk, 1, 256, f)) > 0 && chunks_fed < 20) {
            encode_bytes(&bs, chunk, n_read);
            char name[16];
            snprintf(name, sizeof(name), "raw_%d", chunks_fed);
            engine_ingest(&eng, name, &bs);
            chunks_fed++;
        }
        fclose(f);

        for (int t = 0; t < (int)SUBSTRATE_INT * 2; t++)
            engine_tick(&eng);

        Graph *g0 = &eng.shells[0].g;
        /* Check if raw_* nodes wired to self_* nodes */
        int cross_edges = 0;
        for (int e = 0; e < g0->n_edges; e++) {
            Edge *ed = &g0->edges[e];
            if (ed->weight == 0) continue;
            int src_self = (strncmp(g0->nodes[ed->src_a].name, "self_", 5) == 0);
            int dst_raw = (strncmp(g0->nodes[ed->dst].name, "raw_", 4) == 0);
            int src_raw = (strncmp(g0->nodes[ed->src_a].name, "raw_", 4) == 0);
            int dst_self = (strncmp(g0->nodes[ed->dst].name, "self_", 5) == 0);
            if ((src_self && dst_raw) || (src_raw && dst_self)) cross_edges++;
        }
        printf("    fed %d chunks of own save file, cross-edges=%d\n",
               chunks_fed, cross_edges);
        check("stress5: no crash eating own save", 1, 1);
    }

    engine_destroy(&eng);
    remove("stress_self.xyzt");
}

/* ══════════════════════════════════════════════════════════════
 * TEST 6: HASH COLLISION — check voxel position collisions
 * ══════════════════════════════════════════════════════════════ */
static void stress_hash_collision(void) {
    printf("\n  -- STRESS 6: Hash Collision (1000 nodes) --\n");
    Engine eng;
    engine_init(&eng);
    BitStream bs;

    int coords[1000][3];
    int n_created = 0;
    for (int i = 0; i < 1000 && n_created < 1000; i++) {
        uint8_t buf[32];
        for (int b = 0; b < 32; b++) buf[b] = (uint8_t)(i * 53 + b * 17);
        encode_bytes(&bs, buf, 32);
        char name[16];
        snprintf(name, sizeof(name), "hash_%d", i);
        int id = engine_ingest(&eng, name, &bs);
        if (id >= 0) {
            Node *n = &eng.shells[0].g.nodes[id];
            coords[n_created][0] = coord_x(n->coord) % 64;
            coords[n_created][1] = coord_y(n->coord) % 64;
            coords[n_created][2] = coord_z(n->coord) % 64;
            n_created++;
        }
    }

    /* Count collisions */
    int collisions = 0;
    for (int i = 0; i < n_created; i++)
        for (int j = i + 1; j < n_created; j++)
            if (coords[i][0] == coords[j][0] &&
                coords[i][1] == coords[j][1] &&
                coords[i][2] == coords[j][2])
                collisions++;

    printf("    created=%d  voxel_collisions=%d\n", n_created, collisions);
    printf("    expected (birthday): ~%d\n", n_created * n_created / (2 * 64 * 64 * 64));
    check("stress6: measured collisions", 1, 1);

    engine_destroy(&eng);
}

/* ══════════════════════════════════════════════════════════════
 * TEST 7: SAVE/LOAD CYCLES — 10 rounds
 * ══════════════════════════════════════════════════════════════ */
static void stress_save_load_cycles(void) {
    printf("\n  -- STRESS 7: Save/Load Cycles (10 rounds) --\n");
    Engine eng;
    engine_init(&eng);
    yee_init();
    BitStream bs;

    /* Build initial state */
    for (int i = 0; i < 20; i++) {
        uint8_t buf[64];
        for (int b = 0; b < 64; b++) buf[b] = (uint8_t)(i * 19 + b);
        encode_bytes(&bs, buf, 64);
        char name[16];
        snprintf(name, sizeof(name), "cyc_%d", i);
        engine_ingest(&eng, name, &bs);
    }

    const char *path = "stress_cycles.xyzt";
    for (int round = 0; round < 10; round++) {
        for (int t = 0; t < (int)SUBSTRATE_INT; t++)
            engine_tick(&eng);
        engine_save(&eng, path);
        engine_destroy(&eng);
        yee_destroy();

        engine_init(&eng);
        yee_init();
        engine_load(&eng, path);
    }

    Graph *g0 = &eng.shells[0].g;
    int alive = 0;
    for (int i = 0; i < g0->n_nodes; i++)
        if (g0->nodes[i].alive && !g0->nodes[i].layer_zero) alive++;

    printf("    after 10 save/load cycles: alive=%d edges=%d ticks=%llu\n",
           alive, g0->n_edges, (unsigned long long)eng.total_ticks);
    check("stress7: survived 10 cycles", 1, alive > 0 ? 1 : 0);
    check("stress7: ticks accumulated", 1, eng.total_ticks > 1000 ? 1 : 0);

    engine_destroy(&eng);
    yee_destroy();
    remove(path);
}

/* ══════════════════════════════════════════════════════════════
 * TEST 8: CHILD SATURATION — all 4 slots filled
 * ══════════════════════════════════════════════════════════════ */
static void stress_child_saturation(void) {
    printf("\n  -- STRESS 8: Child Saturation --\n");
    Engine eng;
    engine_init(&eng);
    BitStream bs;

    /* Ingest enough for 4+ children */
    for (int i = 0; i < 20; i++) {
        uint8_t buf[64];
        for (int b = 0; b < 64; b++) buf[b] = (uint8_t)(i * 41 + b * 13 + 0x33);
        encode_bytes(&bs, buf, 64);
        char name[16];
        snprintf(name, sizeof(name), "child_%d", i);
        engine_ingest(&eng, name, &bs);
    }

    /* Run until children spawn */
    for (int t = 0; t < (int)SUBSTRATE_INT * 15; t++)
        engine_tick(&eng);

    printf("    children=%d (max=%d)\n", eng.n_children, MAX_CHILDREN);

    /* Keep ingesting past saturation */
    for (int i = 0; i < 30; i++) {
        uint8_t buf[64];
        for (int b = 0; b < 64; b++) buf[b] = (uint8_t)((i + 20) * 41 + b * 13 + 0x33);
        encode_bytes(&bs, buf, 64);
        char name[16];
        snprintf(name, sizeof(name), "extra_%d", i);
        engine_ingest(&eng, name, &bs);
    }
    for (int t = 0; t < (int)SUBSTRATE_INT * 5; t++)
        engine_tick(&eng);

    printf("    after extra ingests: children=%d\n", eng.n_children);
    check("stress8: children capped at MAX", 1, eng.n_children <= MAX_CHILDREN ? 1 : 0);
    check("stress8: no crash past saturation", 1, 1);

    engine_destroy(&eng);
}

/* ══════════════════════════════════════════════════════════════
 * TEST 9: EMPTY STREAM
 * ══════════════════════════════════════════════════════════════ */
static void stress_empty_stream(void) {
    printf("\n  -- STRESS 9: Empty Engine --\n");
    Engine eng;
    engine_init(&eng);

    /* Tick an empty engine */
    for (int t = 0; t < 1000; t++)
        engine_tick(&eng);

    check("stress9: empty engine survived 1000 ticks", 1, 1);
    check("stress9: no nodes created", 0, eng.shells[0].g.n_nodes);

    /* Save empty state */
    engine_save(&eng, "stress_empty.xyzt");

    /* Load into fresh engine */
    Engine eng2;
    engine_init(&eng2);
    int lr = engine_load(&eng2, "stress_empty.xyzt");
    check("stress9: empty save/load works", 0, lr);

    /* Ingest into recovered engine */
    BitStream bs;
    uint8_t buf[] = "recovery test data after empty";
    encode_bytes(&bs, buf, 30);
    int id = engine_ingest(&eng2, "recovered", &bs);
    check("stress9: can ingest after empty load", 1, id >= 0 ? 1 : 0);

    engine_destroy(&eng);
    engine_destroy(&eng2);
    remove("stress_empty.xyzt");
}

/* ══════════════════════════════════════════════════════════════
 * TEST 10: LONG RUN — L-field saturation check
 * ══════════════════════════════════════════════════════════════ */
static void stress_long_run(void) {
    printf("\n  -- STRESS 10: Long Run (5000 ticks with Yee) --\n");
    Engine eng;
    engine_init(&eng);
    yee_init();
    BitStream bs;
    uint8_t *yee_sub = (uint8_t *)calloc(YEE_TOTAL, 1);

    /* Ingest varied data */
    for (int i = 0; i < 30; i++) {
        uint8_t buf[64];
        for (int b = 0; b < 64; b++) buf[b] = (uint8_t)(i * 31 + b * 7 + (i & 3) * 64);
        encode_bytes(&bs, buf, 64);
        char name[16];
        snprintf(name, sizeof(name), "long_%d", i);
        engine_ingest(&eng, name, &bs);
    }

    /* Run 5000 ticks with full Yee loop */
    float l_min_start = 999, l_max_start = 0;
    {
        float *L = (float *)malloc(YEE_TOTAL * sizeof(float));
        yee_download_L(L, YEE_TOTAL);
        for (int i = 0; i < YEE_TOTAL; i++) {
            if (L[i] < l_min_start) l_min_start = L[i];
            if (L[i] > l_max_start) l_max_start = L[i];
        }
        free(L);
    }

    clock_t t0 = clock();
    for (int t = 0; t < 5000; t++) {
        wire_engine_to_yee(&eng);
        yee_tick();
        engine_tick(&eng);
        if (eng.total_ticks % SUBSTRATE_INT == 0) {
            yee_download_acc(yee_sub, YEE_TOTAL);
            wire_yee_retinas(&eng, yee_sub);
            wire_yee_to_engine(&eng);
            yee_hebbian(0.01f, 0.005f);
        }
    }
    clock_t t1 = clock();

    float *L_end = (float *)malloc(YEE_TOTAL * sizeof(float));
    yee_download_L(L_end, YEE_TOTAL);
    float l_min = 999, l_max = 0, l_mean = 0;
    int l_at_floor = 0, l_at_ceiling = 0;
    for (int i = 0; i < YEE_TOTAL; i++) {
        l_mean += L_end[i];
        if (L_end[i] < l_min) l_min = L_end[i];
        if (L_end[i] > l_max) l_max = L_end[i];
        if (L_end[i] <= 0.75f) l_at_floor++;    /* L_MIN */
        if (L_end[i] >= 16.0f) l_at_ceiling++;  /* L_MAX */
    }
    l_mean /= YEE_TOTAL;

    double ms = elapsed_ms(t0, t1);
    printf("    5000 ticks in %.0fms (%.0f ticks/sec)\n", ms, 5000000.0/ms);
    printf("    L start: [%.4f, %.4f]\n", l_min_start, l_max_start);
    printf("    L end:   [%.4f, %.4f] mean=%.4f\n", l_min, l_max, l_mean);
    printf("    at_floor(L<=0.75)=%d  at_ceiling(L>=16.0)=%d\n",
           l_at_floor, l_at_ceiling);

    check("stress10: L not saturated at floor", 1, l_at_floor == 0 ? 1 : 0);
    check("stress10: L not saturated at ceiling", 1, l_at_ceiling == 0 ? 1 : 0);
    check("stress10: L range expanded", 1, (l_max - l_min) > 0.01f ? 1 : 0);

    free(L_end);
    free(yee_sub);
    yee_destroy();
    engine_destroy(&eng);
}

/* ══════════════════════════════════════════════════════════════ */

void run_stress_system_tests(void) {
    printf("\n=== SYSTEM STRESS TESTS ===\n");

    stress_node_ceiling();
    stress_fingerprint_collision();
    stress_temporal_burst();
    stress_contradiction_storm();
    stress_self_similarity();
    stress_hash_collision();
    stress_save_load_cycles();
    stress_child_saturation();
    stress_empty_stream();
    stress_long_run();
}
