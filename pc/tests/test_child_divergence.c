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

void measure_child_divergence(const char* scenario_name, const uint8_t* binary_data, int binary_len, int ticks) {
    Engine eng;
    engine_init(&eng);

    printf("Running scenario: %-15s | Ticks: %d\n", scenario_name, ticks);

    /* 1. Ingest Data - multiple nodes to trigger nesting */
    if (binary_data != NULL) {
        /* Ingest binary as multiple separate nodes to build valence */
        printf("  Ingesting %d binary nodes (len=%d)...\n", 20, binary_len);
        fflush(stdout);
        for (int i = 0; i < 20; i++) {
            BitStream bs;
            encode_bytes(&bs, binary_data, binary_len);
            char name[64];
            snprintf(name, sizeof(name), "%s_chunk%d", scenario_name, i);
            engine_ingest(&eng, name, &bs);
        }
        printf("  Done ingesting binary nodes.\n");
        fflush(stdout);
    } else {
        /* Ingest prose as multiple separate nodes */
        const char* sentences[] = {
            "The quick brown fox jumps over the lazy dog.",
            "The dog wakes up and barks at the fox.",
            "This is a highly structured repeating sequence.",
            "English prose has predictable patterns.",
            "Words follow grammatical rules consistently.",
            "Sentences build upon previous knowledge.",
            "Language structures thought itself.",
            "Meaning emerges from word combinations.",
            "Syntax guides semantic interpretation.",
            "Pragmatics shapes contextual understanding.",
            "The fox is quick and brown.",
            "The dog is lazy but alert.",
            "Jumping is an action verb.",
            "Barking communicates alarm or greeting.",
            "Animals exhibit characteristic behaviors.",
            "Nature follows observable patterns.",
            "Science studies natural phenomena.",
            "Knowledge builds on prior foundations.",
            "Learning requires active engagement.",
            "Understanding emerges from practice."
        };
        for (int i = 0; i < 20; i++) {
            engine_ingest_text(&eng, sentences[i], sentences[i]);
        }
    }

    printf("  Ingested %d nodes, starting ticks...\n", eng.shells[0].g.n_nodes);

    /* 2. Tick to force S1 Nesting and Child Hebbian Learning */
    for (int i = 0; i < ticks; i++) {
        engine_tick(&eng);
        
        /* Print progress every 500 ticks */
        if ((i + 1) % 500 == 0) {
            printf("  Tick %d: nodes=%d, edges=%d, shells=%d\n", 
                   i+1, eng.shells[0].g.n_nodes, eng.shells[0].g.n_edges, eng.n_shells);
        }
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
        printf("  -> No child spawned (n_shells=%d). Check valence thresholds.\n\n", eng.n_shells);
    }

    engine_destroy(&eng);
}

int main() {
    printf("=== RUNNING DIVERGENCE SWEEP ===\n\n");
    
    /* Highly structured, repeating linguistic pattern */
    measure_child_divergence("t3_prose", NULL, 0, 2000);
    
    printf("\n--- Starting binary scenario ---\n\n");
    fflush(stdout);
    
    /* High-entropy, erratic data (fixed length, no null bytes) */
    const uint8_t binary[] = {
        0x41, 0xFF, 0x80, 0x40, 0x20, 0x10, 0x08, 0x04, 
        0x02, 0x01, 0x55, 0xAA, 0x55, 0xAA, 0xFF, 0x55, 
        0xFF, 0x55, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 
        0xDE, 0xF0, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 
        0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCE, 0xDA, 0xED, 
        0xFE, 0xBF, 0xAF, 0xCF, 0xDF, 0xEF, 0xFF, 0x0F, 
        0x1E, 0x2D, 0x3C, 0x4B, 0x5A, 0x69, 0x78, 0x87
    };
    measure_child_divergence("t3_binary", binary, sizeof(binary), 2000);

    return 0;
}
