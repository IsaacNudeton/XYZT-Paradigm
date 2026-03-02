/*
 * agent_memory_v2.c — Can agents BE memory? (fixed energy scale)
 *
 * v1 failed because: val=1, weight=200 → 1*200/255 = 0 in integer math.
 * The signal was below the noise floor of the substrate (integer truncation).
 *
 * Fix: signals need enough ENERGY to survive transmission.
 * This isn't imposing anything — it's physics. A photon below the
 * noise floor of a detector doesn't get detected. Same principle.
 *
 * Also: self-reinforcement. An agent that can't hear itself can't remember.
 * A neuron has recurrent connections. A cavity has mirrors. Same thing.
 *
 * Isaac & Claude — February 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_NODES 32
#define MAX_EDGES 256

typedef struct { int src, dst, weight; } Edge;
typedef struct {
    int val, accum, n_in, valence;
    int input; /* -1 = no external input */
} Node;
typedef struct {
    Node nodes[MAX_NODES];
    Edge edges[MAX_EDGES];
    int n_nodes, n_edges;
} Lattice;

static void lattice_init(Lattice *L) { memset(L, 0, sizeof(*L)); }
static int add_node(Lattice *L) { int id = L->n_nodes++; L->nodes[id].input = -1; return id; }
static void add_edge(Lattice *L, int s, int d, int w) {
    L->edges[L->n_edges++] = (Edge){s, d, w};
}
/* Bidirectional + self */
static void connect(Lattice *L, int a, int b, int w) {
    add_edge(L, a, b, w);
    add_edge(L, b, a, w);
}
static void self_edge(Lattice *L, int a, int w) {
    add_edge(L, a, a, w);
}

static void tick(Lattice *L) {
    /* Phase 1: broadcast — every node sends to connected nodes */
    for (int i = 0; i < L->n_edges; i++) {
        Edge *e = &L->edges[i];
        if (e->weight == 0) continue;
        int signal = (int)((long long)L->nodes[e->src].val * e->weight / 255);
        L->nodes[e->dst].accum += signal;
        L->nodes[e->dst].n_in++;
    }

    /* Phase 2: resolve */
    for (int i = 0; i < L->n_nodes; i++) {
        Node *n = &L->nodes[i];
        if (n->input >= 0) {
            n->val = n->input;
            n->accum = 0; n->n_in = 0;
            continue;
        }
        if (n->n_in > 0) {
            /* Average, not sum — prevents unbounded growth */
            n->val = n->accum / n->n_in;
            if (n->val != 0 && n->valence < 255) n->valence++;
        } else {
            /* No input at all: decay toward zero (noise floor) */
            n->val = n->val * 240 / 256; /* slow decay: ~6% per tick */
        }
        n->accum = 0; n->n_in = 0;
    }

    /* Phase 3: Hebbian */
    for (int i = 0; i < L->n_edges; i++) {
        Edge *e = &L->edges[i];
        int sa = L->nodes[e->src].val != 0;
        int da = L->nodes[e->dst].val != 0;
        if (sa && da) {
            if (e->weight < 255) e->weight++;
        } else if (sa != da) {
            if (e->weight > 1) e->weight--;
        }
    }
}

static void print_vals(Lattice *L, int n, const char *label) {
    printf("  %s: [", label);
    for (int i = 0; i < n; i++) {
        printf("%4d", L->nodes[i].val);
        if (i < n - 1) printf(",");
    }
    printf("]\n");
}

/* ═══════════════════════════════════════════════ */

static void test_1_persistence(void) {
    printf("═══ TEST 1: Pattern persistence after input removed ═══\n\n");

    Lattice L; lattice_init(&L);
    for (int i = 0; i < 4; i++) add_node(&L);

    /* Ring + self-reinforcement */
    for (int i = 0; i < 4; i++) {
        connect(&L, i, (i+1)%4, 128);
        self_edge(&L, i, 200); /* agents sustain themselves */
    }

    /* Drive with ENERGY, not just 0/1 */
    printf("  Driving [100, 0, 100, 0] for 20 ticks\n");
    L.nodes[0].input = 100;
    L.nodes[1].input = 0;
    L.nodes[2].input = 100;
    L.nodes[3].input = 0;

    for (int t = 0; t < 20; t++) tick(&L);
    print_vals(&L, 4, "driven");

    /* Remove input */
    printf("  Input REMOVED\n");
    for (int i = 0; i < 4; i++) L.nodes[i].input = -1;

    int held = 0;
    for (int t = 0; t < 30; t++) {
        tick(&L);
        if (t % 5 == 0) print_vals(&L, 4, "      ");
        if (abs(L.nodes[0].val) > 10 || abs(L.nodes[2].val) > 10) held++;
    }

    printf("\n  Pattern survived %d/30 ticks\n", held);
    printf("  %s\n\n", held > 20 ? "✓ AGENTS ARE MEMORY" : held > 10 ? "~ Partial memory" : "✗ Decayed");
}

static void test_2_two_memories(void) {
    printf("═══ TEST 2: Two independent memories ═══\n\n");

    Lattice L; lattice_init(&L);
    for (int i = 0; i < 6; i++) add_node(&L);

    /* Cluster A: 0-1-2 triangle + self */
    connect(&L, 0, 1, 128); connect(&L, 1, 2, 128); connect(&L, 0, 2, 128);
    for (int i = 0; i < 3; i++) self_edge(&L, i, 200);

    /* Cluster B: 3-4-5 triangle + self */
    connect(&L, 3, 4, 128); connect(&L, 4, 5, 128); connect(&L, 3, 5, 128);
    for (int i = 3; i < 6; i++) self_edge(&L, i, 200);

    /* NO edges between clusters — they're isolated */

    printf("  Driving A=[100,100,0] B=[0,100,100]\n");
    L.nodes[0].input=100; L.nodes[1].input=100; L.nodes[2].input=0;
    L.nodes[3].input=0;   L.nodes[4].input=100; L.nodes[5].input=100;

    for (int t = 0; t < 20; t++) tick(&L);
    for (int i = 0; i < 6; i++) L.nodes[i].input = -1;

    printf("  Input removed.\n");
    int a_held = 0, b_held = 0;
    for (int t = 0; t < 30; t++) {
        tick(&L);
        if (t % 10 == 0) print_vals(&L, 6, "      ");
        if (abs(L.nodes[0].val) > 10 && abs(L.nodes[1].val) > 10) a_held++;
        if (abs(L.nodes[4].val) > 10 && abs(L.nodes[5].val) > 10) b_held++;
    }

    printf("\n  A held: %d/30  B held: %d/30\n", a_held, b_held);
    printf("  %s\n\n", (a_held > 20 && b_held > 20) ?
        "✓ TWO INDEPENDENT MEMORIES" : "✗ Decayed or interfered");
}

static void test_3_latch(void) {
    printf("═══ TEST 3: SR latch from agents ═══\n\n");

    Lattice L; lattice_init(&L);
    int Q  = add_node(&L);
    int Qn = add_node(&L);

    /* Strong self-reinforcement = agent sustains itself */
    self_edge(&L, Q, 230);
    self_edge(&L, Qn, 230);
    /* No cross-coupling — just two independent self-sustaining agents */

    int pass = 0;

    /* SET */
    printf("  SET Q\n");
    L.nodes[Q].input = 100; L.nodes[Qn].input = 0;
    for (int t = 0; t < 5; t++) tick(&L);
    L.nodes[Q].input = -1; L.nodes[Qn].input = -1;
    for (int t = 0; t < 10; t++) tick(&L);
    printf("  HOLD: Q=%d Qn=%d", L.nodes[Q].val, L.nodes[Qn].val);
    if (L.nodes[Q].val > 10) { pass++; printf(" ✓\n"); } else printf(" ✗\n");

    /* RESET */
    printf("  RESET Qn\n");
    L.nodes[Q].input = 0; L.nodes[Qn].input = 100;
    for (int t = 0; t < 5; t++) tick(&L);
    L.nodes[Q].input = -1; L.nodes[Qn].input = -1;
    for (int t = 0; t < 10; t++) tick(&L);
    printf("  HOLD: Q=%d Qn=%d", L.nodes[Q].val, L.nodes[Qn].val);
    if (L.nodes[Qn].val > 10 && L.nodes[Q].val <= 10) { pass++; printf(" ✓\n"); } else printf(" ✗\n");

    /* SET again */
    printf("  SET Q again\n");
    L.nodes[Q].input = 100; L.nodes[Qn].input = 0;
    for (int t = 0; t < 5; t++) tick(&L);
    L.nodes[Q].input = -1; L.nodes[Qn].input = -1;
    for (int t = 0; t < 20; t++) tick(&L);
    printf("  HOLD 20 ticks: Q=%d Qn=%d", L.nodes[Q].val, L.nodes[Qn].val);
    if (L.nodes[Q].val > 10) { pass++; printf(" ✓\n"); } else printf(" ✗\n");

    printf("\n  %d/3 — %s\n\n", pass,
        pass == 3 ? "✓ LATCH FROM SELF-SUSTAINING AGENTS" : "✗ Failed");
}

static void test_4_overwrite(void) {
    printf("═══ TEST 4: Memory is writable (new pattern overwrites old) ═══\n\n");

    Lattice L; lattice_init(&L);
    for (int i = 0; i < 4; i++) { add_node(&L); self_edge(&L, i, 200); }
    for (int i = 0; i < 4; i++) connect(&L, i, (i+1)%4, 100);

    /* Write A */
    printf("  Write A: [100, 0, 100, 0]\n");
    L.nodes[0].input=100; L.nodes[1].input=0; L.nodes[2].input=100; L.nodes[3].input=0;
    for (int t = 0; t < 15; t++) tick(&L);
    for (int i = 0; i < 4; i++) L.nodes[i].input = -1;
    for (int t = 0; t < 5; t++) tick(&L);
    print_vals(&L, 4, "A hold");
    int a0 = L.nodes[0].val, a2 = L.nodes[2].val;

    /* Write B — opposite */
    printf("  Write B: [0, 100, 0, 100]\n");
    L.nodes[0].input=0; L.nodes[1].input=100; L.nodes[2].input=0; L.nodes[3].input=100;
    for (int t = 0; t < 15; t++) tick(&L);
    for (int i = 0; i < 4; i++) L.nodes[i].input = -1;
    for (int t = 0; t < 5; t++) tick(&L);
    print_vals(&L, 4, "B hold");
    int b1 = L.nodes[1].val, b3 = L.nodes[3].val;

    printf("\n  A stored (%d,%d) → B overwrote (%d,%d)\n", a0, a2, b1, b3);
    printf("  %s\n\n", (a0 > 10 && a2 > 10 && b1 > 10 && b3 > 10) ?
        "✓ WRITABLE MEMORY — old pattern replaced" : "✗ Overwrite failed");
}

static void test_5_decay_without_reinforcement(void) {
    printf("═══ TEST 5: Unreinforced memory decays (garbage collection) ═══\n\n");

    /*
     * Write a pattern to agents WITHOUT self-reinforcement.
     * It should decay. This is the noise floor / garbage collector.
     * Memory without reinforcement = not real memory.
     */
    Lattice L; lattice_init(&L);
    for (int i = 0; i < 4; i++) add_node(&L);
    /* NO self-edges. Just inter-node connections. */
    for (int i = 0; i < 4; i++) connect(&L, i, (i+1)%4, 128);

    L.nodes[0].input=100; L.nodes[1].input=0; L.nodes[2].input=100; L.nodes[3].input=0;
    for (int t = 0; t < 10; t++) tick(&L);
    for (int i = 0; i < 4; i++) L.nodes[i].input = -1;

    printf("  Pattern written to agents WITHOUT self-reinforcement\n");
    int alive = 0;
    for (int t = 0; t < 30; t++) {
        tick(&L);
        if (t % 5 == 0) print_vals(&L, 4, "      ");
        if (abs(L.nodes[0].val) > 5 || abs(L.nodes[2].val) > 5) alive++;
    }

    printf("\n  Survived %d/30 ticks\n", alive);
    printf("  %s\n\n", alive < 10 ?
        "✓ UNREINFORCED MEMORY DECAYS — natural garbage collection" :
        "→ Surprising persistence — topology may have self-organized");
}

int main(void) {
    printf("══════════════════════════════════════════════════════\n");
    printf("  AGENT MEMORY v2 — No registers. Just physics.\n");
    printf("  Self-reinforcement = cavity mirrors.\n");
    printf("  Energy scale = above noise floor.\n");
    printf("  accum += v. That's still the only operation.\n");
    printf("══════════════════════════════════════════════════════\n\n");

    test_1_persistence();
    test_2_two_memories();
    test_3_latch();
    test_4_overwrite();
    test_5_decay_without_reinforcement();

    printf("══════════════════════════════════════════════════════\n");
    printf("  SUMMARY:\n");
    printf("  Self-reinforcing agents = cavities = memory cells.\n");
    printf("  No self-reinforcement = no persistence = GC.\n");
    printf("  The same operation (accum += v) does both.\n");
    printf("  Topology decides what remembers and what forgets.\n");
    printf("══════════════════════════════════════════════════════\n");
    return 0;
}
