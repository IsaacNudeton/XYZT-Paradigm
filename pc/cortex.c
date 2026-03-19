/*
 * cortex.c — Thin controller: ingest → inject → tick → read → loop
 *
 * The clean runtime path. Engine does the graph. Yee does the physics.
 * Cortex wires them together with minimal ceremony.
 *
 * Isaac Oravec & Claude, March 2026
 */

#include "cortex.h"
#include <stdlib.h>
#include <string.h>

/* Yee GPU functions */
extern int yee_init(void);
extern void yee_destroy(void);
extern int yee_tick_async(void);
extern int yee_sync(void);
extern int yee_download_acc(uint8_t *h_substrate, int n);
extern int yee_hebbian(float strengthen_rate, float weaken_rate);

/* Wire bridge */
extern int wire_engine_to_yee(const Engine *eng);
extern int wire_yee_to_engine(Engine *eng);
extern int wire_yee_retinas(Engine *eng, uint8_t *yee_substrate);

#define YEE_TOTAL (64 * 64 * 64)

int cortex_init(Cortex *c) {
    memset(c, 0, sizeof(*c));
    engine_init(&c->eng);

    c->gpu_ok = (yee_init() == 0);
    if (c->gpu_ok) {
        c->yee_sub = (uint8_t *)calloc(YEE_TOTAL, 1);
        wire_yee_retinas(&c->eng, c->yee_sub);
    }

    return c->gpu_ok ? 0 : -1;
}

int cortex_ingest(Cortex *c, const char *name, const uint8_t *data, int len) {
    BitStream bs;
    encode_bytes(&bs, data, len);
    int id = engine_ingest(&c->eng, name, &bs);
    if (id >= 0) c->n_ingested++;
    return id;
}

void cortex_tick(Cortex *c, int n_cycles) {
    for (int cycle = 0; cycle < n_cycles; cycle++) {
        for (int t = 0; t < (int)SUBSTRATE_INT; t++) {
            if (c->gpu_ok) {
                wire_engine_to_yee(&c->eng);
                yee_tick_async();
            }
            engine_tick(&c->eng);
            if (c->gpu_ok) yee_sync();
        }
        /* End-of-cycle sync + Hebbian */
        if (c->gpu_ok) {
            yee_download_acc(c->yee_sub, YEE_TOTAL);
            wire_yee_retinas(&c->eng, c->yee_sub);
            wire_yee_to_engine(&c->eng);
            yee_hebbian(0.01f, 0.005f);
        }
    }
}

int cortex_query(Cortex *c, const char *text, InferResult *out, int max) {
    return infer_query(&c->eng, text, out, max);
}

int cortex_save(Cortex *c, const char *path) {
    return engine_save(&c->eng, path);
}

int cortex_load(Cortex *c, const char *path) {
    return engine_load(&c->eng, path);
}

void cortex_destroy(Cortex *c) {
    free(c->yee_sub);
    c->yee_sub = NULL;
    if (c->gpu_ok) yee_destroy();
    engine_destroy(&c->eng);
}
