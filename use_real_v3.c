#include "use_engine.h"

/* === HELPERS === */
static void print_wiring(const Engine *e) {
    printf("  wiring: ");
    for (int p = COMPUTE_START; p < COMPUTE_END; p++) {
        if (e->grid.reads[p] == 0) continue;
        printf("pos%d<-{", p);
        uint64_t rd = e->grid.reads[p];
        int first = 1;
        while (rd) {
            int b = __builtin_ctzll(rd);
            if (!first) printf(",");
            printf("%s%d", (e->grid.invert[p] & BIT(b)) ? "-" : "", b);
            first = 0;
            rd &= rd - 1;
        }
        printf("} ");
    }
    printf("\n");
}

static void print_edge_weights(const Engine *e) {
    printf("  strong edges: ");
    int count = 0;
    for (int q = 0; q < e->grid.n_pos; q++)
        for (int p = COMPUTE_START; p < COMPUTE_END; p++)
            if (e->grid.edge_w[q][p] > WIRE_THRESH + 8) {
                printf("[%d->%d w=%d%s] ", q, p, e->grid.edge_w[q][p],
                       e->grid.edge_inv[q][p] ? " inv" : "");
                count++;
            }
    if (count == 0) printf("(none above threshold+8)");
    printf("\n");
}

/* === EXPERIMENTS === */

static void exp_english(void) {
    printf("========================================\n");
    printf("  EXP 1: English prose\n");
    printf("========================================\n");
    Engine e; engine_init(&e);

    const char *sentences[] = {
        "The quick brown fox jumps over the lazy dog.",
        "She sells seashells by the seashore.",
        "To be or not to be, that is the question.",
        "All that glitters is not gold.",
        "A stitch in time saves nine.",
        "The early bird catches the worm.",
        "Actions speak louder than words.",
        "Knowledge is power and power is knowledge.",
        "Every cloud has a silver lining.",
        "Where there is a will there is a way.",
        "Practice makes perfect in everything.",
        "The pen is mightier than the sword.",
    };

    for (int i = 0; i < 12; i++) {
        engine_ingest_string(&e, sentences[i]);
        printf("  [%2d] dr=%4d tr=%6d obs: dens=%d sym=%d per=%d pred1=%d pred0=%d\n",
               i + 1, engine_data_residue(&e), engine_total_residue(&e),
               e.analysis.density, e.analysis.symmetry, e.analysis.period,
               e.analysis.predicted_next_one, e.analysis.predicted_next_zero);
    }
    print_wiring(&e);
    print_edge_weights(&e);

    printf("\n  Fingerprint: English ASCII lives at density ~48-52, low symmetry,\n");
    printf("  no strong period. The engine should learn this within ~3 ingests.\n");
    int quiet = 0;
    for (int i = 0; i < 4; i++) {
        engine_ingest_string(&e, "The quick brown fox jumps over the lazy dog.");
        if (engine_data_residue(&e) == 0) quiet++;
    }
    printf("  Repeated sentence 4x: quiet %d/4 times\n\n", quiet);
}

static void exp_source_code(void) {
    printf("========================================\n");
    printf("  EXP 2: C source code\n");
    printf("========================================\n");
    Engine e; engine_init(&e);

    const char *lines[] = {
        "#include <stdio.h>",
        "#include <stdlib.h>",
        "#include <string.h>",
        "int main(void) {",
        "    int x = 0;",
        "    for (int i = 0; i < 10; i++) {",
        "        x += i * i;",
        "    }",
        "    printf(\"%d\\n\", x);",
        "    return 0;",
        "}",
        "#include <stdio.h>",  /* repeat to test learning */
    };

    for (int i = 0; i < 12; i++) {
        engine_ingest_string(&e, lines[i]);
        printf("  [%2d] dr=%4d tr=%6d obs: dens=%d sym=%d per=%d | \"%s\"\n",
               i + 1, engine_data_residue(&e), engine_total_residue(&e),
               e.analysis.density, e.analysis.symmetry, e.analysis.period,
               lines[i]);
    }
    print_wiring(&e);

    printf("\n  Code vs prose: different density (more low-ASCII = fewer high bits),\n");
    printf("  #include lines repeat → engine should catch that.\n\n");
}

static void exp_synthetic_sensors(void) {
    printf("========================================\n");
    printf("  EXP 3: Synthetic sensor signals\n");
    printf("========================================\n");

    /* Sawtooth */
    {
        Engine e; engine_init(&e);
        printf("  --- Sawtooth (0,1,2,...,255,0,1,...) ---\n");
        for (int chunk = 0; chunk < 8; chunk++) {
            uint8_t data[32];
            for (int i = 0; i < 32; i++) data[i] = (chunk * 32 + i) & 0xFF;
            engine_ingest(&e, data, 32);
            printf("  [%d] dr=%4d dens=%d per=%d\n",
                   chunk + 1, engine_data_residue(&e), e.analysis.density, e.analysis.period);
        }
        print_wiring(&e);
    }

    /* Square wave */
    {
        Engine e; engine_init(&e);
        printf("\n  --- Square wave (32 high, 32 low) ---\n");
        for (int chunk = 0; chunk < 8; chunk++) {
            uint8_t data[32];
            uint8_t val = ((chunk / 2) % 2) ? 0xFF : 0x00;
            memset(data, val, 32);
            engine_ingest(&e, data, 32);
            printf("  [%d] dr=%4d dens=%d per=%d val=0x%02X\n",
                   chunk + 1, engine_data_residue(&e), e.analysis.density,
                   e.analysis.period, val);
        }
        print_wiring(&e);
    }

    /* PRNG noise */
    {
        Engine e; engine_init(&e);
        printf("\n  --- PRNG noise ---\n");
        uint32_t seed = 0xDEADBEEF;
        for (int chunk = 0; chunk < 8; chunk++) {
            uint8_t data[32];
            for (int i = 0; i < 32; i++) {
                seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
                data[i] = seed & 0xFF;
            }
            engine_ingest(&e, data, 32);
            printf("  [%d] dr=%4d dens=%d per=%d\n",
                   chunk + 1, engine_data_residue(&e), e.analysis.density, e.analysis.period);
        }
        printf("  Noise: density ~50, no period, high residue throughout.\n");
    }
    printf("\n");
}

static void exp_self_feeding(void) {
    printf("========================================\n");
    printf("  EXP 4: Self-feeding loop\n");
    printf("========================================\n");
    Engine e; engine_init(&e);
    engine_ingest_string(&e, "bootstrap seed");
    printf("  seed: dr=%d tr=%d\n", engine_data_residue(&e), engine_total_residue(&e));

    for (int i = 0; i < 20; i++) {
        engine_ingest(&e, (uint8_t *)e.feedback, sizeof(e.feedback));
        printf("  loop %2d: dr=%4d tr=%6d fb=[%d,%d,%d,%d,%d,%d,%d,%d]\n",
               i + 1, engine_data_residue(&e), engine_total_residue(&e),
               e.feedback[0], e.feedback[1], e.feedback[2], e.feedback[3],
               e.feedback[4], e.feedback[5], e.feedback[6], e.feedback[7]);
    }
    print_wiring(&e);
    print_edge_weights(&e);
    printf("\n  Self-feeding: system should converge to fixed point or cycle.\n\n");
}

static void exp_domain_fingerprints(void) {
    printf("========================================\n");
    printf("  EXP 5: Domain fingerprints\n");
    printf("========================================\n");

    struct { const char *name; const uint8_t *data; int len; } domains[5];

    /* English */
    const char *eng = "The quick brown fox jumps over the lazy dog again and again.";
    domains[0].name = "english"; domains[0].data = (const uint8_t *)eng; domains[0].len = strlen(eng);

    /* C code */
    const char *code = "for (int i = 0; i < n; i++) { sum += arr[i]; } return sum;";
    domains[1].name = "c_code"; domains[1].data = (const uint8_t *)code; domains[1].len = strlen(code);

    /* Binary ramp */
    uint8_t ramp[64]; for (int i = 0; i < 64; i++) ramp[i] = i * 4;
    domains[2].name = "ramp"; domains[2].data = ramp; domains[2].len = 64;

    /* Square */
    uint8_t square[64]; for (int i = 0; i < 64; i++) square[i] = (i / 8) % 2 ? 0xFF : 0x00;
    domains[3].name = "square"; domains[3].data = square; domains[3].len = 64;

    /* Random */
    uint8_t noise[64]; uint32_t s = 42;
    for (int i = 0; i < 64; i++) { s ^= s<<13; s ^= s>>17; s ^= s<<5; noise[i] = s & 0xFF; }
    domains[4].name = "noise"; domains[4].data = noise; domains[4].len = 64;

    printf("  %10s %6s %6s %6s %6s %6s %6s\n",
           "domain", "dens", "sym", "period", "runs1", "runs0", "pat");
    printf("  %10s %6s %6s %6s %6s %6s %6s\n",
           "------", "----", "---", "------", "-----", "-----", "---");

    for (int d = 0; d < 5; d++) {
        BitStream b; bs_from_bytes(&b, domains[d].data, domains[d].len);
        Observation o = observe(&b);
        printf("  %10s %5d%% %5d%% %6d %5d %5d  %d/%d\n",
               domains[d].name, o.density, o.symmetry, o.period,
               o.n_ones, o.n_zeros, o.ones_pattern, o.zeros_pattern);
    }

    printf("\n  Each domain has a distinct fingerprint.\n");
    printf("  English/code: high density (~48-55%%), many short runs.\n");
    printf("  Ramp: ~50%% density, structured runs.\n");
    printf("  Square: bimodal density, clear period.\n");
    printf("  Noise: ~50%% density, no period, no pattern.\n\n");
}

static void exp_wiring_evolution(void) {
    printf("========================================\n");
    printf("  EXP 6: Wiring evolution over time\n");
    printf("========================================\n");
    Engine e; engine_init(&e);

    /* Feed alternating domains: text then binary then text... */
    const char *text = "the same english sentence repeated";
    uint8_t binary[32]; for (int i = 0; i < 32; i++) binary[i] = (i % 4 < 2) ? 0xAA : 0x55;

    for (int round = 0; round < 12; round++) {
        if (round % 2 == 0)
            engine_ingest_string(&e, text);
        else
            engine_ingest(&e, binary, 32);

        int edges = 0;
        for (int p = COMPUTE_START; p < COMPUTE_END; p++)
            if (e.grid.reads[p] != 0) edges++;

        printf("  [%2d] %s dr=%4d edges=%d",
               round + 1, round % 2 == 0 ? "text  " : "binary", engine_data_residue(&e), edges);

        /* Show strongest edge weight */
        int max_w = 0;
        for (int q = 0; q < e.grid.n_pos; q++)
            for (int p = COMPUTE_START; p < COMPUTE_END; p++)
                if (e.grid.edge_w[q][p] > max_w) max_w = e.grid.edge_w[q][p];
        printf(" max_w=%d\n", max_w);
    }
    print_wiring(&e);
    printf("\n  Alternating domains: wiring should oscillate or find\n");
    printf("  invariant structure that spans both.\n\n");
}

int main(void) {
    printf("================================================\n");
    printf("  USE — Real Data Experiments\n");
    printf("  Does the engine find structure in the wild?\n");
    printf("================================================\n\n");

    exp_english();
    exp_source_code();
    exp_synthetic_sensors();
    exp_self_feeding();
    exp_domain_fingerprints();
    exp_wiring_evolution();

    printf("================================================\n");
    printf("  Done. Read the numbers.\n");
    printf("================================================\n");
    return 0;
}
