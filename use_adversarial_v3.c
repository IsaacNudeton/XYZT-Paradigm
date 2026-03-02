/*
 * use_adversarial_v3.c — Stress Test for USE v3 (8 features, fixed wiring)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include "use_engine.h"


/* ══════════════════════════════════════════════════════════════
 * TEST FRAMEWORK
 * ══════════════════════════════════════════════════════════════ */
static int g_pass=0, g_fail=0, g_sp=0, g_sf=0;
static void check(const char *nm, int cond) {
    if (cond) { g_pass++; g_sp++; } else { g_fail++; g_sf++; printf("  FAIL: %s\n",nm); }
}
static void section(const char *nm) { printf("\n== %s ==\n",nm); g_sp=0; g_sf=0; }
static void section_end(void) { printf("  -> %d passed, %d failed\n",g_sp,g_sf); }

static uint64_t rng_s = 0xCAFEBABE12345678ULL;
static uint64_t rng(void) { rng_s^=rng_s<<13; rng_s^=rng_s>>7; rng_s^=rng_s<<17; return rng_s; }

/* ══════════════════════════════════════════════════════════════
 * ADVERSARIAL TESTS
 * ══════════════════════════════════════════════════════════════ */

/* A1: Prediction exhaustive — every linear sequence with delta -50..+50, length 3..8 */
static void test_predict_linear_exhaustive(void) {
    section("PREDICTION: LINEAR EXHAUSTIVE (600 sequences)");
    for (int d = -50; d <= 50; d++) {
        if (d == 0) continue; /* delta=0 is constant, not linear */
        for (int len = 3; len <= 8; len++) {
            int vals[16]; vals[0] = 100;
            for (int i = 1; i < len; i++) vals[i] = vals[i-1] + d;
            int pt;
            int pred = predict_sequence(vals, len, &pt);
            int expected = vals[len-1] + d;
            char nm[64]; snprintf(nm,64,"lin_d%d_n%d",d,len);
            check(nm, pred == expected && pt == 2);
        }
    }
    section_end();
}

/* A2: Prediction — constant sequences, all values -100..100 */
static void test_predict_constant_exhaustive(void) {
    section("PREDICTION: CONSTANT EXHAUSTIVE (201 values)");
    for (int v = -100; v <= 100; v++) {
        int vals[5] = {v,v,v,v,v};
        int pt;
        int pred = predict_sequence(vals, 5, &pt);
        char nm[32]; snprintf(nm,32,"const_%d",v);
        check(nm, pred == v && pt == 1);
    }
    section_end();
}

/* A3: Prediction — geometric sequences (base 2-10, ratio 2-5) */
static void test_predict_geometric(void) {
    section("PREDICTION: GEOMETRIC (36 sequences)");
    for (int base = 2; base <= 10; base++) {
        for (int ratio = 2; ratio <= 5; ratio++) {
            int vals[6]; vals[0] = base;
            for (int i = 1; i < 6; i++) vals[i] = vals[i-1] * ratio;
            int pt;
            int pred = predict_sequence(vals, 5, &pt);
            int expected = vals[4] * ratio;
            char nm[32]; snprintf(nm,32,"geo_%d_%d",base,ratio);
            check(nm, pred == expected && pt == 3);
        }
    }
    section_end();
}

/* A4: Prediction — acceleration (constant 2nd derivative) */
static void test_predict_accel_exhaustive(void) {
    section("PREDICTION: ACCELERATION (200 sequences)");
    for (int a = -10; a <= 10; a++) {
        if (a == 0) continue; /* that's linear */
        for (int d0 = -5; d0 <= 5; d0++) {
            int vals[6]; vals[0] = 0;
            int delta = d0;
            for (int i = 1; i < 6; i++) { vals[i] = vals[i-1] + delta; delta += a; }
            int pt;
            int pred = predict_sequence(vals, 6, &pt);
            int expected = vals[5] + delta;
            char nm[32]; snprintf(nm,32,"acc_a%d_d%d",a,d0);
            check(nm, pred == expected);
        }
    }
    section_end();
}

/* A5: Observe — density correctness for all byte values */
static void test_observe_density_all_bytes(void) {
    section("OBSERVE: DENSITY ALL BYTES (256 values)");
    for (int v = 0; v < 256; v++) {
        uint8_t byte = (uint8_t)v;
        BitStream b; bs_from_bytes(&b, &byte, 1);
        Observation o = observe(&b);
        int expected = __builtin_popcount(v) * 100 / 8;
        char nm[32]; snprintf(nm,32,"dens_0x%02x",v);
        check(nm, o.density == expected);
    }
    section_end();
}

/* A6: Observe — period detection for known periods */
static void test_observe_period_known(void) {
    section("OBSERVE: PERIOD DETECTION (periods 2-32)");
    for (int p = 2; p <= 32; p++) {
        BitStream b; bs_init(&b);
        /* Generate pattern with exact period p, repeated enough times */
        for (int rep = 0; rep < 256 / p + 4; rep++)
            for (int i = 0; i < p && b.len < 512; i++)
                bs_push(&b, i < p/2 ? 1 : 0);
        Observation o = observe(&b);
        char nm[32]; snprintf(nm,32,"period_%d",p);
        /* Period should be exactly p, or a divisor of p */
        check(nm, o.period > 0 && p % o.period == 0);
    }
    section_end();
}

/* A7: Observe — symmetry of repeated bitstreams (first half == second half) */
static void test_observe_symmetry(void) {
    section("OBSERVE: SYMMETRY (repeated halves must be 100%)");
    for (int trial = 0; trial < 50; trial++) {
        BitStream b; bs_init(&b);
        int half = 16 + (rng() % 48);
        uint8_t bits[128];
        for (int i = 0; i < half; i++) bits[i] = rng() & 1;
        /* Build repeated: first half then same first half */
        for (int i = 0; i < half; i++) bs_push(&b, bits[i]);
        for (int i = 0; i < half; i++) bs_push(&b, bits[i]);
        Observation o = observe(&b);
        char nm[32]; snprintf(nm,32,"repeated_%d",trial);
        check(nm, o.symmetry == 100);
    }
    section_end();
}

/* A8: Engine silence convergence — must go quiet within 4 ingests for ANY constant input */
static void test_engine_silence_random(void) {
    section("ENGINE: SILENCE ON CONSTANT INPUT (100 random strings)");
    for (int trial = 0; trial < 100; trial++) {
        Engine e; engine_init(&e);
        /* Generate random string */
        char str[32];
        int len = 4 + (rng() % 20);
        for (int i = 0; i < len; i++) str[i] = 'A' + (rng() % 26);
        str[len] = '\0';

        int quiet_at = -1;
        for (int i = 0; i < 6; i++) {
            engine_ingest_string(&e, str);
            if (engine_data_residue(&e) == 0 && quiet_at < 0) quiet_at = i + 1;
        }
        char nm[32]; snprintf(nm,32,"silence_%d",trial);
        check(nm, quiet_at > 0 && quiet_at <= 4);
    }
    section_end();
}

/* A9: Engine wake — MUST wake on novel input after silence */
static void test_engine_wake(void) {
    section("ENGINE: WAKE ON NOVEL (100 trials)");
    for (int trial = 0; trial < 100; trial++) {
        Engine e; engine_init(&e);
        char str[32]; int len = 4 + (rng() % 16);
        for (int i = 0; i < len; i++) str[i] = 'a' + (rng() % 26);
        str[len] = '\0';
        for (int i = 0; i < 5; i++) engine_ingest_string(&e, str);
        int before = engine_data_residue(&e);

        /* Novel input: completely different */
        char novel[32]; int nlen = 4 + (rng() % 16);
        for (int i = 0; i < nlen; i++) novel[i] = '0' + (rng() % 10);
        novel[nlen] = '\0';
        engine_ingest_string(&e, novel);
        int after = engine_data_residue(&e);

        char nm[32]; snprintf(nm,32,"wake_%d",trial);
        check(nm, before == 0 && after > 0);
    }
    section_end();
}

/* A10: Self-wire determinism — same input sequence always produces same state */
static void test_selfwire_determinism(void) {
    section("SELF-WIRE: DETERMINISM (50 trials)");
    for (int trial = 0; trial < 50; trial++) {
        Engine e1, e2; engine_init(&e1); engine_init(&e2);
        for (int i = 0; i < 5; i++) {
            char s[16]; snprintf(s,16,"input_%d_%d",trial,i);
            engine_ingest_string(&e1, s);
            engine_ingest_string(&e2, s);
        }
        int match = 1;
        for (int p = 0; p < e1.grid.n_pos; p++) {
            if (e1.grid.val[p] != e2.grid.val[p]) { match = 0; break; }
            if (e1.grid.residue[p] != e2.grid.residue[p]) { match = 0; break; }
            if (e1.grid.reads[p] != e2.grid.reads[p]) { match = 0; break; }
        }
        for (int i = 0; i < 8; i++) if (e1.feedback[i] != e2.feedback[i]) match = 0;
        char nm[32]; snprintf(nm,32,"det_%d",trial);
        check(nm, match);
    }
    section_end();
}

/* A11: Values stay bounded (never exceed ±1M) under random input */
static void test_value_bounded(void) {
    section("ENGINE: VALUE BOUNDED (1000 random ingests)");
    Engine e; engine_init(&e);
    int bounded = 1;
    for (int i = 0; i < 1000; i++) {
        uint8_t data[64]; for (int j = 0; j < 64; j++) data[j] = rng() & 0xFF;
        engine_ingest(&e, data, 64);
        for (int p = 0; p < e.grid.n_pos; p++)
            if (e.grid.val[p] > 1000000 || e.grid.val[p] < -1000000) bounded = 0;
    }
    check("all values in [-1M, +1M]", bounded);
    section_end();
}

/* A12: Observe invariants — density always 0-100, symmetry always 0-100 */
static void test_observe_invariants(void) {
    section("OBSERVE: INVARIANTS (1000 random bitstreams)");
    for (int trial = 0; trial < 1000; trial++) {
        uint8_t data[64]; int len = 1 + (rng() % 64);
        for (int i = 0; i < len; i++) data[i] = rng() & 0xFF;
        BitStream b; bs_from_bytes(&b, data, len);
        Observation o = observe(&b);
        char nm[64];
        snprintf(nm,64,"inv_%d_dens",trial);
        check(nm, o.density >= 0 && o.density <= 100);
        snprintf(nm,64,"inv_%d_sym",trial);
        check(nm, o.symmetry >= 0 && o.symmetry <= 100);
        snprintf(nm,64,"inv_%d_period",trial);
        check(nm, o.period >= 0);
    }
    section_end();
}

/* A13: Empty and minimal inputs */
static void test_edge_inputs(void) {
    section("EDGE INPUTS");
    /* Empty bitstream */
    { BitStream b; bs_init(&b); Observation o = observe(&b); check("empty: density=0", o.density == 0); }
    /* Single bit */
    { BitStream b; bs_init(&b); bs_push(&b, 1); Observation o = observe(&b); check("single_1: density=100", o.density == 100); }
    { BitStream b; bs_init(&b); bs_push(&b, 0); Observation o = observe(&b); check("single_0: density=0", o.density == 0); }
    /* Max length */
    { BitStream b; bs_init(&b); for (int i = 0; i < BS_MAXBITS; i++) bs_push(&b, i&1);
      Observation o = observe(&b); check("maxlen: density~50", o.density >= 49 && o.density <= 51); }
    /* Engine with empty string */
    { Engine e; engine_init(&e); engine_ingest_string(&e, ""); check("empty_string: ticked", e.tick_count == 1); }
    /* Engine with single byte */
    { Engine e; engine_init(&e); uint8_t b = 0xFF; engine_ingest(&e, &b, 1); check("single_byte: ticked", e.tick_count == 1); }
    section_end();
}

/* A14: Benchmark */
static double tdiff(struct timespec a, struct timespec b) {
    return (b.tv_sec-a.tv_sec)*1000.0 + (b.tv_nsec-a.tv_nsec)/1e6;
}
static void test_benchmark(void) {
    section("BENCHMARK");
    struct timespec t0, t1;
    int N;

    /* Observe throughput */
    N = 10000;
    BitStream b; uint8_t data[64];
    for (int i=0;i<64;i++) data[i]=rng()&0xFF;
    bs_from_bytes(&b, data, 64);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    volatile int sink = 0;
    for (int i=0;i<N;i++) { Observation o = observe(&b); sink += o.density; }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("  observe(512-bit): %.1f ns/call (%d calls)\n", tdiff(t0,t1)*1e6/N, N);

    /* predict_sequence throughput */
    N = 100000;
    int vals[8] = {1,3,7,13,21,31,43,57};
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i=0;i<N;i++) { int pt; sink += predict_sequence(vals, 8, &pt); }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("  predict(8-val accel): %.1f ns/call (%d calls)\n", tdiff(t0,t1)*1e6/N, N);

    /* Full engine ingest */
    N = 1000;
    Engine e; engine_init(&e);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i=0;i<N;i++) { engine_ingest_string(&e, "benchmark input string"); }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("  engine_ingest(22 bytes): %.1f us/call (%d calls)\n", tdiff(t0,t1)*1e3/N, N);

    (void)sink;
    section_end();
}

int main(void) {
    printf("==================================================\n");
    printf("  USE ADVERSARIAL STRESS TEST\n");
    printf("==================================================\n");

    test_predict_constant_exhaustive();
    test_predict_linear_exhaustive();
    test_predict_geometric();
    test_predict_accel_exhaustive();
    test_observe_density_all_bytes();
    test_observe_period_known();
    test_observe_symmetry();
    test_observe_invariants();
    test_edge_inputs();
    test_engine_silence_random();
    test_engine_wake();
    test_selfwire_determinism();
    test_value_bounded();
    test_benchmark();

    printf("\n==================================================\n");
    printf("  FINAL: %d passed, %d failed, %d total\n", g_pass, g_fail, g_pass+g_fail);
    printf("==================================================\n");
    if (g_fail==0) printf("\n  USE survived adversarial testing.\n\n");
    else printf("\n  CRACKS: %d failures.\n\n", g_fail);
    return g_fail;
}
