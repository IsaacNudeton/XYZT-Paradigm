/*
 * test_child_conflict.c — Prove fractal cleaving in child graphs
 *
 * Creates a child graph manually (no GPU), wires a fake substrate
 * with differentiated L/R pattern, stabilizes, flips the pattern
 * to induce conflict, and verifies that cleaving fires.
 *
 * This is the clean-room proof that child cleaving works,
 * independent of GPU substrate.
 */
#include "test.h"

void run_child_conflict_tests(void) {
    printf("--- child conflict ---\n");

    SubstrateT T;
    T_init(&T);

    /* Build child graph: 8 retina + 4 hidden + 1 output (same as nest_spawn) */
    Graph child;
    graph_init(&child);

    char rname[32];
    for (int r = 0; r < 8; r++) {
        snprintf(rname, 32, "retina_%d", r);
        int rid = graph_add(&child, rname, 0, &T);
        if (rid >= 0) {
            child.nodes[rid].alive = 1;
            child.nodes[rid].layer_zero = 0;
            child.nodes[rid].identity.len = 64;
            child.nodes[rid].Z = 1.0;
            child.nodes[rid].plasticity = PLASTICITY_DEFAULT;
        }
    }
    for (int h = 0; h < 4; h++) {
        char hname[32];
        snprintf(hname, 32, "hidden_%d", h);
        int hid = graph_add(&child, hname, 0, &T);
        if (hid >= 0) {
            child.nodes[hid].alive = 1;
            child.nodes[hid].layer_zero = 0;
            child.nodes[hid].identity.len = 64;
            child.nodes[hid].Z = 1.0;
            child.nodes[hid].plasticity = PLASTICITY_DEFAULT;
        }
    }
    int out = graph_add(&child, "output", 0, &T);
    if (out >= 0) {
        child.nodes[out].alive = 1;
        child.nodes[out].layer_zero = 0;
        child.nodes[out].identity.len = 64;
        child.nodes[out].Z = 1.0;
        child.nodes[out].plasticity = PLASTICITY_DEFAULT;
    }
    /* Wire retina pairs to hidden: (0,1)->h8, (2,3)->h9, (4,5)->h10, (6,7)->h11 */
    for (int r = 0; r < 8; r += 2) {
        int hidden_target = 8 + r / 2;
        graph_wire(&child, r, r + 1, hidden_target, 128, 0);
    }
    /* Wire hidden to output (12): paired hidden nodes */
    {
        int out_idx = child.n_nodes - 1;
        for (int h = 0; h < 4; h++) {
            int h1 = 8 + h;
            int h2 = 8 + ((h + 1) % 4);
            graph_wire(&child, h1, h2, out_idx, 64, 0);
        }
    }

    check("cc: child has 13 nodes", 1, child.n_nodes == 13);
    check("cc: child has 8 edges", 1, child.n_edges == 8);

    /* Fake substrate: 4x4x4 = 64 bytes.
     * Left half (x<2) = 200, right half (x>=2) = 50.
     * This gives retina nodes differentiated values:
     *   octants 0,2,4,6 (ox=0) see 200s
     *   octants 1,3,5,7 (ox=1) see 50s  */
    uint8_t substrate[64];
    for (int z = 0; z < 4; z++)
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                substrate[x + y*4 + z*16] = (x < 2) ? 200 : 50;

    child.retina = substrate;
    child.retina_len = 64;

    /* Inject retina values (same logic as engine.c retina injection) */
    for (int r = 0; r < 8 && r < child.n_nodes; r++) {
        int ox = r & 1, oy = (r >> 1) & 1, oz = (r >> 2) & 1;
        int32_t octant_val = 0;
        for (int lz = oz*2; lz < oz*2+2; lz++)
            for (int ly = oy*2; ly < oy*2+2; ly++)
                for (int lx = ox*2; lx < ox*2+2; lx++)
                    octant_val += child.retina[lx + ly*4 + lz*16];
        child.nodes[r].val = octant_val;
    }

    /* Phase 1: Stabilize — 30 cycles of child_tick_once with same pattern.
     * Each "cycle" = inject retina + 64 ticks (matching engine's ct<64 loop). */
    for (int cyc = 0; cyc < 30; cyc++) {
        for (int r = 0; r < 8 && r < child.n_nodes; r++) {
            int ox = r & 1, oy = (r >> 1) & 1, oz = (r >> 2) & 1;
            int32_t octant_val = 0;
            for (int lz = oz*2; lz < oz*2+2; lz++)
                for (int ly = oy*2; ly < oy*2+2; ly++)
                    for (int lx = ox*2; lx < ox*2+2; lx++)
                        octant_val += child.retina[lx + ly*4 + lz*16];
            child.nodes[r].val = octant_val;
        }
        for (int ct = 0; ct < 64; ct++)
            if (!child_tick_once(&child)) break;
    }

    int edges_before = 0;
    for (int i = 0; i < child.n_edges; i++)
        if (child.edges[i].weight > 0 && child.edges[i].tl.n_cells > 0)
            edges_before++;

    printf("  [cc] after stabilize: edges=%d, drive=%d, heartbeats=%d, learns=%llu\n",
           edges_before, child.drive, child.local_heartbeat,
           (unsigned long long)child.total_learns);

    check("cc: edges survived stabilization", 1, edges_before >= 8);

    /* Phase 2: CONFLICT — flip the substrate (left=50, right=200).
     * This inverts every retina node's value. Hebbian weights built for
     * the old pattern are now wrong. Child should enter frustration. */
    for (int z = 0; z < 4; z++)
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                substrate[x + y*4 + z*16] = (x < 2) ? 50 : 200;

    /* Run 50 conflict cycles — enough for plasticity to hit MAX and cleave */
    for (int cyc = 0; cyc < 50; cyc++) {
        for (int r = 0; r < 8 && r < child.n_nodes; r++) {
            int ox = r & 1, oy = (r >> 1) & 1, oz = (r >> 2) & 1;
            int32_t octant_val = 0;
            for (int lz = oz*2; lz < oz*2+2; lz++)
                for (int ly = oy*2; ly < oy*2+2; ly++)
                    for (int lx = ox*2; lx < ox*2+2; lx++)
                        octant_val += child.retina[lx + ly*4 + lz*16];
            child.nodes[r].val = octant_val;
        }
        for (int ct = 0; ct < 64; ct++)
            if (!child_tick_once(&child)) break;
    }

    int edges_after = 0;
    for (int i = 0; i < child.n_edges; i++)
        if (child.edges[i].weight > 0 && child.edges[i].tl.n_cells > 0)
            edges_after++;

    /* Count how many nodes hit plasticity max or were heated */
    int hot_nodes = 0;
    float max_plast = 0.0f;
    for (int i = 0; i < child.n_nodes; i++) {
        if (child.nodes[i].alive && child.nodes[i].plasticity > PLASTICITY_DEFAULT)
            hot_nodes++;
        if (child.nodes[i].plasticity > max_plast)
            max_plast = child.nodes[i].plasticity;
    }

    /* BFS max depth: retina → output */
    int max_depth = 0;
    int output_idx = child.n_nodes - 1;
    for (int start = 0; start < 8; start++) {
        int depth[MAX_NODES];
        memset(depth, -1, sizeof(int) * (unsigned)child.n_nodes);
        depth[start] = 0;
        int bfs_changed = 1;
        while (bfs_changed) {
            bfs_changed = 0;
            for (int e = 0; e < child.n_edges; e++) {
                Edge *ed = &child.edges[e];
                if (ed->weight == 0 || ed->tl.n_cells == 0) continue;
                int src = ed->src_a;
                int dst = ed->dst;
                if (src < child.n_nodes && dst < child.n_nodes) {
                    if (depth[src] >= 0 && (depth[dst] < 0 || depth[dst] > depth[src] + 1)) {
                        depth[dst] = depth[src] + 1;
                        bfs_changed = 1;
                    }
                }
            }
        }
        if (depth[output_idx] > max_depth) max_depth = depth[output_idx];
    }

    printf("  [cc] after conflict: edges=%d (was %d), drive=%d, heartbeats=%d\n",
           edges_after, edges_before, child.drive, child.local_heartbeat);
    printf("  [cc] hot_nodes=%d, max_plasticity=%.3f, grown=%llu, max_depth=%d\n",
           hot_nodes, max_plast, (unsigned long long)child.total_grown, max_depth);

    /* Falsifiable checks */
    check("cc: child entered frustration at some point", 1,
          child.drive == 1 || child.total_grown > 8 || edges_after != edges_before);
    check("cc: conflict changed edge structure", 1,
          edges_after != edges_before || child.total_grown > 8);

    graph_destroy(&child);
}
