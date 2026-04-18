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

/* Yee coherence download */
extern int yee_download_acc_raw(float *h_acc, int n);
extern int yee_download_signed(float *h_signed, int n);

int cortex_self_observe(Cortex *c) {
    if (!c->gpu_ok) return -1;
    Graph *g0 = &c->eng.shells[0].g;
    if (g0->n_nodes == 0) return -1;

    /* Read coherence field at each node's voxel position */
    float *h_acc = (float *)malloc(YEE_TOTAL * sizeof(float));
    float *h_signed = (float *)malloc(YEE_TOTAL * sizeof(float));
    if (!h_acc || !h_signed) { free(h_acc); free(h_signed); return -1; }

    yee_download_acc_raw(h_acc, YEE_TOTAL);
    yee_download_signed(h_signed, YEE_TOTAL);

    /* Find top-4 resonating nodes by combined score:
     * crystal strength + valence + (1 - coherence) * 100
     * Low coherence = oscillating = inside carved waveguide = interesting */
    typedef struct { int id; float score; } Candidate;
    Candidate top[4] = {{-1,0},{-1,0},{-1,0},{-1,0}};
    int n_top = 0;

    for (int i = 0; i < g0->n_nodes; i++) {
        Node *n = &g0->nodes[i];
        if (!n->alive || n->layer_zero) continue;
        /* Skip previous self-observations to prevent infinite nesting */
        if (n->name[0] == '_' && n->name[1] == 's' && n->name[2] == 'o') continue;

        int gx = coord_x(n->coord) % 64;
        int gy = coord_y(n->coord) % 64;
        int gz = coord_z(n->coord) % 64;
        int vid = gx + gy * 64 + gz * 64 * 64;

        float energy = h_acc[vid];
        float coherence = (energy > 0.001f) ? fabsf(h_signed[vid]) / energy : 1.0f;
        float incoherence = 1.0f - coherence;  /* high = oscillating = resonant */

        float score = (float)crystal_strength(n) +
                      (float)n->valence * 0.5f +
                      incoherence * 200.0f;

        if (n_top < 4) {
            top[n_top].id = i; top[n_top].score = score; n_top++;
        } else {
            int min_k = 0;
            for (int k = 1; k < 4; k++)
                if (top[k].score < top[min_k].score) min_k = k;
            if (score > top[min_k].score) {
                top[min_k].id = i; top[min_k].score = score;
            }
        }
    }

    free(h_acc);
    free(h_signed);

    if (n_top == 0) return -1;

    /* Encode: concatenate the top nodes' names + scores into a self-observation.
     * This is what the engine "sees" about itself. The content of this
     * observation IS the engine's state compressed into bytes. */
    char self_buf[256];
    int pos = 0;
    for (int k = 0; k < n_top && top[k].id >= 0; k++) {
        Node *n = &g0->nodes[top[k].id];
        int written = snprintf(self_buf + pos, 256 - pos, "%s:%.0f ",
                               n->name, top[k].score);
        if (written > 0) pos += written;
    }

    /* Ingest the self-observation as a new node */
    char name[64];
    snprintf(name, 64, "_so_%06llu", (unsigned long long)c->eng.total_ticks);

    BitStream bs;
    encode_bytes(&bs, (const uint8_t *)self_buf, pos);
    int id = engine_ingest(&c->eng, name, &bs);
    if (id >= 0) c->n_ingested++;

    return id;
}

/* ── Prediction loop: graph proposes, wave verifies, Hebbian learns ──
 *
 * 1. Run inference on current state (Phase 4a spatial + 4b topological)
 * 2. Top-K results are PREDICTIONS — what the graph thinks is relevant
 * 3. Re-inject predictions at 0.3x amplitude (hypothesis, not statement)
 * 4. Tick to let waves propagate through carved topology
 * 5. Read which predictions resonated (survived the sponge)
 * 6. Strengthen edges to verified predictions, weaken edges to failed ones
 *
 * The sponge is the bullshit detector. Wrong predictions get absorbed.
 * Only predictions matching carved knowledge survive.
 * The engine can't hallucinate because the physics won't let it. */

extern int yee_clear_fields(void);
extern int yee_inject(const void *sources, int n);
extern int yee_apply_sponge(int width, float rate);
extern int yee_download_acc_raw(float *h_acc, int n);

int cortex_predict(Cortex *c) {
    if (!c->gpu_ok) return 0;
    Graph *g0 = &c->eng.shells[0].g;
    if (g0->n_nodes < 2) return 0;

    /* Step 1: Find what the engine is currently "thinking about" */
    int best_id = -1;
    int best_score = 0;
    for (int i = 0; i < g0->n_nodes; i++) {
        Node *n = &g0->nodes[i];
        if (!n->alive || n->layer_zero) continue;
        if (n->name[0] == '_') continue;
        int score = abs(n->val) + (int)n->valence * 4 + crystal_strength(n) * 2;
        if (score > best_score) { best_score = score; best_id = i; }
    }
    if (best_id < 0) return 0;

    /* Step 2: Find nodes connected to the current focus via strong edges */
    typedef struct { int voxel; int node_id; float amp; } Prediction;
    Prediction preds[8];
    int n_preds = 0;

    for (int e = 0; e < g0->n_edges && n_preds < 8; e++) {
        Edge *ed = &g0->edges[e];
        if (ed->weight < 128) continue;  /* only strong edges */
        int target = -1;
        if (ed->src_a == best_id) target = ed->dst;
        else if (ed->dst == best_id) target = ed->src_a;
        if (target < 0 || target == best_id) continue;
        if (!g0->nodes[target].alive) continue;

        int gx = coord_x(g0->nodes[target].coord) % 64;
        int gy = coord_y(g0->nodes[target].coord) % 64;
        int gz = coord_z(g0->nodes[target].coord) % 64;

        preds[n_preds].voxel = gx + gy * 64 + gz * 64 * 64;
        preds[n_preds].node_id = target;
        preds[n_preds].amp = 0.3f * (float)ed->weight / 255.0f;  /* 0.3x = hypothesis */
        n_preds++;
    }

    if (n_preds == 0) return 0;

    /* Step 3: Clear and inject predictions at reduced amplitude.
     * Predict needs its own clean grid — hypotheses must propagate
     * without interference from prior wave state. */
    yee_clear_fields();

    /* Reuse the YeeSource layout (voxel_id, amplitude, strength) */
    typedef struct { int vid; float amp; float str; } PS;
    PS psrc[8];
    for (int i = 0; i < n_preds; i++) {
        psrc[i].vid = preds[i].voxel;
        psrc[i].amp = preds[i].amp;
        psrc[i].str = 1.0f;
    }

    /* Drive predictions for 15 ticks, then let ring with sponge */
    for (int t = 0; t < 40; t++) {
        if (t < 15)
            yee_inject((const void *)psrc, n_preds);
        yee_tick();
        yee_apply_sponge(4, 0.15f);
    }

    /* Step 4: Read which predictions resonated */
    float *h_acc = (float *)malloc(YEE_TOTAL * sizeof(float));
    if (!h_acc) return 0;
    yee_download_acc_raw(h_acc, YEE_TOTAL);

    int verified = 0;
    for (int i = 0; i < n_preds; i++) {
        float energy = h_acc[preds[i].voxel];
        int nid = preds[i].node_id;

        if (energy > 0.01f) {
            /* Prediction verified — edge strengthened */
            verified++;
            if (g0->nodes[nid].valence < 254)
                g0->nodes[nid].valence += 2;
        }
        /* Predictions below 0.01 were absorbed by sponge — wrong prediction.
         * Don't explicitly weaken — the Hebbian's normal weaken rate handles decay. */
    }

    free(h_acc);
    return verified;
}

/* ══════════════════════════════════════════════════════════════
 * THE AUTONOMOUS HEARTBEAT
 *
 * Every component already existed. This function connects them.
 * Each cycle: tick → sense → voice → predict → self-observe → feedback.
 * The output feeds back as input. x = f(x). The loop closes.
 *
 * The engine turns on.
 * ══════════════════════════════════════════════════════════════ */

extern int wire_output_decode(uint8_t *decoded, int max_bytes);
extern int wire_retina_inject(const uint8_t *data, int len, float amplitude);
extern int yee_tick(void);

int cortex_heartbeat(Cortex *c, int n_cycles) {
    if (!c->gpu_ok) return 0;

    for (int cycle = 0; cycle < n_cycles; cycle++) {

        /* 0. CLEAR — E/H fields are ephemeral scratch space.
         * L-field persists. Each cycle starts clean. */
        yee_clear_fields();

        /* 1. TICK — physics + graph run for one SUBSTRATE_INT cycle.
         * Sponge runs periodically to accumulate boundary absorption
         * into d_V_output — this IS the voice. Without sponge,
         * the output accumulator stays zero and the voice is silent. */
        for (int t = 0; t < (int)SUBSTRATE_INT; t++) {
            wire_engine_to_yee(&c->eng);
            yee_tick_async();
            engine_tick(&c->eng);
            yee_sync();
            if (t % 10 == 9) {
                yee_apply_sponge(4, 0.03f);
                yee_sync();  /* sponge writes d_V_output via unified memory —
                              * must complete before next wire_engine_to_yee reads it */
            }
        }

        /* 2. SENSE — read the grid into the graph */
        wire_yee_retinas(&c->eng, c->yee_sub);
        wire_yee_to_engine(&c->eng);
        yee_hebbian(0.01f, 0.005f);

        /* 3. VOICE — read the engine's own output.
         * The sponge has been absorbing boundary energy every tick.
         * Whatever survived propagation through the L-field IS the voice. */
        uint8_t voice[64];
        int voice_len = wire_output_decode(voice, 64);

        /* 4. PREDICT — graph proposes, physics verifies.
         * 0.3x amplitude hypotheses. Sponge kills wrong ones.
         * Verified predictions strengthen edges. */
        int verified = cortex_predict(c);
        (void)verified;

        /* 5. SELF-OBSERVE — every 4th cycle.
         * The engine reads its own resonance pattern, encodes it,
         * and ingests it as a new node. The engine sees itself.
         * Not every cycle — too much self-nesting floods the graph. */
        if (cycle % 4 == 0) {
            cortex_self_observe(c);
        }

        /* 6. FEEDBACK — voice re-enters through both paths.
         * Physics path: retina injection on x=0 face. The wave hears
         * its own echo. Sponge absorbs on x=63, retina injects on x=0.
         * Graph path: voice becomes a node for Hebbian to learn from.
         * Both paths close the loop. x = f(x) through physics AND graph. */
        if (voice_len > 0) {
            /* Substrate closure: voice re-enters through the retina */
            wire_retina_inject(voice, voice_len, 0.5f);
            char vname[64];
            snprintf(vname, 64, "_voice_%06llu",
                     (unsigned long long)c->eng.total_ticks);
            BitStream bs;
            encode_bytes(&bs, voice, voice_len);
            engine_ingest(&c->eng, vname, &bs);
            c->n_ingested++;
        }

        /* 7. DRIVE — emotional states trigger actions.
         * Frustration: explore (dream with thermal noise).
         * Boredom: introspect (extra self-observation).
         * Curiosity: keep perceiving (default — do nothing extra). */
        uint8_t drive = c->eng.shells[0].g.drive;
        if (drive == 1) {
            /* Frustrated — dream: inject noise, see what resonates */
            DreamResult dreams[4];
            infer_dream(&c->eng, dreams, 4, 100);
        } else if (drive == 2 && cycle % 4 != 0) {
            /* Bored — extra introspection (if not already self-observing) */
            cortex_self_observe(c);
        }
        /* drive == 0: curiosity. The default tick loop IS curiosity. */

        /* 8. PERSIST */
        engine_save(&c->eng, "state.xyzt");
    }

    return n_cycles;
}

void cortex_destroy(Cortex *c) {
    free(c->yee_sub);
    c->yee_sub = NULL;
    if (c->gpu_ok) yee_destroy();
    engine_destroy(&c->eng);
}
