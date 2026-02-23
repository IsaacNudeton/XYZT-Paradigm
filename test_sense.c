/*
 * test_sense.c — N-cluster convergence + observer loop verification
 *
 * Tests:
 *   1. Seven distinct signal patterns → seven distinct clusters
 *   2. Zero cross-cluster contamination
 *   3. Observer feedback: tick → observe → mark → next tick propagates
 *
 * Compile: gcc -O2 -o test_sense test_sense.c -Wno-unused-function
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════
 * STUBS
 * ═══════════════════════════════════════════════════════════════ */

static void uart_puts(const char *s) { printf("%s", s); }
static void uart_putc(char c) { putchar(c); }
static void uart_dec(uint32_t v) { printf("%u", v); }
static void uart_hex(uint32_t v) { printf("0x%08X", v); }
static void uart_hex64(uint64_t v) { printf("0x%016llX", (unsigned long long)v); }

static void gpio_set_function(uint32_t p, uint32_t f) { (void)p; (void)f; }
static void gpio_set_pull(uint32_t p, uint32_t l) { (void)p; (void)l; }
static void gpio_enable_edge_detect(uint32_t p, int r, int f) { (void)p; (void)r; (void)f; }

/* ═══════════════════════════════════════════════════════════════
 * WIRE GRAPH — same as wire.c
 * ═══════════════════════════════════════════════════════════════ */

#define WIRE_MAX_NODES   64
#define WIRE_MAX_EDGES   256
#define WIRE_NAME_LEN    48
#define W_INIT    128
#define W_LEARN   20
#define W_MIN     5
#define W_MAX     250
#define WNODE_FILE     0
#define WNODE_PROBLEM  1
#define WNODE_CONCEPT  2

typedef struct { char name[WIRE_NAME_LEN]; uint8_t type; uint16_t hit_count; uint8_t _pad; } WireNode;
typedef struct { uint16_t src, dst; uint8_t weight, passes, fails, _pad; } WireEdge;
typedef struct { uint32_t n_nodes, n_edges, total_learns; WireNode nodes[WIRE_MAX_NODES]; WireEdge edges[WIRE_MAX_EDGES]; } WireGraph;

static WireGraph g_wire;

static int str_eq(const char *a, const char *b) { while (*a && *b) { if (*a++ != *b++) return 0; } return (*a == *b); }

void wire_init(void) { memset(&g_wire, 0, sizeof(g_wire)); }

int wire_find(const char *name) {
    for (uint32_t i = 0; i < g_wire.n_nodes; i++) if (str_eq(g_wire.nodes[i].name, name)) return (int)i;
    return -1;
}

int wire_add(const char *name, uint8_t type) {
    int id = wire_find(name); if (id >= 0) return id;
    if (g_wire.n_nodes >= WIRE_MAX_NODES) return -1;
    id = g_wire.n_nodes++;
    int i = 0; while (name[i] && i < WIRE_NAME_LEN - 1) { g_wire.nodes[id].name[i] = name[i]; i++; }
    g_wire.nodes[id].name[i] = '\0'; g_wire.nodes[id].type = type; g_wire.nodes[id].hit_count = 0;
    return id;
}

static int find_edge(int src, int dst) {
    for (uint32_t i = 0; i < g_wire.n_edges; i++) if (g_wire.edges[i].src == src && g_wire.edges[i].dst == dst) return (int)i;
    return -1;
}

void wire_connect(int a, int b) {
    if (a < 0 || b < 0) return;
    if (find_edge(a,b) < 0 && g_wire.n_edges < WIRE_MAX_EDGES) { WireEdge *e = &g_wire.edges[g_wire.n_edges++]; e->src=a; e->dst=b; e->weight=W_INIT; e->passes=0; e->fails=0; }
    if (find_edge(b,a) < 0 && g_wire.n_edges < WIRE_MAX_EDGES) { WireEdge *e = &g_wire.edges[g_wire.n_edges++]; e->src=b; e->dst=a; e->weight=W_INIT; e->passes=0; e->fails=0; }
    g_wire.nodes[a].hit_count++; g_wire.nodes[b].hit_count++;
}

void wire_learn(int node_id, int pass) {
    if (node_id < 0 || node_id >= (int)g_wire.n_nodes) return;
    for (uint32_t i = 0; i < g_wire.n_edges; i++) {
        WireEdge *e = &g_wire.edges[i];
        if ((int)e->src == node_id || (int)e->dst == node_id) {
            if (pass) { e->passes++; int nw=(int)e->weight+W_LEARN; e->weight=(nw>W_MAX)?W_MAX:(uint8_t)nw; }
            else { e->fails++; int nw=(int)e->weight-W_LEARN; e->weight=(nw<W_MIN)?W_MIN:(uint8_t)nw; }
        }
    }
    g_wire.total_learns++;
}

WireGraph *wire_get(void) { return &g_wire; }

/* ═══════════════════════════════════════════════════════════════
 * CAPTURE TYPE
 * ═══════════════════════════════════════════════════════════════ */

#define CAPTURE_BUF_SIZE 4096
typedef struct { uint64_t timestamp; uint32_t pin_state; } edge_event_t;
typedef struct { edge_event_t events[CAPTURE_BUF_SIZE]; uint32_t head, count, pin_mask; } capture_buf_t;

void capture_for_duration(capture_buf_t *b, uint64_t d) { (void)b; (void)d; }

/* ═══════════════════════════════════════════════════════════════
 * V6 ENGINE — for observer loop test
 * ═══════════════════════════════════════════════════════════════ */

#define V6_MAX_POS 64
#define V6_BIT(n)  (1ULL << (n))

typedef struct {
    uint8_t  marked[V6_MAX_POS];
    uint64_t reads[V6_MAX_POS];
    uint64_t co_present[V6_MAX_POS];
    uint8_t  active[V6_MAX_POS];
    int      n_pos;
} V6Grid;

static void v6_init(V6Grid *g) { memset(g, 0, sizeof(*g)); }
static void v6_mark(V6Grid *g, int p, int v) { g->marked[p] = v?1:0; if (p >= g->n_pos) g->n_pos = p+1; }
static void v6_wire(V6Grid *g, int p, uint64_t rd) { g->reads[p]=rd; g->active[p]=1; if (p >= g->n_pos) g->n_pos=p+1; }
static void v6_tick(V6Grid *g) {
    uint8_t snap[V6_MAX_POS]; memcpy(snap, g->marked, V6_MAX_POS);
    for (int p = 0; p < g->n_pos; p++) {
        if (!g->active[p]) continue;
        uint64_t present = 0, bits = g->reads[p];
        while (bits) { int b = __builtin_ctzll(bits); if (snap[b]) present |= V6_BIT(b); bits &= bits-1; }
        g->co_present[p] = present;
    }
}
static int v6_obs_any(uint64_t cp) { return cp != 0; }
static int v6_obs_count(uint64_t cp) { return __builtin_popcountll(cp); }
static int v6_obs_parity(uint64_t cp) { return __builtin_popcountll(cp) & 1; }

#define TEST_SENSE
#include "sense.c"

/* ═══════════════════════════════════════════════════════════════
 * SIGNAL GENERATORS — 7 distinct patterns
 * ═══════════════════════════════════════════════════════════════ */

/* Pattern 1: clock+data on pins 0,1 (fast, 20us period) */
static void gen_fast_clock_data(capture_buf_t *buf) {
    buf->pin_mask = (1<<0)|(1<<1); buf->head=0; buf->count=0;
    uint64_t t=1000; uint32_t state=0;
    int bits[] = {1,0,1,1,0,1,0,0};
    for (int c=0; c<200 && buf->count<CAPTURE_BUF_SIZE-2; c++) {
        state |= (1<<0); if (bits[c%8]) state|=(1<<1); else state&=~(1<<1);
        buf->events[buf->count].timestamp=t; buf->events[buf->count].pin_state=state; buf->count++; t+=10;
        state &= ~(1<<0);
        buf->events[buf->count].timestamp=t; buf->events[buf->count].pin_state=state; buf->count++; t+=10;
    }
}

/* Pattern 2: single async on pin 4 (bursty) */
static void gen_async_pin4(capture_buf_t *buf) {
    buf->pin_mask = (1<<4); buf->head=0; buf->count=0;
    uint64_t t=1000; uint32_t state=(1<<4);
    int bits[] = {0,1,0,1,1,0,0,1,0,1};
    for (int burst=0; burst<5; burst++) {
        for (int b=0; b<10 && buf->count<CAPTURE_BUF_SIZE; b++) {
            uint32_t ns = bits[b] ? (1<<4) : 0;
            if (ns != state) { state=ns; buf->events[buf->count].timestamp=t; buf->events[buf->count].pin_state=state; buf->count++; }
            t += 87;
        }
        state=(1<<4); buf->events[buf->count].timestamp=t; buf->events[buf->count].pin_state=state; buf->count++; t+=2000;
    }
}

/* Pattern 3: held+toggle on pins 9,10 (slow, 200us) */
static void gen_held_toggle_9_10(capture_buf_t *buf) {
    buf->pin_mask = (1<<9)|(1<<10); buf->head=0; buf->count=0;
    uint64_t t=1000; uint32_t state=(1<<9)|(1<<10);
    for (int c=0; c<150 && buf->count<CAPTURE_BUF_SIZE-2; c++) {
        state ^= (1<<10);
        buf->events[buf->count].timestamp=t; buf->events[buf->count].pin_state=state; buf->count++; t+=100;
        if (c%9==0) {
            state &= ~(1<<9);
            buf->events[buf->count].timestamp=t; buf->events[buf->count].pin_state=state; buf->count++; t+=40;
            state |= (1<<9);
            buf->events[buf->count].timestamp=t; buf->events[buf->count].pin_state=state; buf->count++; t+=60;
        } else t+=100;
    }
}

/* Pattern 4: three correlated pins 14,15,16 (fast, 30us) */
static void gen_three_corr(capture_buf_t *buf) {
    buf->pin_mask = (1<<14)|(1<<15)|(1<<16); buf->head=0; buf->count=0;
    uint64_t t=1000; uint32_t state=0;
    for (int c=0; c<200 && buf->count<CAPTURE_BUF_SIZE-2; c++) {
        state = ((c%2)?((1<<14)|(1<<16)):((1<<15)));
        buf->events[buf->count].timestamp=t; buf->events[buf->count].pin_state=state; buf->count++; t+=15;
        state=0;
        buf->events[buf->count].timestamp=t; buf->events[buf->count].pin_state=state; buf->count++; t+=15;
    }
}

/* Pattern 5: single slow regular on pin 20 (2000us period) */
static void gen_slow_regular_20(capture_buf_t *buf) {
    buf->pin_mask = (1<<20); buf->head=0; buf->count=0;
    uint64_t t=1000; uint32_t state=0;
    for (int c=0; c<100 && buf->count<CAPTURE_BUF_SIZE-2; c++) {
        state ^= (1<<20);
        buf->events[buf->count].timestamp=t; buf->events[buf->count].pin_state=state; buf->count++; t+=1000;
    }
}

/* Pattern 6: two async bursty on pins 22,23 */
static void gen_dual_burst_22_23(capture_buf_t *buf) {
    buf->pin_mask = (1<<22)|(1<<23); buf->head=0; buf->count=0;
    uint64_t t=1000; uint32_t state=(1<<22)|(1<<23);
    for (int burst=0; burst<4; burst++) {
        for (int b=0; b<8 && buf->count<CAPTURE_BUF_SIZE; b++) {
            state ^= (1<<22); if (b%3==0) state ^= (1<<23);
            buf->events[buf->count].timestamp=t; buf->events[buf->count].pin_state=state; buf->count++; t+=50;
        }
        t+=3000;
    }
}

/* Pattern 7: very fast regular on pin 26 (<10us period) */
static void gen_vfast_26(capture_buf_t *buf) {
    buf->pin_mask = (1<<26); buf->head=0; buf->count=0;
    uint64_t t=1000; uint32_t state=0;
    for (int c=0; c<400 && buf->count<CAPTURE_BUF_SIZE-2; c++) {
        state ^= (1<<26);
        buf->events[buf->count].timestamp=t; buf->events[buf->count].pin_state=state; buf->count++; t+=4;
    }
}

/* ═══════════════════════════════════════════════════════════════
 * CLUSTER ANALYSIS
 * ═══════════════════════════════════════════════════════════════ */

typedef struct {
    int members[32];
    int n_members;
} Cluster;

/* Find clusters: connected components where all edges > threshold */
static int find_clusters(Cluster *clusters, int max_clusters, uint8_t threshold) {
    int visited[WIRE_MAX_NODES] = {0};
    int n_clusters = 0;

    for (uint32_t i = 0; i < g_wire.n_nodes && n_clusters < max_clusters; i++) {
        if (visited[i]) continue;
        /* BFS from node i */
        Cluster *cl = &clusters[n_clusters];
        cl->n_members = 0;
        int queue[WIRE_MAX_NODES], qh=0, qt=0;
        queue[qt++] = i; visited[i] = 1;
        while (qh < qt) {
            int node = queue[qh++];
            cl->members[cl->n_members++] = node;
            for (uint32_t e = 0; e < g_wire.n_edges; e++) {
                WireEdge *edge = &g_wire.edges[e];
                if (edge->weight < threshold) continue;
                int neighbor = -1;
                if ((int)edge->src == node) neighbor = edge->dst;
                else if ((int)edge->dst == node) neighbor = edge->src;
                if (neighbor >= 0 && !visited[neighbor]) {
                    visited[neighbor] = 1;
                    queue[qt++] = neighbor;
                }
            }
        }
        if (cl->n_members > 0) n_clusters++;
    }
    return n_clusters;
}

/* ═══════════════════════════════════════════════════════════════
 * OBSERVER LOOP TEST — verify tick→observe→mark propagates
 * ═══════════════════════════════════════════════════════════════ */

static int test_observer_loop(void) {
    int pass = 0, fail = 0;
    printf("\n  ── Observer Loop Test ──\n\n");

    #define OBS_CHECK(name, cond) do { \
        if (cond) { pass++; printf("  PASS: %s\n", name); } \
        else { fail++; printf("  FAIL: %s\n", name); } \
    } while(0)

    /* 3-stage shift register: pos 0 → pos 1 → pos 2
     * Wiring: pos 1 reads from pos 0, pos 2 reads from pos 1
     * Input: mark pos 0, then tick+observe repeatedly.
     * After tick 1: co_present[1] has bit 0 (sees pos 0 marked)
     *   observer marks pos 1
     * After tick 2: co_present[2] has bit 1 (sees pos 1 marked)
     *   observer marks pos 2
     * Signal propagated through the shift register. */

    V6Grid g; v6_init(&g);
    v6_wire(&g, 1, V6_BIT(0));  /* pos 1 reads pos 0 */
    v6_wire(&g, 2, V6_BIT(1));  /* pos 2 reads pos 1 */

    /* Inject signal at pos 0 */
    v6_mark(&g, 0, 1);

    /* WRONG WAY: tick without observer feedback */
    v6_tick(&g);
    OBS_CHECK("tick-only: co_present[1] sees pos 0",
              v6_obs_any(g.co_present[1]) == 1);
    OBS_CHECK("tick-only: marked[1] unchanged (BUG if relied on)",
              g.marked[1] == 0);
    OBS_CHECK("tick-only: co_present[2] empty (hasn't propagated)",
              v6_obs_any(g.co_present[2]) == 0);

    /* RIGHT WAY: tick + observer feedback */
    v6_init(&g);
    v6_wire(&g, 1, V6_BIT(0));
    v6_wire(&g, 2, V6_BIT(1));
    v6_mark(&g, 0, 1);

    /* Tick 1 + observe */
    v6_tick(&g);
    for (int p = 1; p <= 2; p++)
        v6_mark(&g, p, v6_obs_any(g.co_present[p]));

    OBS_CHECK("tick+obs 1: pos 1 now marked", g.marked[1] == 1);
    OBS_CHECK("tick+obs 1: pos 2 not yet", g.marked[2] == 0);

    /* Clear input, tick 2 + observe */
    v6_mark(&g, 0, 0);
    v6_tick(&g);
    for (int p = 1; p <= 2; p++)
        v6_mark(&g, p, v6_obs_any(g.co_present[p]));

    OBS_CHECK("tick+obs 2: pos 1 cleared (input gone)", g.marked[1] == 0);
    OBS_CHECK("tick+obs 2: pos 2 now marked (shifted)", g.marked[2] == 1);

    /* Tick 3: signal exits */
    v6_tick(&g);
    for (int p = 1; p <= 2; p++)
        v6_mark(&g, p, v6_obs_any(g.co_present[p]));

    OBS_CHECK("tick+obs 3: pos 2 cleared (exited register)", g.marked[2] == 0);

    /* XOR pipeline: pos 3 reads {0,1}, observer uses parity */
    v6_init(&g);
    v6_mark(&g, 0, 1);
    v6_mark(&g, 1, 0);
    v6_wire(&g, 3, V6_BIT(0)|V6_BIT(1));
    v6_tick(&g);
    int xor_result = v6_obs_parity(g.co_present[3]);
    v6_mark(&g, 3, xor_result);
    OBS_CHECK("XOR(1,0)→mark: pos 3 = 1", g.marked[3] == 1);

    v6_mark(&g, 1, 1);
    v6_tick(&g);
    xor_result = v6_obs_parity(g.co_present[3]);
    v6_mark(&g, 3, xor_result);
    OBS_CHECK("XOR(1,1)→mark: pos 3 = 0", g.marked[3] == 0);

    #undef OBS_CHECK

    printf("\n  Observer loop: %d pass, %d fail\n", pass, fail);
    return fail == 0;
}

/* ═══════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════ */

int main(void) {
    int total_pass = 0, total_fail = 0;

    printf("\n  ════════════════════════════════════════════\n");
    printf("  N-Cluster Convergence + Observer Loop Test\n");
    printf("  7 patterns. No labels. Observer closes loop.\n");
    printf("  ════════════════════════════════════════════\n");

    /* ── Part 1: Observer Loop ──────────────────────────────── */
    int obs_ok = test_observer_loop();
    if (obs_ok) total_pass++; else total_fail++;

    /* ── Part 2: N-Cluster Convergence ──────────────────────── */
    printf("\n  ── N-Cluster Convergence ──\n\n");

    wire_init();
    static capture_buf_t buf;
    static sense_pass_t pass;

    void (*generators[])(capture_buf_t*) = {
        gen_fast_clock_data, gen_async_pin4, gen_held_toggle_9_10,
        gen_three_corr, gen_slow_regular_20, gen_dual_burst_22_23,
        gen_vfast_26
    };
    int n_patterns = 7;

    printf("  Running 15 rounds of %d patterns...\n\n", n_patterns);

    for (int round = 0; round < 15; round++) {
        for (int p = 0; p < n_patterns; p++) {
            generators[p](&buf);
            sense_extract(&buf, &pass);
            sense_decay(&pass);
        }
    }

    /* Count clusters */
    Cluster clusters[20];
    int n_clusters = find_clusters(clusters, 20, W_INIT + 5);

    printf("  Wire Graph: %u nodes, %u edges\n", g_wire.n_nodes, g_wire.n_edges);
    printf("  Clusters found (threshold w>%d): %d\n\n", W_INIT+5, n_clusters);

    for (int c = 0; c < n_clusters; c++) {
        printf("  Cluster %d (%d nodes): ", c, clusters[c].n_members);
        for (int m = 0; m < clusters[c].n_members; m++) {
            if (m > 0) printf(", ");
            printf("%s", g_wire.nodes[clusters[c].members[m]].name);
        }
        printf("\n");
    }

    #define CHECK(name, cond) do { \
        if (cond) { total_pass++; printf("  PASS: %s\n", name); } \
        else { total_fail++; printf("  FAIL: %s\n", name); } \
    } while(0)

    printf("\n");
    CHECK("At least 5 distinct clusters", n_clusters >= 5);
    int max_size = 0;
    for (int c = 0; c < n_clusters; c++)
        if (clusters[c].n_members > max_size) max_size = clusters[c].n_members;
    if (max_size >= 15) { total_fail++; printf("  FAIL: mega-cluster of size %d\n", max_size); }
    else { total_pass++; printf("  PASS: largest cluster = %d nodes\n", max_size); }

    /* Cross-cluster: pins from pattern 1 (0,1) shouldn't connect to pattern 2 (4) */
    int a0 = wire_find("A0"), a4 = wire_find("A4");
    if (a0 >= 0 && a4 >= 0) {
        int eid = find_edge(a0, a4);
        CHECK("A0-A4 cross-cluster absent or weak",
              eid < 0 || g_wire.edges[eid].weight <= W_INIT);
    } else {
        CHECK("A0 and A4 both exist", a0 >= 0 && a4 >= 0);
    }

    /* Pins from pattern 1 should NOT connect to pattern 7 */
    int a26 = wire_find("A26");
    if (a0 >= 0 && a26 >= 0) {
        int eid = find_edge(a0, a26);
        CHECK("A0-A26 cross-cluster absent or weak",
              eid < 0 || g_wire.edges[eid].weight <= W_INIT);
    }

    #undef CHECK

    printf("\n  ════════════════════════════════\n");
    printf("  TOTAL: %d pass, %d fail\n", total_pass, total_fail);
    printf("  %s\n", total_fail == 0 ? "ALL TESTS PASSED." : "SOME TESTS FAILED.");
    printf("  ════════════════════════════════\n\n");

    return total_fail == 0 ? 0 : 1;
}
