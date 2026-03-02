/*
 * xyzt_v5.c — Pure Accumulation Engine
 *
 * ONE operation: signals arrive at a position, position counts them.
 * No threshold. No neuron flag. No ALU. No opcodes.
 *
 * NOT uses a bias position (constant 1) + inversion:
 *   NOT(a) = 1 + (-a) = 1 - a.  a=0->1, a=1->0.
 *   Same accumulation. Same observer. Just topology.
 *
 * The observer interprets the accumulation at the output boundary.
 * AND, OR, XOR, NOT, addition, subtraction, comparison —
 * all from the same engine operation.
 *
 * Physics  = D(nothing) = something
 * Math     = 0, 1
 * Compute  = collision at position
 *
 * Isaac Oravec & Claude, February 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAX_POS 64
#define BIT(n) (1ULL << (n))

typedef struct {
    int32_t  val[MAX_POS];
    uint64_t reads[MAX_POS];
    uint64_t invert[MAX_POS];
    uint8_t  active[MAX_POS];
    int      n_pos;
} Grid;

static void grid_init(Grid *g) { memset(g, 0, sizeof(*g)); }

static void wire(Grid *g, uint8_t pos, uint64_t rd, uint64_t inv) {
    g->reads[pos]  = rd;
    g->invert[pos] = inv;
    g->active[pos] = 1;
    if (pos >= g->n_pos) g->n_pos = pos + 1;
}

/* THE engine. One operation. Accumulate. */
static void tick(Grid *g) {
    int32_t scratch[MAX_POS];
    memcpy(scratch, g->val, sizeof(scratch));
    for (int p = 0; p < g->n_pos; p++) {
        if (!g->active[p]) continue;
        int32_t accum = 0;
        uint64_t bits = g->reads[p];
        uint64_t inv  = g->invert[p];
        while (bits) {
            int b = __builtin_ctzll(bits);
            int32_t v = scratch[b];
            if (inv & (1ULL << b)) v = -v;
            accum += v;
            bits &= bits - 1;
        }
        g->val[p] = accum;
    }
}

/* Observers: not in the engine. At the boundary. */
static int obs_bool(int32_t v)       { return v > 0 ? 1 : 0; }
static int obs_all(int32_t v, int n) { return v >= n ? 1 : 0; }
static int obs_parity(int32_t v)     { return (v & 1) ? 1 : 0; }
static int obs_raw(int32_t v)        { return v; }

/* Test framework */
static int g_pass = 0, g_fail = 0;
static void check(const char *name, int exp, int got) {
    if (exp == got) g_pass++;
    else { g_fail++; printf("  FAIL %s: expected %d, got %d\n", name, exp, got); }
}

/* ══════════════════════════════════════════════════════════════ */

static void test_not(void) {
    printf("--- NOT ---\n");
    for (int a = 0; a <= 1; a++) {
        Grid g; grid_init(&g);
        g.val[0] = a; g.val[1] = 1;
        wire(&g, 2, BIT(0)|BIT(1), BIT(0));
        tick(&g);
        check("NOT", 1-a, obs_bool(g.val[2]));
    }
}

static void test_and(void) {
    printf("--- AND ---\n");
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++) {
            Grid g; grid_init(&g);
            g.val[0] = a; g.val[1] = b;
            wire(&g, 2, BIT(0)|BIT(1), 0);
            tick(&g);
            check("AND", a&b, obs_all(g.val[2], 2));
        }
}

static void test_or(void) {
    printf("--- OR ---\n");
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++) {
            Grid g; grid_init(&g);
            g.val[0] = a; g.val[1] = b;
            wire(&g, 2, BIT(0)|BIT(1), 0);
            tick(&g);
            check("OR", a|b, obs_bool(g.val[2]));
        }
}

static void test_xor(void) {
    printf("--- XOR ---\n");
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++) {
            Grid g; grid_init(&g);
            g.val[0] = a; g.val[1] = b;
            wire(&g, 2, BIT(0)|BIT(1), 0);
            tick(&g);
            check("XOR", a^b, obs_parity(g.val[2]));
        }
}

static void test_majority(void) {
    printf("--- Majority ---\n");
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++)
            for (int c = 0; c <= 1; c++) {
                Grid g; grid_init(&g);
                g.val[0] = a; g.val[1] = b; g.val[2] = c;
                wire(&g, 3, BIT(0)|BIT(1)|BIT(2), 0);
                tick(&g);
                check("MAJ", (a+b+c>=2)?1:0, obs_all(g.val[3], 2));
            }
}

static void test_one_collision_all_ops(void) {
    printf("--- One collision -> 6 boolean ops ---\n");
    printf("  a b | raw | OR AND XOR NAND NOR XNOR\n");
    printf("  ----|-----|---------------------------\n");
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++) {
            Grid g; grid_init(&g);
            g.val[0] = a; g.val[1] = b;
            wire(&g, 2, BIT(0)|BIT(1), 0);
            tick(&g);
            int32_t r = g.val[2];
            int or_=obs_bool(r), and_=obs_all(r,2), xor_=obs_parity(r);
            int nand=1-and_, nor=1-or_, xnor=1-xor_;
            printf("  %d %d |  %d  |  %d   %d    %d    %d    %d    %d\n",
                   a, b, r, or_, and_, xor_, nand, nor, xnor);
            check("OR",a|b,or_); check("AND",a&b,and_); check("XOR",a^b,xor_);
            check("NAND",!(a&b),nand); check("NOR",!(a|b),nor); check("XNOR",!(a^b),xnor);
        }
}

static void test_addition(void) {
    printf("--- Addition ---\n");
    int cases[][3] = {{0,0,0},{1,1,2},{3,5,8},{7,8,15},{100,200,300}};
    for (int i = 0; i < 5; i++) {
        Grid g; grid_init(&g);
        g.val[0] = cases[i][0]; g.val[1] = cases[i][1];
        wire(&g, 2, BIT(0)|BIT(1), 0);
        tick(&g);
        char n[32]; snprintf(n,32,"%d+%d",cases[i][0],cases[i][1]);
        check(n, cases[i][2], obs_raw(g.val[2]));
    }
}

static void test_subtraction(void) {
    printf("--- Subtraction ---\n");
    int cases[][3] = {{5,3,2},{10,10,0},{0,1,-1},{100,37,63}};
    for (int i = 0; i < 4; i++) {
        Grid g; grid_init(&g);
        g.val[0] = cases[i][0]; g.val[1] = cases[i][1];
        wire(&g, 2, BIT(0)|BIT(1), BIT(1));
        tick(&g);
        char n[32]; snprintf(n,32,"%d-%d",cases[i][0],cases[i][1]);
        check(n, cases[i][2], obs_raw(g.val[2]));
    }
}

static void test_multiplication(void) {
    printf("--- Multiplication ---\n");
    Grid g; grid_init(&g);
    g.val[0]=3; g.val[1]=3; g.val[2]=3; g.val[3]=3;
    wire(&g, 4, BIT(0)|BIT(1)|BIT(2)|BIT(3), 0);
    tick(&g);
    check("3*4", 12, obs_raw(g.val[4]));

    Grid g2; grid_init(&g2);
    g2.val[0]=7; g2.val[1]=7; g2.val[2]=7;
    wire(&g2, 3, BIT(0)|BIT(1)|BIT(2), 0);
    tick(&g2);
    check("7*3", 21, obs_raw(g2.val[3]));

    Grid g3; grid_init(&g3);
    for (int i=0;i<5;i++) g3.val[i]=5;
    wire(&g3, 5, BIT(0)|BIT(1)|BIT(2)|BIT(3)|BIT(4), 0);
    tick(&g3);
    check("5*5", 25, obs_raw(g3.val[5]));
}

static void test_comparison(void) {
    printf("--- Comparison ---\n");
    struct { int a,b,gt,eq,lt; } cases[] = {
        {5,3,1,0,0},{3,5,0,0,1},{4,4,0,1,0},{0,0,0,1,0},{100,1,1,0,0}
    };
    for (int i = 0; i < 5; i++) {
        Grid g; grid_init(&g);
        g.val[0] = cases[i].a; g.val[1] = cases[i].b;
        wire(&g, 2, BIT(0)|BIT(1), BIT(1));
        tick(&g);
        int32_t d = g.val[2];
        char n[32];
        snprintf(n,32,"%d>%d",cases[i].a,cases[i].b);  check(n, cases[i].gt, d>0?1:0);
        snprintf(n,32,"%d=%d",cases[i].a,cases[i].b);  check(n, cases[i].eq, d==0?1:0);
        snprintf(n,32,"%d<%d",cases[i].a,cases[i].b);  check(n, cases[i].lt, d<0?1:0);
    }
}

static void test_full_adder(void) {
    printf("--- Full adder ---\n");
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++)
            for (int cin = 0; cin <= 1; cin++) {
                Grid g; grid_init(&g);
                g.val[0] = a; g.val[1] = b; g.val[2] = cin;
                wire(&g, 3, BIT(0)|BIT(1)|BIT(2), 0);
                tick(&g);
                int total = a + b + cin;
                check("FA sum",  total&1,  obs_parity(g.val[3]));
                check("FA cout", total>>1, obs_all(g.val[3], 2));
            }
}

static void test_4bit_adder(void) {
    printf("--- 4-bit ripple adder ---\n");
    struct { int a,b,sum,cout; } cases[] = {
        {0,0,0,0},{1,1,2,0},{3,5,8,0},{7,8,15,0},
        {15,0,15,0},{15,1,0,1},{9,6,15,0},{10,11,5,1},
    };
    for (int t = 0; t < 8; t++) {
        int a=cases[t].a, b=cases[t].b, carry=0, sum=0;
        for (int i = 0; i < 4; i++) {
            Grid g; grid_init(&g);
            g.val[0]=(a>>i)&1; g.val[1]=(b>>i)&1; g.val[2]=carry;
            wire(&g, 3, BIT(0)|BIT(1)|BIT(2), 0);
            tick(&g);
            sum |= (obs_parity(g.val[3]) << i);
            carry = obs_all(g.val[3], 2);
        }
        char n[32]; snprintf(n,32,"4ADD %d+%d",a,b);
        check(n, cases[t].sum, sum);
        snprintf(n,32,"4ADD cout %d+%d",a,b);
        check(n, cases[t].cout, carry);
    }
}

static void test_neural_xor(void) {
    printf("--- Neural XOR ---\n");
    for (int a = 0; a <= 1; a++)
        for (int b = 0; b <= 1; b++) {
            /* Layer 1: ONE collision. TWO observers. */
            Grid g; grid_init(&g);
            g.val[0]=a; g.val[1]=b;
            wire(&g, 2, BIT(0)|BIT(1), 0);  /* a + b */
            tick(&g);
            int h0 = obs_bool(g.val[2]);          /* OR:   > 0 */
            int h1 = 1 - obs_all(g.val[2], 2);    /* NAND: NOT(>= 2) */

            /* Layer 2: combine observed values */
            Grid g2; grid_init(&g2);
            g2.val[0]=h0; g2.val[1]=h1;
            wire(&g2, 2, BIT(0)|BIT(1), 0);
            tick(&g2);
            check("NXOR", a^b, obs_all(g2.val[2], 2)); /* AND(OR, NAND) = XOR */
        }
}

static void test_counter(void) {
    printf("--- 2-bit counter ---\n");
    int r0=0, r1=0;
    int exp_r0[] = {1,0,1,0,1,0,1,0};
    int exp_r1[] = {0,1,1,0,0,1,1,0};
    for (int t = 0; t < 8; t++) {
        Grid g; grid_init(&g);
        g.val[0]=r0; g.val[1]=r1; g.val[2]=1;
        wire(&g, 3, BIT(0)|BIT(2), BIT(0));  /* 1+(-R0) */
        wire(&g, 4, BIT(0)|BIT(1), 0);       /* R0+R1 */
        tick(&g);
        r0 = obs_bool(g.val[3]);
        r1 = obs_parity(g.val[4]);
        char n[32]; snprintf(n,32,"CNT t%d",t+1);
        check(n, exp_r0[t]|(exp_r1[t]<<1), r0|(r1<<1));
    }
}

static void test_sr_latch(void) {
    printf("--- SR latch ---\n");
    int q = 0;
    struct { int s,r,eq; const char *d; } seq[] = {
        {1,0,1,"SET"},{0,0,1,"HOLD"},{0,1,0,"RESET"},
        {0,0,0,"HOLD"},{1,0,1,"SET"},{0,0,1,"HOLD"},
    };
    for (int t = 0; t < 6; t++) {
        Grid g; grid_init(&g);
        g.val[0]=seq[t].s; g.val[1]=seq[t].r; g.val[2]=q;
        wire(&g, 3, BIT(0)|BIT(1)|BIT(2), BIT(1));
        tick(&g);
        q = obs_bool(g.val[3]);
        char n[32]; snprintf(n,32,"SR %s",seq[t].d);
        check(n, seq[t].eq, q);
    }
}

static void test_traffic_fsm(void) {
    printf("--- Traffic FSM ---\n");
    int b0=0, b1=0;
    int exp_b0[] = {1,0,0, 1,0,0, 1,0,0};
    int exp_b1[] = {0,1,0, 0,1,0, 0,1,0};
    for (int t = 0; t < 9; t++) {
        Grid g1; grid_init(&g1);
        g1.val[0]=b0; g1.val[1]=b1; g1.val[2]=1;
        wire(&g1, 3, BIT(0)|BIT(2), BIT(0));
        wire(&g1, 4, BIT(1)|BIT(2), BIT(1));
        tick(&g1);
        int nb0 = obs_bool(g1.val[3]);
        int nb1 = obs_bool(g1.val[4]);

        Grid g2; grid_init(&g2);
        g2.val[0]=nb0; g2.val[1]=nb1; g2.val[2]=b0;
        wire(&g2, 3, BIT(0)|BIT(1), 0);
        wire(&g2, 4, BIT(2)|BIT(1), 0);
        tick(&g2);
        b0 = obs_all(g2.val[3], 2);
        b1 = obs_all(g2.val[4], 2);
        char n[32]; snprintf(n,32,"FSM t%d",t+1);
        check(n, exp_b0[t]|(exp_b1[t]<<1), b0|(b1<<1));
    }
}

int main(void) {
    printf("==========================================================\n");
    printf("  XYZT v5 -- Pure Accumulation Engine\n");
    printf("  One operation: signals arrive, position counts.\n");
    printf("  The observer makes the distinction.\n");
    printf("==========================================================\n\n");

    test_not();
    test_and();
    test_or();
    test_xor();
    test_majority();
    test_one_collision_all_ops();
    test_addition();
    test_subtraction();
    test_multiplication();
    test_comparison();
    test_full_adder();
    test_4bit_adder();
    test_neural_xor();
    test_counter();
    test_sr_latch();
    test_traffic_fsm();

    printf("\n==========================================================\n");
    printf("  RESULTS: %d passed, %d failed, %d total\n",
           g_pass, g_fail, g_pass + g_fail);
    printf("==========================================================\n");

    if (g_fail == 0) {
        printf("\n  ALL PASS.\n\n");
        printf("  The engine does ONE thing: accumulate at positions.\n");
        printf("  The observer creates every operation:\n");
        printf("    obs_bool   -> OR, NOT (with bias)\n");
        printf("    obs_all(n) -> AND, MAJORITY, CARRY\n");
        printf("    obs_parity -> XOR, SUM\n");
        printf("    obs_raw    -> addition, subtraction, multiplication\n");
        printf("    obs_sign   -> comparison (>, =, <)\n\n");
        printf("  Same collision. Different distinction.\n");
        printf("  The operation is not in the engine.\n");
        printf("  The operation is in the observation.\n\n");
    } else {
        printf("\n  FAILURES DETECTED.\n\n");
    }
    return g_fail;
}
