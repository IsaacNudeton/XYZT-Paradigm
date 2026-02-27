/*
 * xyzt_v6.c — Pure Co-Presence Engine
 *
 * v5 had addition hiding in the engine. That's not bedrock.
 * Addition is a human distinction — counting is observation.
 *
 * Real bedrock:
 *   One distinction in a void — meaningless.
 *   Two distinctions — still meaningless.
 *   The observer makes them distinguishable. {2, 3}.
 *
 * So the engine holds NO arithmetic. Not addition. Not counting.
 * tick() does one thing: route distinctions through topology.
 * The output is co-presence — a set of who showed up.
 *
 * The observer creates ALL meaning from co-presence:
 *   Layer 0: co-presence          (who arrived?)
 *   Layer 1: boolean logic        (obs_any→OR, obs_all→AND, obs_parity→XOR)
 *   Layer 2: arithmetic           (chained full adders from boolean)
 *   Layer 3: comparison, multiply (from arithmetic)
 *   Layer 4: state machines       (boolean + feedback)
 *
 * Distinction on top of distinction on top of distinction.
 * The engine never computes. It only presents.
 * The observer creates every operation that exists.
 *
 * D(nothing)      = distinction
 * D(distinction)  = co-presence
 * D(co-presence)  = meaning  ← observer lives here
 *
 * Isaac Oravec & Claude, February 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAX_POS 64
#define BIT(n) (1ULL << (n))

/* ══════════════════════════════════════════════════════════════
 * THE ENGINE — zero arithmetic inside
 *
 * Positions hold a distinction (marked) or don't (unmarked).
 * Topology says who reads whom.
 * tick() creates co-presence: the SET of marked sources.
 * That's it. No counting. No summing. No operations.
 * ══════════════════════════════════════════════════════════════ */

typedef struct {
    uint8_t  marked[MAX_POS];       /* distinction present: 0 or 1       */
    uint64_t reads[MAX_POS];        /* topology: bitmask of source pos   */
    uint64_t co_present[MAX_POS];   /* result: bitmask of marked sources */
    uint8_t  active[MAX_POS];       /* is this position wired            */
    int      n_pos;
} Grid;

static void grid_init(Grid *g) { memset(g, 0, sizeof(*g)); }

static void mark(Grid *g, int pos, int val) {
    g->marked[pos] = val ? 1 : 0;
    if (pos >= g->n_pos) g->n_pos = pos + 1;
}

static void wire(Grid *g, int pos, uint64_t rd) {
    g->reads[pos]  = rd;
    g->active[pos] = 1;
    if (pos >= g->n_pos) g->n_pos = pos + 1;
}

/*
 * THE tick. No arithmetic. Only co-presence.
 *
 * For each wired position: look at sources.
 * Record WHICH sources are marked. As a set.
 * That's the entire engine.
 */
static void tick(Grid *g) {
    uint8_t snap[MAX_POS];
    memcpy(snap, g->marked, MAX_POS);

    for (int p = 0; p < g->n_pos; p++) {
        if (!g->active[p]) continue;
        uint64_t present = 0;
        uint64_t bits = g->reads[p];
        while (bits) {
            int b = __builtin_ctzll(bits);
            if (snap[b]) present |= (1ULL << b);
            bits &= bits - 1;
        }
        g->co_present[p] = present;
    }
}

/* ══════════════════════════════════════════════════════════════
 * OBSERVERS — all meaning lives here
 *
 * The engine presents co-presence.
 * The observer decides what it means.
 * Even counting is observation — not engine.
 * ══════════════════════════════════════════════════════════════ */

/* Did anyone show up? */
static int obs_any(uint64_t cp) {
    return cp != 0 ? 1 : 0;
}

/* Did everyone show up? */
static int obs_all(uint64_t cp, uint64_t rd) {
    return (cp & rd) == rd ? 1 : 0;
}

/* Did nobody show up? */
static int obs_none(uint64_t cp) {
    return cp == 0 ? 1 : 0;
}

/* Odd number present? (counting is observation) */
static int obs_parity(uint64_t cp) {
    return __builtin_popcountll(cp) & 1;
}

/* How many showed up? (counting is observation) */
static int obs_count(uint64_t cp) {
    return __builtin_popcountll(cp);
}

/* At least N present? */
static int obs_ge(uint64_t cp, int n) {
    return __builtin_popcountll(cp) >= n ? 1 : 0;
}

/* Exactly N present? */
static int obs_exactly(uint64_t cp, int n) {
    return __builtin_popcountll(cp) == n ? 1 : 0;
}

/* ══════════════════════════════════════════════════════════════
 * BUILDING BLOCKS — observers composed through topology
 *
 * Each function creates a Grid, wires it, ticks it, observes.
 * Every operation goes through the engine.
 * Nothing is computed "outside."
 * ══════════════════════════════════════════════════════════════ */

static int do_and(int a, int b) {
    Grid g; grid_init(&g);
    mark(&g, 0, a); mark(&g, 1, b);
    wire(&g, 2, BIT(0)|BIT(1));
    tick(&g);
    return obs_all(g.co_present[2], BIT(0)|BIT(1));
}

static int do_or(int a, int b) {
    Grid g; grid_init(&g);
    mark(&g, 0, a); mark(&g, 1, b);
    wire(&g, 2, BIT(0)|BIT(1));
    tick(&g);
    return obs_any(g.co_present[2]);
}

static int do_not(int a) {
    /* bias position (always marked) + input.
     * If only bias arrived → 1. If both arrived → 0.
     * obs_exactly(1): "did exactly one show up?" */
    Grid g; grid_init(&g);
    mark(&g, 0, a); mark(&g, 1, 1);  /* pos 1 = bias */
    wire(&g, 2, BIT(0)|BIT(1));
    tick(&g);
    return obs_exactly(g.co_present[2], 1);
}

static int do_xor(int a, int b) {
    Grid g; grid_init(&g);
    mark(&g, 0, a); mark(&g, 1, b);
    wire(&g, 2, BIT(0)|BIT(1));
    tick(&g);
    return obs_parity(g.co_present[2]);
}

/* Full adder: three distinctions co-present at one position.
 * Sum  = parity of who showed up.
 * Carry = at least 2 showed up.
 * Same co-presence. Two observers. Two meanings. */
typedef struct { int sum; int cout; } FA;

static FA do_full_adder(int a, int b, int cin) {
    Grid g; grid_init(&g);
    mark(&g, 0, a); mark(&g, 1, b); mark(&g, 2, cin);
    wire(&g, 3, BIT(0)|BIT(1)|BIT(2));
    tick(&g);
    FA r;
    r.sum  = obs_parity(g.co_present[3]);
    r.cout = obs_ge(g.co_present[3], 2);
    return r;
}

/* ══════════════════════════════════════════════════════════════
 * ARITHMETIC — emerges from boolean, which emerges from
 *              co-presence. Distinction on distinction.
 * ══════════════════════════════════════════════════════════════ */

static void int_to_bits(int val, int *bits, int n) {
    for (int i = 0; i < n; i++) bits[i] = (val >> i) & 1;
}

static int bits_to_int(int *bits, int n) {
    int val = 0;
    for (int i = 0; i < n; i++) if (bits[i]) val |= (1 << i);
    return val;
}

/* N-bit addition: chain of full adders.
 * Each full adder is a Grid tick + two observers.
 * Addition is not assumed — it's built from co-presence. */
static int add_nbits(int *a, int *b, int *result, int n) {
    int carry = 0;
    for (int i = 0; i < n; i++) {
        FA r = do_full_adder(a[i], b[i], carry);
        result[i] = r.sum;
        carry = r.cout;
    }
    return carry;
}

/* N-bit NOT: each bit through its own Grid. */
static void not_nbits(int *in, int *out, int n) {
    for (int i = 0; i < n; i++) out[i] = do_not(in[i]);
}

/* N-bit subtraction: A - B = A + NOT(B) + 1.
 * Two's complement built entirely from co-presence. */
static int sub_nbits(int *a, int *b, int *result, int n) {
    int nb[32];
    not_nbits(b, nb, n);
    /* Add 1 via initial carry = 1 */
    int carry = 1;
    for (int i = 0; i < n; i++) {
        FA r = do_full_adder(a[i], nb[i], carry);
        result[i] = r.sum;
        carry = r.cout;
    }
    return carry;
}

/* N-bit multiply: shift-and-add.
 * AND gates select partial products.
 * Adder tree accumulates them.
 * Every gate is a Grid tick. */
static int multiply(int va, int vb, int n) {
    int a[16], b[16];
    int_to_bits(va, a, n);
    int_to_bits(vb, b, n);
    int accum[32] = {0};
    int width = 2 * n;

    for (int i = 0; i < n; i++) {
        int partial[32] = {0};
        for (int j = 0; j < n; j++) {
            partial[i + j] = do_and(a[j], b[i]);
        }
        int temp[32] = {0};
        add_nbits(accum, partial, temp, width);
        memcpy(accum, temp, sizeof(accum));
    }
    return bits_to_int(accum, width);
}

/* Zero check: OR all bits. Are ANY distinctions present? */
static int is_zero(int *bits, int n) {
    Grid g; grid_init(&g);
    uint64_t rd = 0;
    for (int i = 0; i < n; i++) {
        mark(&g, i, bits[i]);
        rd |= BIT(i);
    }
    wire(&g, n, rd);
    tick(&g);
    return obs_none(g.co_present[n]);
}

/* ══════════════════════════════════════════════════════════════
 * TEST FRAMEWORK
 * ══════════════════════════════════════════════════════════════ */

static int g_pass = 0, g_fail = 0;
static void check(const char *name, int exp, int got) {
    if (exp == got) g_pass++;
    else { g_fail++; printf("  FAIL %s: expected %d, got %d\n", name, exp, got); }
}

/* ══════════════════════════════════════════════════════════════
 * TESTS — Layer 1: Boolean from co-presence
 * ══════════════════════════════════════════════════════════════ */

static void test_not(void) {
    printf("--- NOT (bias + obs_exactly) ---\n");
    for (int a = 0; a <= 1; a++) {
        Grid g; grid_init(&g);
        mark(&g, 0, a); mark(&g, 1, 1);
        wire(&g, 2, BIT(0)|BIT(1));
        tick(&g);
        check("NOT", 1-a, obs_exactly(g.co_present[2], 1));
    }
}

static void test_and(void) {
    printf("--- AND (obs_all) ---\n");
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++) {
            Grid g; grid_init(&g);
            mark(&g, 0, a); mark(&g, 1, b);
            wire(&g, 2, BIT(0)|BIT(1));
            tick(&g);
            check("AND", a&b, obs_all(g.co_present[2], BIT(0)|BIT(1)));
        }
}

static void test_or(void) {
    printf("--- OR (obs_any) ---\n");
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++) {
            Grid g; grid_init(&g);
            mark(&g, 0, a); mark(&g, 1, b);
            wire(&g, 2, BIT(0)|BIT(1));
            tick(&g);
            check("OR", a|b, obs_any(g.co_present[2]));
        }
}

static void test_xor(void) {
    printf("--- XOR (obs_parity) ---\n");
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++) {
            Grid g; grid_init(&g);
            mark(&g, 0, a); mark(&g, 1, b);
            wire(&g, 2, BIT(0)|BIT(1));
            tick(&g);
            check("XOR", a^b, obs_parity(g.co_present[2]));
        }
}

static void test_majority(void) {
    printf("--- Majority (obs_ge) ---\n");
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++)
            for (int c = 0; c <= 1; c++) {
                Grid g; grid_init(&g);
                mark(&g, 0, a); mark(&g, 1, b); mark(&g, 2, c);
                wire(&g, 3, BIT(0)|BIT(1)|BIT(2));
                tick(&g);
                check("MAJ", (a+b+c>=2)?1:0, obs_ge(g.co_present[3], 2));
            }
}

static void test_one_collision_all_ops(void) {
    printf("--- One co-presence -> 6 boolean ops ---\n");
    printf("  a b | who showed up      | OR AND XOR NAND NOR XNOR\n");
    printf("  ----|--------------------|--------------------------\n");
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++) {
            Grid g; grid_init(&g);
            mark(&g, 0, a); mark(&g, 1, b);
            uint64_t rd = BIT(0)|BIT(1);
            wire(&g, 2, rd);
            tick(&g);

            uint64_t cp = g.co_present[2];
            int or_  = obs_any(cp);
            int and_ = obs_all(cp, rd);
            int xor_ = obs_parity(cp);
            int nand = 1 - and_;
            int nor  = 1 - or_;
            int xnor = 1 - xor_;

            const char *who = (a&&b) ? "{pos0, pos1}" :
                              (a)    ? "{pos0}       " :
                              (b)    ? "{pos1}       " :
                                       "{}           ";
            printf("  %d %d | %s |  %d   %d    %d    %d    %d    %d\n",
                   a, b, who, or_, and_, xor_, nand, nor, xnor);

            check("OR",a|b,or_);     check("AND",a&b,and_);   check("XOR",a^b,xor_);
            check("NAND",!(a&b),nand); check("NOR",!(a|b),nor); check("XNOR",!(a^b),xnor);
        }
}

/* ══════════════════════════════════════════════════════════════
 * TESTS — Layer 2: Arithmetic from boolean from co-presence
 * ══════════════════════════════════════════════════════════════ */

static void test_full_adder(void) {
    printf("--- Full adder (co-presence of 3) ---\n");
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++)
            for (int cin = 0; cin <= 1; cin++) {
                Grid g; grid_init(&g);
                mark(&g, 0, a); mark(&g, 1, b); mark(&g, 2, cin);
                wire(&g, 3, BIT(0)|BIT(1)|BIT(2));
                tick(&g);
                int total = a + b + cin;
                check("FA sum",  total&1,  obs_parity(g.co_present[3]));
                check("FA cout", total>>1, obs_ge(g.co_present[3], 2));
            }
}

static void test_4bit_adder(void) {
    printf("--- 4-bit ripple adder (chained full adders) ---\n");
    struct { int a,b,sum,cout; } cases[] = {
        {0,0,0,0},{1,1,2,0},{3,5,8,0},{7,8,15,0},
        {15,0,15,0},{15,1,0,1},{9,6,15,0},{10,11,5,1},
    };
    for (int t = 0; t < 8; t++) {
        int a[4], b[4], result[4];
        int_to_bits(cases[t].a, a, 4);
        int_to_bits(cases[t].b, b, 4);
        int cout = add_nbits(a, b, result, 4);
        int sum = bits_to_int(result, 4);
        char n[32];
        snprintf(n,32,"4ADD %d+%d", cases[t].a, cases[t].b);
        check(n, cases[t].sum, sum);
        snprintf(n,32,"4ADD cout %d+%d", cases[t].a, cases[t].b);
        check(n, cases[t].cout, cout);
    }
}

static void test_addition(void) {
    printf("--- Addition (8-bit, from chained co-presence) ---\n");
    struct { int a,b,sum; } cases[] = {
        {0,0,0},{1,1,2},{3,5,8},{7,8,15},{100,200,44},
        /* 100+200=300, but 300 mod 256 = 44 in 8-bit */
    };
    for (int i = 0; i < 5; i++) {
        int a[8], b[8], result[8];
        int_to_bits(cases[i].a, a, 8);
        int_to_bits(cases[i].b, b, 8);
        add_nbits(a, b, result, 8);
        int sum = bits_to_int(result, 8);
        char n[32]; snprintf(n,32,"%d+%d", cases[i].a, cases[i].b);
        check(n, cases[i].sum, sum);
    }
}

static void test_subtraction(void) {
    printf("--- Subtraction (NOT + add + 1, all from co-presence) ---\n");
    struct { int a,b,diff; } cases[] = {
        {5,3,2},{10,10,0},{100,37,63},{7,1,6},
    };
    for (int i = 0; i < 4; i++) {
        int a[8], b[8], result[8];
        int_to_bits(cases[i].a, a, 8);
        int_to_bits(cases[i].b, b, 8);
        sub_nbits(a, b, result, 8);
        int diff = bits_to_int(result, 8);
        char n[32]; snprintf(n,32,"%d-%d", cases[i].a, cases[i].b);
        check(n, cases[i].diff, diff);
    }
}

static void test_comparison(void) {
    printf("--- Comparison (subtraction sign, from co-presence) ---\n");
    struct { int a,b,gt,eq,lt; } cases[] = {
        {5,3,1,0,0},{3,5,0,0,1},{4,4,0,1,0},{0,0,0,1,0},{100,1,1,0,0},
    };
    for (int i = 0; i < 5; i++) {
        int a[8], b[8], result[8];
        int_to_bits(cases[i].a, a, 8);
        int_to_bits(cases[i].b, b, 8);
        sub_nbits(a, b, result, 8);

        int zero = is_zero(result, 8);
        int sign = result[7];  /* MSB = sign in two's complement */
        int gt = (zero == 0 && sign == 0) ? 1 : 0;
        int eq = zero;
        int lt = sign;

        char n[32];
        snprintf(n,32,"%d>%d",cases[i].a,cases[i].b); check(n, cases[i].gt, gt);
        snprintf(n,32,"%d=%d",cases[i].a,cases[i].b); check(n, cases[i].eq, eq);
        snprintf(n,32,"%d<%d",cases[i].a,cases[i].b); check(n, cases[i].lt, lt);
    }
}

static void test_multiplication(void) {
    printf("--- Multiplication (shift-and-add, pure grid) ---\n");
    struct { int a,b,expected; } cases[] = {
        {3,4,12},{7,3,21},{5,5,25},{0,7,0},{1,1,1},
    };
    for (int i = 0; i < 5; i++) {
        int result = multiply(cases[i].a, cases[i].b, 8);
        char n[32]; snprintf(n,32,"%d*%d", cases[i].a, cases[i].b);
        check(n, cases[i].expected, result);
    }
}

/* ══════════════════════════════════════════════════════════════
 * TESTS — Layer 3: Multi-layer composition
 * ══════════════════════════════════════════════════════════════ */

static void test_neural_xor(void) {
    printf("--- Neural XOR (multi-layer, pure co-presence) ---\n");
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++) {
            /* Layer 1: one co-presence, two observers */
            Grid g; grid_init(&g);
            mark(&g, 0, a); mark(&g, 1, b);
            wire(&g, 2, BIT(0)|BIT(1));
            tick(&g);
            int h0 = obs_any(g.co_present[2]);              /* OR  */
            int h1 = 1 - obs_all(g.co_present[2], BIT(0)|BIT(1)); /* NAND */

            /* Layer 2: AND(OR, NAND) = XOR */
            Grid g2; grid_init(&g2);
            mark(&g2, 0, h0); mark(&g2, 1, h1);
            wire(&g2, 2, BIT(0)|BIT(1));
            tick(&g2);
            check("NXOR", a^b, obs_all(g2.co_present[2], BIT(0)|BIT(1)));
        }
}

/* ══════════════════════════════════════════════════════════════
 * TESTS — Layer 4: Sequential logic (boolean + feedback)
 * ══════════════════════════════════════════════════════════════ */

static void test_counter(void) {
    printf("--- 2-bit counter (NOT + XOR feedback) ---\n");
    int r0 = 0, r1 = 0;
    int exp_r0[] = {1,0,1,0,1,0,1,0};
    int exp_r1[] = {0,1,1,0,0,1,1,0};
    for (int t = 0; t < 8; t++) {
        /* bit 0 toggles: NOT(r0) */
        int new_r0 = do_not(r0);
        /* bit 1 toggles when r0=1: XOR(r0, r1) */
        int new_r1 = do_xor(r0, r1);
        r0 = new_r0;
        r1 = new_r1;
        char n[32]; snprintf(n,32,"CNT t%d",t+1);
        check(n, exp_r0[t]|(exp_r1[t]<<1), r0|(r1<<1));
    }
}

static void test_sr_latch(void) {
    printf("--- SR latch (OR + AND + NOT feedback) ---\n");
    int q = 0;
    struct { int s,r,eq; const char *d; } seq[] = {
        {1,0,1,"SET"},{0,0,1,"HOLD"},{0,1,0,"RESET"},
        {0,0,0,"HOLD"},{1,0,1,"SET"},{0,0,1,"HOLD"},
    };
    for (int t = 0; t < 6; t++) {
        /* Q_next = S OR (Q AND NOT R) */
        int not_r = do_not(seq[t].r);
        int q_hold = do_and(q, not_r);
        q = do_or(seq[t].s, q_hold);
        char n[32]; snprintf(n,32,"SR %s",seq[t].d);
        check(n, seq[t].eq, q);
    }
}

static void test_traffic_fsm(void) {
    printf("--- Traffic FSM (3-state: NOT + AND) ---\n");
    int b0 = 0, b1 = 0;
    /* States: 00=Green, 01=Yellow, 10=Red, cycle */
    int exp_b0[] = {1,0,0, 1,0,0, 1,0,0};
    int exp_b1[] = {0,1,0, 0,1,0, 0,1,0};
    for (int t = 0; t < 9; t++) {
        int nb0 = do_not(b0);
        int nb1 = do_not(b1);
        int new_b0 = do_and(nb0, nb1);   /* NOR(b0,b1): activate from state 00 */
        int new_b1 = do_and(b0, nb1);    /* b0 AND NOT b1: activate from state 01 */
        b0 = new_b0;
        b1 = new_b1;
        char n[32]; snprintf(n,32,"FSM t%d",t+1);
        check(n, exp_b0[t]|(exp_b1[t]<<1), b0|(b1<<1));
    }
}

/* ══════════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════════════ */

int main(void) {
    printf("==========================================================\n");
    printf("  XYZT v6 — Pure Co-Presence Engine\n");
    printf("  Zero arithmetic in the engine.\n");
    printf("  The observer creates ALL meaning.\n");
    printf("  Distinction on top of distinction.\n");
    printf("==========================================================\n\n");

    /* Layer 1: Boolean from co-presence */
    test_not();
    test_and();
    test_or();
    test_xor();
    test_majority();
    test_one_collision_all_ops();

    /* Layer 2: Arithmetic from boolean from co-presence */
    test_full_adder();
    test_4bit_adder();
    test_addition();
    test_subtraction();
    test_comparison();
    test_multiplication();

    /* Layer 3: Multi-layer composition */
    test_neural_xor();

    /* Layer 4: Sequential logic (feedback) */
    test_counter();
    test_sr_latch();
    test_traffic_fsm();

    printf("\n==========================================================\n");
    printf("  RESULTS: %d passed, %d failed, %d total\n",
           g_pass, g_fail, g_pass + g_fail);
    printf("==========================================================\n");

    if (g_fail == 0) {
        printf("\n  ALL PASS.\n\n");
        printf("  The engine does ONE thing:\n");
        printf("    Route distinctions. Record co-presence.\n");
        printf("    No addition. No counting. No arithmetic.\n\n");
        printf("  The observer creates every layer:\n");
        printf("    L0  co-presence     who showed up?\n");
        printf("    L1  boolean         obs_any->OR  obs_all->AND  obs_parity->XOR\n");
        printf("    L2  arithmetic      chained full adders from boolean\n");
        printf("    L3  comparison      subtraction sign from arithmetic\n");
        printf("    L4  multiplication  shift-and-add from AND + arithmetic\n");
        printf("    L5  state machines  boolean + feedback\n\n");
        printf("  v5 had addition hiding in tick().\n");
        printf("  v6 has NOTHING in tick().\n");
        printf("  Distinction on top of distinction on top of distinction.\n");
        printf("  The operation is not in the engine.\n");
        printf("  The operation is not even in the observation.\n");
        printf("  The operation is in the DISTINCTION the observer makes.\n\n");
    } else {
        printf("\n  FAILURES DETECTED.\n\n");
    }
    return g_fail;
}
