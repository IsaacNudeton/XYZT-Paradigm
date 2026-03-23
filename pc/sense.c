/*
 * sense.c — Sense feedback: GPU co-presence → valence
 *
 * Isaac Oravec & Claude, March 2026
 */

#include "sense.h"

void sense_feedback(Engine *eng, const sense_result_t *result) {
    Graph *g = &eng->shells[0].g;
    for (int i = 0; i < result->n_features; i++) {
        int id = result->node_ids[i];
        if (id < 0 || id >= g->n_nodes) continue;
        Node *n = &g->nodes[id];
        if (!n->alive) continue;
        if (n->val != 0) {
            if (n->valence < 255) n->valence++;
        } else {
            if (n->valence > 0) n->valence--;
        }
    }
}
