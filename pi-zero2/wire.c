/*
 * wire.c — Hebbian Graph (bare metal)
 *
 * Learning happens as SIDE EFFECT of normal system use.
 * Verify pass → strengthen edges. Fail → weaken.
 * Connect concepts → edges form.
 *
 * The loop closes itself.
 *
 * Isaac Oravec & Claude — February 2026
 */

#include "xyzt_os.h"

/* Single static graph */
static WireGraph g_wire;

static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return (*a == *b);
}

static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

void wire_init(void) {
    g_wire.n_nodes = 0;
    g_wire.n_edges = 0;
    g_wire.total_learns = 0;
    for (int i = 0; i < WIRE_MAX_NODES; i++) {
        g_wire.nodes[i].name[0] = '\0';
        g_wire.nodes[i].type = 0;
        g_wire.nodes[i].hit_count = 0;
    }
    for (int i = 0; i < WIRE_MAX_EDGES; i++) {
        g_wire.edges[i].weight = 0;
        g_wire.edges[i].passes = 0;
        g_wire.edges[i].fails = 0;
    }
}

int wire_find(const char *name) {
    for (uint32_t i = 0; i < g_wire.n_nodes; i++)
        if (str_eq(g_wire.nodes[i].name, name)) return (int)i;
    return -1;
}

int wire_add(const char *name, uint8_t type) {
    int id = wire_find(name);
    if (id >= 0) return id;
    if (g_wire.n_nodes >= WIRE_MAX_NODES) return -1;
    id = g_wire.n_nodes++;
    str_copy(g_wire.nodes[id].name, name, WIRE_NAME_LEN);
    g_wire.nodes[id].type = type;
    g_wire.nodes[id].hit_count = 0;
    return id;
}

static int find_edge(int src, int dst) {
    for (uint32_t i = 0; i < g_wire.n_edges; i++)
        if (g_wire.edges[i].src == src && g_wire.edges[i].dst == dst)
            return (int)i;
    return -1;
}

void wire_connect(int a, int b) {
    if (a < 0 || b < 0) return;
    /* a → b */
    if (find_edge(a, b) < 0 && g_wire.n_edges < WIRE_MAX_EDGES) {
        WireEdge *e = &g_wire.edges[g_wire.n_edges++];
        e->src = a; e->dst = b; e->weight = W_INIT;
        e->passes = 0; e->fails = 0;
    }
    /* b → a */
    if (find_edge(b, a) < 0 && g_wire.n_edges < WIRE_MAX_EDGES) {
        WireEdge *e = &g_wire.edges[g_wire.n_edges++];
        e->src = b; e->dst = a; e->weight = W_INIT;
        e->passes = 0; e->fails = 0;
    }
    g_wire.nodes[a].hit_count++;
    g_wire.nodes[b].hit_count++;
}

void wire_learn(int node_id, int pass) {
    if (node_id < 0 || node_id >= (int)g_wire.n_nodes) return;
    for (uint32_t i = 0; i < g_wire.n_edges; i++) {
        WireEdge *e = &g_wire.edges[i];
        if ((int)e->src == node_id || (int)e->dst == node_id) {
            if (pass) {
                e->passes++;
                int nw = (int)e->weight + W_LEARN;
                e->weight = (nw > W_MAX) ? W_MAX : (uint8_t)nw;
            } else {
                e->fails++;
                int nw = (int)e->weight - W_LEARN;
                e->weight = (nw < W_MIN) ? W_MIN : (uint8_t)nw;
            }
        }
    }
    g_wire.total_learns++;
}

int wire_weight(int src, int dst) {
    int eid = find_edge(src, dst);
    if (eid < 0) return 0;
    return g_wire.edges[eid].weight;
}

void wire_print(void) {
    uart_puts("  Wire: ");
    uart_dec(g_wire.n_nodes); uart_puts(" nodes, ");
    uart_dec(g_wire.n_edges); uart_puts(" edges, ");
    uart_dec(g_wire.total_learns); uart_puts(" learns\n");

    if (g_wire.n_nodes > 0) {
        for (uint32_t i = 0; i < g_wire.n_nodes; i++) {
            WireNode *n = &g_wire.nodes[i];
            uart_puts("    ["); uart_dec(i); uart_puts("] ");
            const char *tag = n->type == WNODE_PROBLEM ? "prob" :
                              n->type == WNODE_CONCEPT ? "idea" : "file";
            uart_puts(tag);
            uart_puts(" hits="); uart_dec(n->hit_count);
            uart_puts(" "); uart_puts(n->name); uart_puts("\n");
        }
    }

    if (g_wire.n_edges > 0) {
        for (uint32_t i = 0; i < g_wire.n_edges; i++) {
            WireEdge *e = &g_wire.edges[i];
            uart_puts("    ");
            if (e->weight > W_INIT + 30)       uart_puts("*** ");
            else if (e->weight > W_INIT + 10)  uart_puts("**  ");
            else if (e->weight > W_INIT - 10)  uart_puts("*   ");
            else                                uart_puts(".   ");
            uart_puts("w="); uart_dec(e->weight);
            uart_puts(" p="); uart_dec(e->passes);
            uart_puts(" f="); uart_dec(e->fails);
            uart_puts("  "); uart_puts(g_wire.nodes[e->src].name);
            uart_puts(" -> "); uart_puts(g_wire.nodes[e->dst].name);
            uart_puts("\n");
        }
    }

    if (g_wire.n_nodes == 0)
        uart_puts("    (empty)\n");
}

void wire_query(const char *name) {
    int id = wire_find(name);
    if (id < 0) {
        uart_puts("  Not in graph: "); uart_puts(name); uart_puts("\n");
        return;
    }
    WireNode *n = &g_wire.nodes[id];
    uart_puts("  "); uart_puts(n->name);
    uart_puts(" (hits="); uart_dec(n->hit_count); uart_puts(")\n");
    int count = 0;
    for (uint32_t i = 0; i < g_wire.n_edges; i++) {
        WireEdge *e = &g_wire.edges[i];
        if ((int)e->src == id) {
            uart_puts("    w="); uart_dec(e->weight);
            uart_puts(" -> "); uart_puts(g_wire.nodes[e->dst].name);
            uart_puts("\n");
            count++;
        }
    }
    if (count == 0) uart_puts("    (no connections)\n");
}

void wire_reset(void) {
    wire_init();
    uart_puts("  Wire graph cleared\n");
}

WireGraph *wire_get(void) { return &g_wire; }
