/*
 * test_pio_sense.c — Verify PIO adapter → sense pipeline
 *
 * Simulates what happens when a real I2C sensor is connected
 * to Pico GPIO pins and the PIO samples it at 1MHz.
 * PIO buffer → pio_to_capture → sense_extract → wire graph clusters.
 *
 * Compile: gcc -O2 -o test_pio_sense test_pio_sense.c -Wno-unused-function
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Stubs */
static void uart_puts(const char *s) { printf("%s", s); }
static void uart_putc(char c) { putchar(c); }
static void uart_dec(uint32_t v) { printf("%u", v); }
static void uart_hex(uint32_t v) { printf("0x%08X", v); }
static void uart_hex64(uint64_t v) { printf("0x%016llX", (unsigned long long)v); }
static void gpio_set_function(uint32_t p, uint32_t f) { (void)p; (void)f; }
static void gpio_set_pull(uint32_t p, uint32_t l) { (void)p; (void)l; }
static void gpio_enable_edge_detect(uint32_t p, int r, int f) { (void)p; (void)r; (void)f; }

/* Wire graph (same as wire.c) */
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
int wire_find(const char *name) { for (uint32_t i = 0; i < g_wire.n_nodes; i++) if (str_eq(g_wire.nodes[i].name, name)) return (int)i; return -1; }
int wire_add(const char *name, uint8_t type) {
    int id = wire_find(name); if (id >= 0) return id;
    if (g_wire.n_nodes >= WIRE_MAX_NODES) return -1;
    id = g_wire.n_nodes++; int i = 0;
    while (name[i] && i < WIRE_NAME_LEN - 1) { g_wire.nodes[id].name[i] = name[i]; i++; }
    g_wire.nodes[id].name[i] = '\0'; g_wire.nodes[id].type = type; return id;
}
static int find_edge(int src, int dst) { for (uint32_t i = 0; i < g_wire.n_edges; i++) if (g_wire.edges[i].src == src && g_wire.edges[i].dst == dst) return (int)i; return -1; }
void wire_connect(int a, int b) {
    if (a < 0 || b < 0) return;
    if (find_edge(a,b)<0 && g_wire.n_edges<WIRE_MAX_EDGES) { WireEdge *e=&g_wire.edges[g_wire.n_edges++]; e->src=a;e->dst=b;e->weight=W_INIT;e->passes=0;e->fails=0; }
    if (find_edge(b,a)<0 && g_wire.n_edges<WIRE_MAX_EDGES) { WireEdge *e=&g_wire.edges[g_wire.n_edges++]; e->src=b;e->dst=a;e->weight=W_INIT;e->passes=0;e->fails=0; }
    g_wire.nodes[a].hit_count++; g_wire.nodes[b].hit_count++;
}
void wire_learn(int id, int pass) {
    if (id<0||id>=(int)g_wire.n_nodes) return;
    for (uint32_t i=0;i<g_wire.n_edges;i++) { WireEdge *e=&g_wire.edges[i];
        if ((int)e->src==id||(int)e->dst==id) {
            if (pass) { e->passes++; int nw=(int)e->weight+W_LEARN; e->weight=(nw>W_MAX)?W_MAX:(uint8_t)nw; }
            else { e->fails++; int nw=(int)e->weight-W_LEARN; e->weight=(nw<W_MIN)?W_MIN:(uint8_t)nw; }
        }
    } g_wire.total_learns++;
}
WireGraph *wire_get(void) { return &g_wire; }

/* Include adapter and sense */
#include "pio_adapt.h"

void capture_for_duration(capture_buf_t *b, uint64_t d) { (void)b; (void)d; }

#define TEST_SENSE
#include "sense.c"

/* ═══════════════════════════════════════════════════════════════
 * SIMULATE PIO CAPTURE OF REAL I2C
 *
 * Pin 2 = SCL (clock, ~100kHz = 10us period, regular toggle)
 * Pin 3 = SDA (data, mostly high with pull-up, dips during transfer)
 *
 * At 1MHz sample rate (period_us = 1.0), we get one sample per us.
 * A 10us SCL period = 10 samples per clock cycle.
 * ═══════════════════════════════════════════════════════════════ */

#define PIO_BUF_SIZE 4096

static uint32_t pio_buf[PIO_BUF_SIZE];

static void sim_i2c_pio(void) {
    /* Simulate I2C at 100kHz on pins 2 (SCL) and 3 (SDA)
     * 1MHz sample rate = 1 sample/us
     * SCL period = 10us (5 high, 5 low)
     * SDA starts high, goes low for START, toggles with data, ends high for STOP */

    uint32_t scl_bit = (1 << 2);
    uint32_t sda_bit = (1 << 3);

    /* I2C byte: START + 8 data bits + ACK
     * START = SDA falls while SCL high
     * Data changes on SCL low, sampled on SCL high
     * ACK = SDA low on 9th clock */
    int data_bits[] = {1, 0, 1, 0, 1, 1, 0, 0, 0};  /* 0xAC + ACK */

    int idx = 0;

    /* Idle: both high */
    for (int i = 0; i < 50 && idx < PIO_BUF_SIZE; i++)
        pio_buf[idx++] = scl_bit | sda_bit;

    /* Three I2C byte transfers with gaps */
    for (int byte = 0; byte < 3; byte++) {
        /* START: SDA falls while SCL high */
        for (int i = 0; i < 5 && idx < PIO_BUF_SIZE; i++)
            pio_buf[idx++] = scl_bit;  /* SCL high, SDA low */

        /* 9 clock cycles (8 data + ACK) */
        for (int bit = 0; bit < 9; bit++) {
            uint32_t sda = data_bits[bit] ? sda_bit : 0;

            /* SCL low phase: SDA changes */
            for (int i = 0; i < 5 && idx < PIO_BUF_SIZE; i++)
                pio_buf[idx++] = sda;  /* SCL low, SDA = data */

            /* SCL high phase: SDA stable (sample point) */
            for (int i = 0; i < 5 && idx < PIO_BUF_SIZE; i++)
                pio_buf[idx++] = scl_bit | sda;
        }

        /* STOP: SDA rises while SCL high */
        for (int i = 0; i < 3 && idx < PIO_BUF_SIZE; i++)
            pio_buf[idx++] = scl_bit;  /* SCL high, SDA low */
        for (int i = 0; i < 3 && idx < PIO_BUF_SIZE; i++)
            pio_buf[idx++] = scl_bit | sda_bit;  /* SCL high, SDA high */

        /* Idle gap between bytes */
        for (int i = 0; i < 100 && idx < PIO_BUF_SIZE; i++)
            pio_buf[idx++] = scl_bit | sda_bit;
    }

    /* Fill rest with idle */
    while (idx < PIO_BUF_SIZE)
        pio_buf[idx++] = scl_bit | sda_bit;
}

/* Simulate SPI: pin 0=SCLK (500kHz), pin 1=MOSI, pin 4=CS */
static void sim_spi_pio(void) {
    uint32_t sclk = (1 << 0);
    uint32_t mosi = (1 << 1);
    uint32_t cs   = (1 << 4);
    int data[] = {1,0,1,1,0,1,0,0};  /* 0xB4 */

    int idx = 0;

    /* Idle: CS high, SCLK low */
    for (int i = 0; i < 50 && idx < PIO_BUF_SIZE; i++)
        pio_buf[idx++] = cs;

    for (int frame = 0; frame < 5; frame++) {
        /* CS goes low */
        for (int bit = 0; bit < 8; bit++) {
            uint32_t d = data[bit] ? mosi : 0;
            /* SCLK low phase */
            for (int i = 0; i < 1 && idx < PIO_BUF_SIZE; i++)
                pio_buf[idx++] = d;
            /* SCLK high phase */
            for (int i = 0; i < 1 && idx < PIO_BUF_SIZE; i++)
                pio_buf[idx++] = sclk | d;
        }
        /* CS high between frames */
        for (int i = 0; i < 20 && idx < PIO_BUF_SIZE; i++)
            pio_buf[idx++] = cs;
    }

    while (idx < PIO_BUF_SIZE) pio_buf[idx++] = cs;
}

/* ═══════════════════════════════════════════════════════════════
 * CLUSTER FINDER (same as test_sense.c)
 * ═══════════════════════════════════════════════════════════════ */

typedef struct { int members[32]; int n_members; } Cluster;

static int find_clusters(Cluster *clusters, int max_cl, uint8_t thresh) {
    int visited[WIRE_MAX_NODES] = {0};
    int nc = 0;
    for (uint32_t i = 0; i < g_wire.n_nodes && nc < max_cl; i++) {
        if (visited[i]) continue;
        Cluster *cl = &clusters[nc]; cl->n_members = 0;
        int queue[WIRE_MAX_NODES], qh=0, qt=0;
        queue[qt++] = i; visited[i] = 1;
        while (qh < qt) {
            int node = queue[qh++];
            cl->members[cl->n_members++] = node;
            for (uint32_t e = 0; e < g_wire.n_edges; e++) {
                WireEdge *edge = &g_wire.edges[e];
                if (edge->weight < thresh) continue;
                int nb = -1;
                if ((int)edge->src == node) nb = edge->dst;
                else if ((int)edge->dst == node) nb = edge->src;
                if (nb >= 0 && !visited[nb]) { visited[nb]=1; queue[qt++]=nb; }
            }
        }
        if (cl->n_members > 0) nc++;
    }
    return nc;
}

/* ═══════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════ */

int main(void) {
    int pass = 0, fail = 0;

    printf("\n  ════════════════════════════════════════════\n");
    printf("  PIO → Capture → Sense Pipeline Test\n");
    printf("  Simulates real hardware capture on Pico\n");
    printf("  ════════════════════════════════════════════\n\n");

    #define CHECK(name, cond) do { \
        if (cond) { pass++; printf("  PASS: %s\n", name); } \
        else { fail++; printf("  FAIL: %s\n", name); } \
    } while(0)

    /* ── Test 1: PIO adapter basic conversion ──────────────── */
    {
        /* Simple: alternating pin 0 state every 5 samples */
        uint32_t test_samples[100];
        for (int i = 0; i < 100; i++)
            test_samples[i] = (i / 5) % 2 ? 1 : 0;

        capture_buf_t cap;
        pio_to_capture(test_samples, 100, 1.0f, 0xFFFF, &cap);

        CHECK("Adapter produces edges", cap.count > 2);
        CHECK("Adapter: first event at t=0", cap.events[0].timestamp == 0);
        CHECK("Adapter: edges have correct pin", (cap.events[1].pin_state & 1) == 1);

        printf("  Adapter: %u edges from 100 samples\n\n", cap.count);
    }

    /* ── Test 2: Simulated I2C through full pipeline ───────── */
    {
        wire_init();

        sim_i2c_pio();

        capture_buf_t cap;
        static sense_pass_t sp;

        /* Run 10 observation rounds */
        for (int round = 0; round < 10; round++) {
            pio_to_capture(pio_buf, PIO_BUF_SIZE, 1.0f, (1<<2)|(1<<3), &cap);
            sense_extract(&cap, &sp);
            sense_decay(&sp);
        }

        printf("  I2C simulation:\n");
        printf("    Graph: %u nodes, %u edges\n", g_wire.n_nodes, g_wire.n_edges);

        CHECK("I2C: features extracted", sp.n_features > 0);
        CHECK("I2C: pin 2 active (SCL)", wire_find("A2") >= 0);
        CHECK("I2C: pin 3 active (SDA)", wire_find("A3") >= 0);
        /* I2C SCL is NOT continuously regular — it clocks during
         * transfers then idles between bytes. The system correctly
         * identifies it as bursty, not regular. */
        CHECK("I2C: pin 2 bursty (SCL clocks then idles)", wire_find("B2") >= 0);
        CHECK("I2C: pins 2,3 correlated", wire_find("C2.3") >= 0);

        /* All I2C features should form one cluster */
        Cluster clusters[20];
        int nc = find_clusters(clusters, 20, W_INIT + 5);
        printf("    Clusters: %d\n", nc);
        CHECK("I2C: single cluster formed", nc == 1);

        if (nc > 0) {
            printf("    Cluster 0: ");
            for (int m = 0; m < clusters[0].n_members; m++) {
                if (m > 0) printf(", ");
                printf("%s", g_wire.nodes[clusters[0].members[m]].name);
            }
            printf("\n");
        }
        printf("\n");
    }

    /* ── Test 3: I2C + SPI form two distinct clusters ──────── */
    {
        wire_init();

        static sense_pass_t sp;
        capture_buf_t cap;

        for (int round = 0; round < 15; round++) {
            /* I2C observation */
            sim_i2c_pio();
            pio_to_capture(pio_buf, PIO_BUF_SIZE, 1.0f, (1<<2)|(1<<3), &cap);
            sense_extract(&cap, &sp);
            sense_decay(&sp);

            /* SPI observation */
            sim_spi_pio();
            pio_to_capture(pio_buf, PIO_BUF_SIZE, 1.0f, (1<<0)|(1<<1)|(1<<4), &cap);
            sense_extract(&cap, &sp);
            sense_decay(&sp);
        }

        printf("  I2C + SPI mixed:\n");
        printf("    Graph: %u nodes, %u edges\n", g_wire.n_nodes, g_wire.n_edges);

        Cluster clusters[20];
        int nc = find_clusters(clusters, 20, W_INIT + 5);
        printf("    Clusters: %d\n", nc);

        CHECK("Mixed: two distinct clusters", nc == 2);

        for (int c = 0; c < nc; c++) {
            printf("    Cluster %d: ", c);
            for (int m = 0; m < clusters[c].n_members; m++) {
                if (m > 0) printf(", ");
                printf("%s", g_wire.nodes[clusters[c].members[m]].name);
            }
            printf("\n");
        }

        /* Cross-cluster check */
        int a2 = wire_find("A2");  /* I2C SCL */
        int a0 = wire_find("A0");  /* SPI SCLK */
        if (a2 >= 0 && a0 >= 0) {
            int eid = find_edge(a2, a0);
            CHECK("I2C-SPI cross-cluster absent or weak",
                  eid < 0 || g_wire.edges[eid].weight <= W_INIT);
        }
        printf("\n");
    }

    #undef CHECK

    printf("  ════════════════════════════════\n");
    printf("  TOTAL: %d pass, %d fail\n", pass, fail);
    printf("  %s\n", pass > 0 && fail == 0 ? "PIPELINE VERIFIED." : "NEEDS WORK.");
    printf("  ════════════════════════════════\n\n");

    return fail == 0 ? 0 : 1;
}
