/*
 * test_child_divergence.c
 * Verifies that S1 children develop distinct topologies 
 * when parent substrate processes different data distributions.
 * 
 * Isaac Oravec & Claude, March 2026
 */

#include "../engine.h"
#include "../transducer.h"
#include <stdio.h>
#include <string.h>

void measure_child_divergence(const char* scenario_name, const char* data, int is_binary, int ticks) {
    Engine eng;
    engine_init(&eng);

    printf("Running scenario: %-15s | Ticks: %d\n", scenario_name, ticks);

    /* 1. Ingest Data */
    if (is_binary) {
        BitStream bs;
        encode_bytes(&bs, (const uint8_t*)data, (int)strlen(data));
        engine_ingest(&eng, scenario_name, &bs);
    } else {
        engine_ingest_text(&eng, scenario_name, data);
    }

    /* 2. Tick to force S1 Nesting and Child Hebbian Learning */
    for (int i = 0; i < ticks; i++) {
        engine_tick(&eng);
    }

    /* 3. Measure the resulting child topology */
    if (eng.n_shells > 1) {
        Graph *child = &eng.shells[1].g;
        int active_edges = 0;
        int total_weight = 0;
        int max_weight = 0;

        for (int e = 0; e < child->n_edges; e++) {
            int w = child->edges[e].weight;
            if (w > 0) {
                active_edges++;
                total_weight += w;
                if (w > max_weight) max_weight = w;
            }
        }
        
        double avg_weight = active_edges > 0 ? (double)total_weight / active_edges : 0;
        
        printf("  [Child Graph: %s]\n", eng.shells[1].name);
        printf("  Total Nodes : %d\n", child->n_nodes);
        printf("  Active Edges: %d\n", active_edges);
        printf("  Max Weight  : %d\n", max_weight);
        printf("  Avg Weight  : %.2f\n\n", avg_weight);
    } else {
        printf("  -> No child spawned. Heat/Valence too low.\n\n");
    }

    engine_destroy(&eng);
}

int main() {
    /* Highly structured, repeating linguistic pattern */
    const char *prose = 
        "The quick brown fox jumps over the lazy dog. "
        "The dog wakes up and barks at the fox. "
        "This is a highly structured, repeating sequence of english prose.";

    /* High-entropy, erratic bit flips */
    const char *binary = 
        "\x01\xFF\x80\x40\x20\x10\x08\x04\x02\x01\x00\x55\xAA\x55\xAA\xFF\x00\xFF\x00"
        "\x12\x34\x56\x78\x9A\xBC\xDE\xF0\x11\x22\x33\x44\x55\x66\x77\x88\x99\xAA\xBB";

    printf("=== RUNNING DIVERGENCE SWEEP ===\n\n");
    
    measure_child_divergence("t3_prose", prose, 0, 2000);
    measure_child_divergence("t3_binary", binary, 1, 2000);

    return 0;
}
