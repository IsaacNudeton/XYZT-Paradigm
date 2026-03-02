/*
 * test_sense.c — Sense layer tests
 */
#include "test.h"
#include "../sense.h"

void run_sense_tests(void) {
    /* SE1: sense_fill basic */
    printf("--- Sense: Fill ---\n");
    {
        uint8_t data[] = {0x55, 0xAA, 0x55, 0xAA, 0x55};
        sense_buf_t buf;
        sense_fill(&buf, data, 5);
        check("SE1 fill count", 5, (int)buf.count);
        check("SE1 fill val[0]", 0x55, buf.events[0].value);
        check("SE1 fill ts[3]", 3, (int)buf.events[3].timestamp);
    }

    /* SE2: ACTIVE detection */
    printf("--- Sense: Active ---\n");
    {
        Engine eng; engine_init(&eng);
        /* Alternating 0x55 and 0xAA: all 8 channels transition every sample */
        uint8_t data[32];
        for (int i = 0; i < 32; i++) data[i] = (i % 2) ? 0xAA : 0x55;
        sense_result_t result;
        sense_pass(&eng, data, 32, &result);
        check("SE2 features found", 1, result.n_features > 0 ? 1 : 0);
        /* All 8 channels should be active (31 transitions each) */
        int active_count = 0;
        Graph *g = &eng.shells[0].g;
        for (int i = 0; i < result.n_features; i++) {
            int id = result.node_ids[i];
            if (id >= 0 && id < g->n_nodes &&
                g->nodes[id].name[0] == 's' && g->nodes[id].name[2] == 'A')
                active_count++;
        }
        check("SE2 all 8 channels active", 8, active_count);
        engine_destroy(&eng);
    }

    /* SE3: DUTY detection */
    printf("--- Sense: Duty ---\n");
    {
        Engine eng; engine_init(&eng);
        /* 0x55 = 01010101: each channel 50% high across samples */
        uint8_t data[64];
        for (int i = 0; i < 64; i++) data[i] = (i % 2) ? 0xAA : 0x55;
        sense_result_t result;
        sense_pass(&eng, data, 64, &result);
        int duty_count = 0;
        Graph *g = &eng.shells[0].g;
        for (int i = 0; i < result.n_features; i++) {
            int id = result.node_ids[i];
            if (id >= 0 && id < g->n_nodes &&
                g->nodes[id].name[0] == 's' && g->nodes[id].name[2] == 'D')
                duty_count++;
        }
        check("SE3 duty features found", 1, duty_count > 0 ? 1 : 0);
        engine_destroy(&eng);
    }

    /* SE4: REGULARITY + PERIOD */
    printf("--- Sense: Regularity ---\n");
    {
        Engine eng; engine_init(&eng);
        /* Channel 0: toggle every sample = perfectly regular */
        uint8_t data[64];
        for (int i = 0; i < 64; i++) data[i] = (i % 2) ? 0x01 : 0x00;
        sense_result_t result;
        sense_pass(&eng, data, 64, &result);
        int reg_count = 0, period_count = 0;
        Graph *g = &eng.shells[0].g;
        for (int i = 0; i < result.n_features; i++) {
            int id = result.node_ids[i];
            if (id >= 0 && id < g->n_nodes && g->nodes[id].name[0] == 's') {
                if (g->nodes[id].name[2] == 'R') reg_count++;
                if (g->nodes[id].name[2] == 'P') period_count++;
            }
        }
        check("SE4 regularity detected", 1, reg_count > 0 ? 1 : 0);
        check("SE4 period detected", 1, period_count > 0 ? 1 : 0);
        engine_destroy(&eng);
    }

    /* SE5: Graph nodes have s: prefix */
    printf("--- Sense: Node Names ---\n");
    {
        Engine eng; engine_init(&eng);
        uint8_t data[32];
        for (int i = 0; i < 32; i++) data[i] = (i % 2) ? 0xFF : 0x00;
        sense_result_t result;
        sense_pass(&eng, data, 32, &result);
        int prefixed = 1;
        Graph *g = &eng.shells[0].g;
        for (int i = 0; i < result.n_features; i++) {
            int id = result.node_ids[i];
            if (id >= 0 && id < g->n_nodes) {
                if (g->nodes[id].name[0] != 's' || g->nodes[id].name[1] != ':')
                    prefixed = 0;
            }
        }
        check("SE5 all sense nodes s: prefixed", 1, prefixed);
        engine_destroy(&eng);
    }

    /* SE6: Hebbian edges between features */
    printf("--- Sense: Hebbian Edges ---\n");
    {
        Engine eng; engine_init(&eng);
        uint8_t data[64];
        for (int i = 0; i < 64; i++) data[i] = (i % 2) ? 0xFF : 0x00;
        sense_result_t result;
        sense_pass(&eng, data, 64, &result);
        /* Multiple features should create edges between them */
        int sense_edges = 0;
        Graph *g = &eng.shells[0].g;
        for (int e = 0; e < g->n_edges; e++) {
            int sa = g->edges[e].src_a, sd = g->edges[e].dst;
            if (sa < g->n_nodes && sd < g->n_nodes &&
                g->nodes[sa].name[0] == 's' && g->nodes[sd].name[0] == 's')
                sense_edges++;
        }
        check("SE6 sense edges created", 1, sense_edges > 0 ? 1 : 0);
        engine_destroy(&eng);
    }

    /* SE7: Decay reduces edge weight */
    printf("--- Sense: Decay ---\n");
    {
        Engine eng; engine_init(&eng);
        /* First pass: small data → few features, stays under conservation budget */
        uint8_t data1[8] = {0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00, 0xFF, 0x00};
        sense_result_t result;
        sense_pass(&eng, data1, 8, &result);
        int n_feats_1 = result.n_features;

        /* Record initial edge weight */
        Graph *g = &eng.shells[0].g;
        int init_weight = -1;
        int init_edge_idx = -1;
        for (int e = 0; e < g->n_edges; e++) {
            int sa = g->edges[e].src_a;
            if (sa < g->n_nodes && g->nodes[sa].name[0] == 's' && g->edges[e].weight > 0) {
                init_weight = g->edges[e].weight;
                init_edge_idx = e;
                break;
            }
        }

        /* Second pass: constant data → no transitions → no features → decay */
        uint8_t data2[8];
        memset(data2, 0x42, 8);
        sense_pass(&eng, data2, 8, &result);

        /* Check same edge's weight decreased or was pruned (weight 0) */
        int final_weight = (init_edge_idx >= 0) ? g->edges[init_edge_idx].weight : -1;
        check("SE7 had features", 1, n_feats_1 > 0 ? 1 : 0);
        check("SE7 edge decayed", 1,
              (init_weight > 0 && final_weight < init_weight) ? 1 : 0);
        engine_destroy(&eng);
    }

    /* SE8: Sense nodes have identity (participate in grow) */
    printf("--- Sense: Identity ---\n");
    {
        Engine eng; engine_init(&eng);
        uint8_t data[32];
        for (int i = 0; i < 32; i++) data[i] = (i % 2) ? 0x0F : 0xF0;
        sense_result_t result;
        sense_pass(&eng, data, 32, &result);
        int has_identity = 1;
        Graph *g = &eng.shells[0].g;
        for (int i = 0; i < result.n_features; i++) {
            int id = result.node_ids[i];
            if (id >= 0 && id < g->n_nodes) {
                if (g->nodes[id].identity.len == 0) has_identity = 0;
            }
        }
        check("SE8 sense nodes have identity", 1, has_identity);
        engine_destroy(&eng);
    }

    /* SE9: Integration — sense runs during engine tick */
    printf("--- Sense: Engine Integration ---\n");
    {
        Engine eng; engine_init(&eng);
        engine_ingest_text(&eng, "sense_test", "test data for sense integration pass abc");
        engine_ingest_text(&eng, "sense_test2", "different data for sense integration xyz");
        for (int i = 0; i <= (int)SUBSTRATE_INT; i++) engine_tick(&eng);
        check("SE9 sense ran in tick", 1, eng.last_sense.n_features > 0 ? 1 : 0);
        engine_destroy(&eng);
    }

    /* SE10: No regression — engine basic still works with sense active */
    printf("--- Sense: No Regression ---\n");
    {
        Engine eng; engine_init(&eng);
        int id = engine_ingest_text(&eng, "regression", "hello world regression test data");
        check("SE10 ingest ok", 1, id >= 0 ? 1 : 0);
        for (int i = 0; i < 10; i++) engine_tick(&eng);
        check("SE10 engine ticked", 10, (int)eng.total_ticks);
        engine_destroy(&eng);
    }
}
