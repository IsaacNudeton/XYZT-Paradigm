/*
 * PICO INTEGRATION GUIDE — sense.c into main.c
 *
 * This file shows the exact changes needed to replace the hardcoded
 * protocol detection in the Pico firmware with the unsupervised
 * learning loop.
 *
 * Files to add to the Pico project:
 *   sense.c       — feature extraction + Hebbian learning
 *   pio_adapt.h   — PIO sample buffer → capture_buf_t adapter
 *   wire.c        — Hebbian graph (already exists in Pi firmware)
 *
 * Files to modify:
 *   main.c        — replace detect_protocol(), add wire graph, add commands
 *   CMakeLists.txt — add sense.c and wire.c to build
 *
 * What gets DELETED from main.c:
 *   - typedef enum { PROTO_UNKNOWN, PROTO_UART, PROTO_SPI, PROTO_I2C, PROTO_RAW } Protocol;
 *   - static Protocol detect_protocol(uint16_t *history, int len) { ... }
 *   - All references to Protocol enum and detect_protocol()
 *
 * What gets ADDED:
 *   - #include "pio_adapt.h" (after v6_core.h)
 *   - Wire graph state (global, like on Pi)
 *   - cmd_scan() rewritten to use sense pipeline
 *   - cmd_learn() — continuous learn loop
 *   - cmd_wgraph() — show wire graph state
 *   - sense_tick() replaces raw v6_tick() in auto mode
 */

/* ═══════════════════════════════════════════════════════════════
 * ADD TO INCLUDES (after #include "v6_core.h"):
 * ═══════════════════════════════════════════════════════════════ */

// #include "pio_adapt.h"

/* ═══════════════════════════════════════════════════════════════
 * ADD GLOBAL STATE (after static float current_div):
 * ═══════════════════════════════════════════════════════════════ */

// static capture_buf_t sense_cap;     /* adapter output */
// static sense_pass_t  sense_pass;    /* current feature set */

/* ═══════════════════════════════════════════════════════════════
 * REPLACE cmd_scan() — was hardcoded detection, now learns:
 * ═══════════════════════════════════════════════════════════════ */

/*
static void cmd_scan(void) {
    int scan_len = 2048;
    printf("\n  Scanning %d ticks @ %.1f MHz...\n", scan_len, 125.0f / current_div);

    memset(sample_buf, 0, sizeof(sample_buf));
    sampler_init(current_div);
    sampler_start();
    sleep_ms((int)(scan_len * current_div / 125000.0f) + 50);
    sampler_stop();

    // Convert PIO samples to capture buffer
    pio_to_capture(sample_buf, scan_len, current_div / 125.0f,
                   (1u << N_PINS) - 1, &sense_cap);

    // Extract features and learn
    sense_extract(&sense_cap, &sense_pass);
    sense_decay(&sense_pass);

    // Report features, not protocol names
    printf("\n  Captured: %d edges from %d samples\n", sense_cap.count, scan_len);
    sense_print_pass(&sense_pass);

    // Show pin activity (keep this, it's useful)
    uint16_t history[2048];
    int transitions[N_PINS] = {0};
    for (int i = 0; i < scan_len; i++)
        history[i] = sample_buf[i] & 0xFFFF;
    for (int i = 1; i < scan_len; i++) {
        uint16_t edges = history[i-1] ^ history[i];
        for (int p = 0; p < N_PINS; p++)
            if (edges & (1 << p)) transitions[p]++;
    }

    printf("\n  Pin activity:\n");
    for (int p = 0; p < N_PINS; p++) {
        printf("  GP%-2d | ", p);
        int bar = transitions[p] * 40 / (scan_len + 1);
        for (int b = 0; b < bar; b++) printf("#");
        if (transitions[p] > 0) printf(" %d", transitions[p]);
        printf("\n");
    }
    printf("\n");
}
*/

/* ═══════════════════════════════════════════════════════════════
 * ADD cmd_learn() — continuous scan+learn loop:
 * ═══════════════════════════════════════════════════════════════ */

/*
static void cmd_learn(int rounds) {
    if (rounds <= 0) rounds = 10;
    if (rounds > 1000) rounds = 1000;
    int scan_len = 2048;

    printf("\n  Learning: %d rounds @ %.1f MHz\n", rounds, 125.0f / current_div);

    for (int r = 0; r < rounds; r++) {
        // Capture
        memset(sample_buf, 0, sizeof(sample_buf));
        sampler_init(current_div);
        sampler_start();
        sleep_ms((int)(scan_len * current_div / 125000.0f) + 50);
        sampler_stop();

        // Convert + extract + learn
        pio_to_capture(sample_buf, scan_len, current_div / 125.0f,
                       (1u << N_PINS) - 1, &sense_cap);
        sense_extract(&sense_cap, &sense_pass);
        sense_decay(&sense_pass);

        // Progress
        if ((r + 1) % 10 == 0 || r == rounds - 1)
            printf("  [%d/%d] %d features, %d nodes, %d edges\n",
                   r + 1, rounds, sense_pass.n_features,
                   wire_get()->n_nodes, wire_get()->n_edges);

        // Abort on keypress
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) {
            printf("  Aborted at round %d\n", r + 1);
            break;
        }
    }

    printf("\n  Final graph:\n");
    wire_print();
    printf("\n");
}
*/

/* ═══════════════════════════════════════════════════════════════
 * ADD TO COMMAND DISPATCH (in the main while loop):
 * ═══════════════════════════════════════════════════════════════ */

/*
    // Replace this line:
    //   Protocol proto = detect_protocol(history, scan_len);
    //   const char *names[] = {"UNKNOWN", "UART", "SPI", "I2C", "RAW"};
    //   printf("\n  Detected: %s\n\n", names[proto]);

    // With: (already in cmd_scan above)
    //   sense_extract + sense_print_pass

    // Add these commands to the REPL parser:
    } else if (strncmp(cmd, "learn", 5) == 0) {
        int rounds = 10;
        if (len > 6) rounds = atoi(cmd + 6);
        cmd_learn(rounds);
    } else if (strcmp(cmd, "wgraph") == 0) {
        wire_print();
    } else if (strcmp(cmd, "wreset") == 0) {
        wire_reset();
    } else if (strncmp(cmd, "wquery ", 7) == 0) {
        wire_query(cmd + 7);
*/

/* ═══════════════════════════════════════════════════════════════
 * ADD TO help text:
 * ═══════════════════════════════════════════════════════════════ */

/*
    printf("  Learning:\n");
    printf("    scan            Capture + extract + learn\n");
    printf("    learn [N]       N rounds of scan+learn\n");
    printf("    wgraph          Show wire graph\n");
    printf("    wquery <name>   Query node connections\n");
    printf("    wreset          Clear wire graph\n");
*/

/* ═══════════════════════════════════════════════════════════════
 * REPLACE auto mode tick with observer feedback:
 * ═══════════════════════════════════════════════════════════════ */

/*
    // In cmd_auto(), replace:
    //   v6_tick(&grid);
    // With:
    //   v6_tick(&grid);
    //   // Observer feedback: propagate for internal positions
    //   for (int p = GPIO_WIDTH; p < V6_MAX_POS; p++) {
    //       if (!grid.active[p]) continue;
    //       v6_mark(&grid, p, v6_obs_any(grid.co_present[p]));
    //   }
*/

/* ═══════════════════════════════════════════════════════════════
 * CMakeLists.txt changes:
 * ═══════════════════════════════════════════════════════════════ */

/*
    add_executable(pico_xyzt
        main.c
        wire.c
        sense.c
    )
*/

/* ═══════════════════════════════════════════════════════════════
 * EXPECTED SERIAL OUTPUT when I2C sensor on GPIO 2,3:
 *
 *   v6> scan
 *     Scanning 2048 ticks @ 1.0 MHz...
 *     Captured: 347 edges from 2048 samples
 *     Features: 7
 *       A2  hits=1
 *       A3  hits=1
 *       D2  hits=1
 *       D3  hits=1
 *       C2.3  hits=1
 *       B2  hits=1
 *       B3  hits=1
 *
 *   v6> learn 50
 *     Learning: 50 rounds @ 1.0 MHz
 *     [10/50] 7 features, 7 nodes, 42 edges
 *     [20/50] 7 features, 7 nodes, 42 edges
 *     [30/50] 7 features, 7 nodes, 42 edges
 *     [40/50] 7 features, 7 nodes, 42 edges
 *     [50/50] 7 features, 7 nodes, 42 edges
 *
 *     Final graph:
 *       7 nodes, 42 edges, 350 learns
 *       [0] idea hits=100 A2
 *       [1] idea hits=100 A3
 *       ...
 *       *** w=250 p=50 f=0  A2 -> A3
 *       *** w=250 p=50 f=0  A2 -> D2
 *       ...
 *
 *   (All edges at w=250. One solid cluster. No labels.)
 *
 *   Now connect SPI device on GPIO 0,1,4 and run learn again:
 *   Two clusters form. The system knows they're different
 *   because different invariants co-occur. It doesn't know
 *   what they're called. It doesn't need to.
 * ═══════════════════════════════════════════════════════════════ */
