// ═══════════════════════════════════════════════════════════════
//  XYZT v6 Edge Node — Pico RP2040
//
//  The v6 co-presence engine running on real hardware.
//  GPIO pins ARE grid positions. Voltage IS distinction.
//  PIO sampling IS tick(). Protocol detection IS observation.
//
//  This firmware makes ANY RP2040 board a v6 compute node.
//  The PC is the heavy observer. The Pico is the edge.
//  USB serial is the mesh lane.
//
//  Grid layout (64 positions):
//    [0-15]   GPIO bridge (physical pins mirror grid)
//    [16-31]  User data / scratch
//    [32-47]  Program word (16-bit wiring diagram)
//    [48-49]  Observer selector (2-bit)
//    [50]     Execution output
//    [51-63]  Scratch
//
//  To port to another board:
//    1. Include v6_core.h (same engine, no changes)
//    2. Replace the HAL section below (~30 lines)
//    3. Everything else stays identical
//
//  Isaac Oravec & Claude — February 2026
// ═══════════════════════════════════════════════════════════════

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/unique_id.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "sampler.pio.h"
#include "v6_core.h"

// ─── Grid Layout Constants ──────────────────────────────────

#define GPIO_BASE   0
#define GPIO_WIDTH  16
#define PROG_BASE   32
#define PROG_WIDTH  16
#define OBS_BASE    48
#define OUT_POS     50

// ─── Hardware Config ────────────────────────────────────────

#define N_PINS        16
#define BASE_PIN      0
#define SAMPLE_BUF    4096
#define DEFAULT_DIV   125.0f      // 1 MHz sample rate

// ─── Global State ───────────────────────────────────────────

static V6Grid grid;                      // THE v6 engine
static uint32_t sample_buf[SAMPLE_BUF];  // PIO DMA ring buffer
static float current_div = DEFAULT_DIV;

// ═════════════════════════════════════════════════════════════
// HAL — RP2040-specific (replace this section for other boards)
//
// ~30 lines. This is ALL that changes per board.
// ═════════════════════════════════════════════════════════════

static void hal_init_pins(void) {
    for (int i = BASE_PIN; i < BASE_PIN + N_PINS; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_IN);
        gpio_pull_down(i);
    }
}

static uint32_t hal_read_pins(void) {
    return gpio_get_all() & ((1u << N_PINS) - 1);
}

static void hal_write_pins(uint32_t mask, uint32_t value) {
    for (int i = 0; i < N_PINS; i++) {
        if (mask & (1u << i)) {
            gpio_set_dir(i, GPIO_OUT);
            gpio_put(i, (value >> i) & 1);
        }
    }
}

static void hal_restore_inputs(uint32_t mask) {
    for (int i = 0; i < N_PINS; i++) {
        if (mask & (1u << i)) {
            gpio_set_dir(i, GPIO_IN);
            gpio_pull_down(i);
        }
    }
}

static int hal_pin_count(void) { return N_PINS; }
static const char *hal_board_id(void) { return "pico_rp2040"; }

// ═════════════════════════════════════════════════════════════
// Bridge — GPIO ↔ Grid positions
// ═════════════════════════════════════════════════════════════

static void bridge_sample(void) {
    uint32_t pins = hal_read_pins();
    for (int i = 0; i < N_PINS; i++) {
        v6_mark(&grid, GPIO_BASE + i, (pins >> i) & 1);
    }
}

static void bridge_output(uint32_t mask) {
    uint32_t value = 0;
    for (int i = 0; i < N_PINS; i++) {
        if (grid.marked[GPIO_BASE + i]) value |= (1u << i);
    }
    hal_write_pins(mask, value);
}

// ═════════════════════════════════════════════════════════════
// PIO Sampler — fast 125MHz acquisition (RP2040-specific)
// ═════════════════════════════════════════════════════════════

static PIO pio_hw;
static uint pio_sm;
static int dma_chan;
static int pio_initialized = 0;

static void sampler_init(float div) {
    pio_hw = pio0;
    pio_sm = pio_claim_unused_sm(pio_hw, true);
    uint offset = pio_add_program(pio_hw, &sampler_program);
    pio_sm_config c = sampler_program_get_default_config(offset);

    sm_config_set_in_pins(&c, BASE_PIN);
    sm_config_set_in_shift(&c, false, true, N_PINS);
    sm_config_set_clkdiv(&c, div);

    for (int i = BASE_PIN; i < BASE_PIN + N_PINS; i++) {
        pio_gpio_init(pio_hw, i);
        gpio_set_dir(i, GPIO_IN);
        gpio_pull_down(i);
    }

    pio_sm_init(pio_hw, pio_sm, offset, &c);

    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config dc = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, false);
    channel_config_set_write_increment(&dc, true);
    channel_config_set_ring(&dc, true, 14);  // wrap at 4096 words
    channel_config_set_dreq(&dc, pio_get_dreq(pio_hw, pio_sm, false));

    dma_channel_configure(dma_chan, &dc,
        sample_buf, &pio_hw->rxf[pio_sm],
        0xFFFFFFFF, false);

    pio_initialized = 1;
}

static void sampler_start(void) {
    dma_channel_start(dma_chan);
    pio_sm_set_enabled(pio_hw, pio_sm, true);
}

static void sampler_stop(void) {
    pio_sm_set_enabled(pio_hw, pio_sm, false);
    dma_channel_abort(dma_chan);
}

// ═════════════════════════════════════════════════════════════
// Protocol Detection (Z-layer observer)
//
// Analyzes transition patterns across ticks.
// Same co-presence data, different observation = different protocol.
// ═════════════════════════════════════════════════════════════

typedef enum {
    PROTO_UNKNOWN, PROTO_UART, PROTO_SPI, PROTO_I2C, PROTO_RAW
} Protocol;

static Protocol detect_protocol(uint16_t *history, int len) {
    if (len < 100) return PROTO_RAW;

    int transitions[N_PINS] = {0};
    for (int i = 1; i < len; i++) {
        uint16_t edges = history[i-1] ^ history[i];
        for (int p = 0; p < N_PINS; p++) {
            if (edges & (1 << p)) transitions[p]++;
        }
    }

    int max_trans = 0, max_pin = -1;
    int second_trans = 0, second_pin = -1;
    for (int p = 0; p < N_PINS; p++) {
        if (transitions[p] > max_trans) {
            second_trans = max_trans; second_pin = max_pin;
            max_trans = transitions[p]; max_pin = p;
        } else if (transitions[p] > second_trans) {
            second_trans = transitions[p]; second_pin = p;
        }
    }

    if (max_pin < 0) return PROTO_RAW;

    int high_count = 0;
    for (int i = 0; i < len; i++) {
        if (history[i] & (1 << max_pin)) high_count++;
    }
    float duty = (float)high_count / len;
    int looks_like_clock = (duty > 0.35f && duty < 0.65f);

    if (looks_like_clock && second_pin >= 0) {
        for (int p = 0; p < N_PINS; p++) {
            if (p == max_pin || p == second_pin) continue;
            int low_count = 0;
            for (int i = 0; i < len; i++) {
                if (!(history[i] & (1 << p))) low_count++;
            }
            if ((float)low_count / len > 0.3f) return PROTO_SPI;
        }
    }

    if (max_pin >= 0 && second_pin >= 0) {
        int both_high = 1;
        int pins[2] = {max_pin, second_pin};
        for (int j = 0; j < 2; j++) {
            int hi = 0;
            for (int i = 0; i < len; i++) {
                if (history[i] & (1 << pins[j])) hi++;
            }
            if ((float)hi / len < 0.6f) both_high = 0;
        }
        if (both_high && !looks_like_clock) return PROTO_I2C;
    }

    if (!looks_like_clock && max_trans > 10) return PROTO_UART;
    return PROTO_RAW;
}

// ═════════════════════════════════════════════════════════════
// Helper: print a set of positions from a bitmask
// ═════════════════════════════════════════════════════════════

static void print_set(uint64_t mask, int max_pos) {
    printf("{");
    int first = 1;
    for (int i = 0; i < max_pos; i++) {
        if (mask & V6_BIT(i)) {
            if (!first) printf(",");
            printf("%d", i);
            first = 0;
        }
    }
    printf("}");
}

// ═════════════════════════════════════════════════════════════
// Hardware Commands (PIO-based, fast acquisition)
// ═════════════════════════════════════════════════════════════

static void cmd_help(void) {
    printf("\n");
    printf("  ═══ XYZT v6 Edge Node ═══\n\n");
    printf("  Hardware:\n");
    printf("    id              Board identity\n");
    printf("    pins            Current GPIO state\n");
    printf("    scan            Auto-detect protocol\n");
    printf("    raw [N]         Dump N raw samples\n");
    printf("    monitor         Live pin display\n");
    printf("    edges           Transition analysis\n");
    printf("    speed <MHz>     Set sample rate\n");
    printf("\n");
    printf("  v6 Engine:\n");
    printf("    grid            Show grid state\n");
    printf("    mark <P> <V>    Mark position P (0/1)\n");
    printf("    wire <P> <M>    Wire pos P to hex mask M\n");
    printf("    tick            Execute tick\n");
    printf("    obs <P> <T>     Observe (any/all/par/cnt)\n");
    printf("    sample          GPIO -> grid [0-15]\n");
    printf("    output [M]      Grid [0-15] -> GPIO (hex mask)\n");
    printf("    word <B> <V>    Write word at base B (hex V)\n");
    printf("    read <B> [W]    Read word at base B width W\n");
    printf("    exec [B]        OS execute (data base B)\n");
    printf("    reset           Clear grid\n");
    printf("    auto <ms>       Continuous sample+tick (0=stop)\n");
    printf("\n");
    printf("  help              This message\n\n");
}

static void cmd_id(void) {
    pico_unique_board_id_t uid;
    pico_get_unique_board_id(&uid);
    printf("\n  Board:   %s\n", hal_board_id());
    printf("  Pins:    %d (GPIO %d-%d)\n", hal_pin_count(), BASE_PIN, BASE_PIN + N_PINS - 1);
    printf("  Grid:    %d positions\n", V6_MAX_POS);
    printf("  UID:     ");
    for (int i = 0; i < PICO_UNIQUE_BOARD_ID_SIZE_BYTES; i++)
        printf("%02X", uid.id[i]);
    printf("\n  Speed:   %.1f MHz\n", 125.0f / current_div);
    printf("  Engine:  v6 co-presence\n\n");
}

static void cmd_pins(void) {
    uint32_t state = hal_read_pins();
    printf("\n  GPIO co-presence snapshot:\n  ");
    for (int i = N_PINS - 1; i >= 0; i--) {
        printf("%d", (state >> i) & 1);
        if (i % 4 == 0 && i > 0) printf(" ");
    }
    printf("\n  ");
    for (int i = N_PINS - 1; i >= 0; i--) {
        printf("%c", (state & (1 << i)) ? '^' : '.');
        if (i % 4 == 0 && i > 0) printf(" ");
    }
    printf("\n  Marked: %d / %d\n\n", v6_obs_count(state), N_PINS);
}

static void cmd_scan(void) {
    int scan_len = 2048;
    printf("\n  Scanning %d ticks @ %.1f MHz...\n", scan_len, 125.0f / current_div);

    memset(sample_buf, 0, sizeof(sample_buf));
    sampler_init(current_div);
    sampler_start();
    sleep_ms((int)(scan_len * current_div / 125000.0f) + 50);
    sampler_stop();

    uint16_t history[2048];
    int transitions[N_PINS] = {0};
    for (int i = 0; i < scan_len; i++)
        history[i] = sample_buf[i] & 0xFFFF;
    for (int i = 1; i < scan_len; i++) {
        uint16_t edges = history[i-1] ^ history[i];
        for (int p = 0; p < N_PINS; p++)
            if (edges & (1 << p)) transitions[p]++;
    }

    Protocol proto = detect_protocol(history, scan_len);
    const char *names[] = {"UNKNOWN", "UART", "SPI", "I2C", "RAW"};
    printf("\n  Detected: %s\n\n", names[proto]);
    printf("  Pin activity:\n");
    for (int p = 0; p < N_PINS; p++) {
        printf("  GP%-2d | ", p);
        int bar = transitions[p] * 40 / (scan_len + 1);
        for (int b = 0; b < bar; b++) printf("#");
        if (transitions[p] > 0) printf(" %d", transitions[p]);
        if (transitions[p] > scan_len / 4) printf(" <-clk?");
        else if (transitions[p] > scan_len / 20) printf(" <-data?");
        else if (transitions[p] > 0) printf(" <-ctrl?");
        printf("\n");
    }
    printf("\n");
}

static void cmd_raw(int count) {
    if (count <= 0) count = 64;
    if (count > SAMPLE_BUF) count = SAMPLE_BUF;

    printf("\n  Capturing %d ticks @ %.1f MHz...\n", count, 125.0f / current_div);
    memset(sample_buf, 0, sizeof(sample_buf));
    sampler_init(current_div);
    sampler_start();
    sleep_ms((int)(count * current_div / 125000.0f) + 10);
    sampler_stop();

    printf("  Tick  | 15       8 7        0 | Marks\n");
    printf("  ------+------------------------+------\n");
    for (int i = 0; i < count; i++) {
        uint16_t snap = sample_buf[i] & 0xFFFF;
        printf("  %5d | ", i);
        for (int b = 15; b >= 0; b--) {
            printf("%c", (snap & (1 << b)) ? '1' : '0');
            if (b == 8) printf(" ");
        }
        printf(" | %d\n", v6_popcount64(snap));
    }
    printf("\n");
}

static void cmd_monitor(void) {
    printf("\n  Live monitor (500 frames, 50ms each):\n\n");
    uint16_t prev = 0;
    for (int frame = 0; frame < 500; frame++) {
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) break;

        uint16_t curr = hal_read_pins() & 0xFFFF;
        uint16_t edges = prev ^ curr;

        printf("  ");
        for (int i = N_PINS - 1; i >= 0; i--) {
            if (curr & (1 << i))
                printf("%c", (edges & (1 << i)) ? '^' : '1');
            else
                printf("%c", (edges & (1 << i)) ? 'v' : '.');
            if (i % 4 == 0 && i > 0) printf(" ");
        }
        printf("  [%d marked]\n", v6_popcount64(curr));
        prev = curr;
        sleep_ms(50);
    }
    printf("\n");
}

static void cmd_edges(void) {
    int scan_len = 1024;
    printf("\n  Edge analysis (%d ticks)...\n", scan_len);

    memset(sample_buf, 0, sizeof(sample_buf));
    sampler_init(current_div);
    sampler_start();
    sleep_ms((int)(scan_len * current_div / 125000.0f) + 20);
    sampler_stop();

    uint32_t rising[N_PINS] = {0}, falling[N_PINS] = {0};
    for (int i = 1; i < scan_len; i++) {
        uint16_t p = sample_buf[i-1] & 0xFFFF;
        uint16_t c = sample_buf[i] & 0xFFFF;
        uint16_t rise = ~p & c;
        uint16_t fall = p & ~c;
        for (int pin = 0; pin < N_PINS; pin++) {
            if (rise & (1 << pin)) rising[pin]++;
            if (fall & (1 << pin)) falling[pin]++;
        }
    }

    printf("\n  Pin  | Rising | Falling | Total\n");
    printf("  -----+--------+---------+------\n");
    for (int p = 0; p < N_PINS; p++) {
        if (rising[p] || falling[p])
            printf("  GP%-2d | %6d | %7d | %d\n",
                   p, rising[p], falling[p], rising[p] + falling[p]);
    }
    printf("\n");
}

// ═════════════════════════════════════════════════════════════
// v6 Engine Commands
// ═════════════════════════════════════════════════════════════

static void cmd_grid(void) {
    printf("\n  === v6 Grid ===\n\n");

    // GPIO [0-15]
    printf("  GPIO  [0-15]:  ");
    int gpio_marks = 0;
    for (int i = N_PINS - 1; i >= 0; i--) {
        printf("%d", grid.marked[i]);
        if (grid.marked[i]) gpio_marks++;
        if (i % 4 == 0 && i > 0) printf(" ");
    }
    printf("  (%d marked)\n", gpio_marks);

    // Data [16-31]
    printf("  Data [16-31]:  ");
    int data_marks = 0;
    for (int i = 31; i >= 16; i--) {
        printf("%d", grid.marked[i]);
        if (grid.marked[i]) data_marks++;
        if ((i - 16) % 4 == 0 && i > 16) printf(" ");
    }
    printf("  (%d)\n", data_marks);

    // Program [32-47]
    printf("  Prog [32-47]:  ");
    for (int i = 47; i >= 32; i--) {
        printf("%d", grid.marked[i]);
        if ((i - 32) % 4 == 0 && i > 32) printf(" ");
    }
    uint64_t prog = v6_word_read(&grid, PROG_BASE, PROG_WIDTH);
    printf("  -> ");
    print_set(prog, PROG_WIDTH);
    printf("\n");

    // Observer [48-49]
    uint64_t obs = v6_word_read(&grid, OBS_BASE, 2);
    const char *obs_names[] = {"ANY(OR)", "ALL(AND)", "PAR(XOR)", "CNT"};
    printf("  Obs  [48-49]:  %d%d = %s\n",
           grid.marked[49], grid.marked[48], obs_names[(int)obs & 3]);

    // Output [50]
    printf("  Out  [50]:     %d", grid.marked[OUT_POS]);
    if (grid.active[OUT_POS]) {
        printf("  cp=");
        print_set(grid.co_present[OUT_POS], V6_MAX_POS);
    }
    printf("\n");

    // Scratch [51-63]
    printf("  Scratch[51-63]:");
    int scratch_marks = 0;
    for (int i = 51; i < V6_MAX_POS; i++) {
        if (grid.marked[i]) scratch_marks++;
    }
    printf(" %d marked\n", scratch_marks);

    // Wired positions
    printf("\n  Wired:\n");
    int any = 0;
    for (int p = 0; p < grid.n_pos; p++) {
        if (!grid.active[p]) continue;
        any = 1;
        printf("    pos %d reads ", p);
        print_set(grid.reads[p], V6_MAX_POS);
        if (grid.co_present[p])  {
            printf(" cp=");
            print_set(grid.co_present[p], V6_MAX_POS);
        }
        printf("\n");
    }
    if (!any) printf("    (none)\n");
    printf("\n");
}

static void cmd_mark(int pos, int val) {
    if (pos < 0 || pos >= V6_MAX_POS) {
        printf("  Error: pos must be 0-%d\n\n", V6_MAX_POS - 1);
        return;
    }
    v6_mark(&grid, pos, val);
    printf("  pos %d = %d\n\n", pos, val ? 1 : 0);
}

static void cmd_wire_pos(int pos, uint64_t mask) {
    if (pos < 0 || pos >= V6_MAX_POS) {
        printf("  Error: pos must be 0-%d\n\n", V6_MAX_POS - 1);
        return;
    }
    v6_wire(&grid, pos, mask);
    printf("  pos %d wired to read ", pos);
    print_set(mask, V6_MAX_POS);
    printf("\n\n");
}

static void cmd_tick_exec(void) {
    v6_tick(&grid);
    printf("  Tick complete.\n");
    int any = 0;
    for (int p = 0; p < grid.n_pos; p++) {
        if (!grid.active[p]) continue;
        any = 1;
        printf("    pos %d: cp=", p);
        print_set(grid.co_present[p], V6_MAX_POS);
        printf(" (%d present)\n", v6_obs_count(grid.co_present[p]));
    }
    if (!any) printf("    No wired positions.\n");
    printf("\n");
}

static void cmd_obs(int pos, const char *type_str) {
    if (pos < 0 || pos >= V6_MAX_POS || !grid.active[pos]) {
        printf("  Error: pos %d not wired\n\n", pos);
        return;
    }
    int obs_type = V6_OBS_ANY;
    if (strcmp(type_str, "all") == 0)      obs_type = V6_OBS_ALL;
    else if (strcmp(type_str, "par") == 0)  obs_type = V6_OBS_PARITY;
    else if (strcmp(type_str, "cnt") == 0)  obs_type = V6_OBS_COUNT;

    const char *names[] = {"ANY(OR)", "ALL(AND)", "PAR(XOR)", "CNT"};
    uint64_t cp = grid.co_present[pos];
    uint64_t rd = grid.reads[pos];
    int result = v6_observe(cp, rd, obs_type);

    printf("  Observe pos %d (%s):\n", pos, names[obs_type]);
    printf("    reads:  ");
    print_set(rd, V6_MAX_POS);
    printf("\n    cp:     ");
    print_set(cp, V6_MAX_POS);
    printf("\n    result: %d\n\n", result);
}

static void cmd_sample(void) {
    bridge_sample();
    uint32_t pins = hal_read_pins();
    printf("  GPIO -> grid [0-15]: 0x%04X  (%d marked)\n\n",
           pins, v6_popcount64(pins));
}

static void cmd_output(uint32_t mask) {
    bridge_output(mask);
    uint32_t value = 0;
    for (int i = 0; i < N_PINS; i++)
        if (grid.marked[GPIO_BASE + i]) value |= (1u << i);
    printf("  Grid [0-15] -> GPIO (mask=0x%04X): 0x%04X\n\n", mask, value);
}

static void cmd_word(int base, uint64_t val) {
    if (base < 0 || base >= V6_MAX_POS) {
        printf("  Error: base out of range\n\n");
        return;
    }
    int width = PROG_WIDTH;
    if (base + width > V6_MAX_POS) width = V6_MAX_POS - base;
    v6_word_write(&grid, base, width, val);
    printf("  Word at [%d]: 0x%04llX = ", base, (unsigned long long)val);
    print_set(val, width);
    printf("\n\n");
}

static void cmd_read_word(int base, int width) {
    if (base < 0 || base >= V6_MAX_POS) {
        printf("  Error: base out of range\n\n");
        return;
    }
    if (width <= 0) width = 8;
    if (base + width > V6_MAX_POS) width = V6_MAX_POS - base;
    uint64_t val = v6_word_read(&grid, base, width);
    printf("  Word at [%d..%d]: 0x%04llX = ", base, base + width - 1,
           (unsigned long long)val);
    print_set(val, width);
    printf("\n\n");
}

static void cmd_exec(int data_base) {
    if (data_base < 0 || data_base >= V6_MAX_POS) {
        printf("  Error: data_base out of range\n\n");
        return;
    }

    // Show what's about to happen
    uint64_t prog = v6_word_read(&grid, PROG_BASE, PROG_WIDTH);
    uint64_t obs_word = v6_word_read(&grid, OBS_BASE, 2);
    const char *obs_names[] = {"ANY(OR)", "ALL(AND)", "PAR(XOR)", "CNT"};

    printf("  Executing OS instruction:\n");
    printf("    Program: ");
    print_set(prog, PROG_WIDTH);
    printf(" (read from data_base %d)\n", data_base);
    printf("    Observer: %s\n", obs_names[(int)obs_word & 3]);

    // Execute
    int result = v6_os_exec(&grid, PROG_BASE, PROG_WIDTH,
                             data_base, OUT_POS, OBS_BASE);

    printf("    Wiring: pos %d reads ", OUT_POS);
    print_set(grid.reads[OUT_POS], V6_MAX_POS);
    printf("\n    Co-present: ");
    print_set(grid.co_present[OUT_POS], V6_MAX_POS);
    printf("\n    Result: %d\n\n", result);
}

static void cmd_reset(void) {
    v6_init(&grid);
    printf("  Grid cleared.\n\n");
}

static void cmd_auto(int ms) {
    if (ms <= 0) {
        printf("  Auto mode stopped.\n\n");
        return;
    }

    uint64_t obs_word = v6_word_read(&grid, OBS_BASE, 2);
    const char *obs_names[] = {"ANY", "ALL", "PAR", "CNT"};
    printf("\n  Auto: sample+tick every %d ms (obs=%s, any key stops)\n\n",
           ms, obs_names[(int)obs_word & 3]);

    uint32_t tick_num = 0;
    while (1) {
        int c = getchar_timeout_us(0);
        if (c != PICO_ERROR_TIMEOUT) break;

        bridge_sample();

        // If output position is wired, tick and observe
        if (grid.active[OUT_POS]) {
            v6_tick(&grid);
            obs_word = v6_word_read(&grid, OBS_BASE, 2);
            int result = v6_observe(grid.co_present[OUT_POS],
                                     grid.reads[OUT_POS], (int)obs_word);
            uint32_t pins = hal_read_pins();
            printf("  t=%d pins=0x%04X cp=%d obs=%d\n",
                   tick_num, pins, v6_obs_count(grid.co_present[OUT_POS]), result);
        } else {
            uint32_t pins = hal_read_pins();
            int marks = 0;
            for (int i = 0; i < N_PINS; i++)
                if (grid.marked[i]) marks++;
            printf("  t=%d pins=0x%04X marks=%d\n", tick_num, pins, marks);
        }

        tick_num++;
        sleep_ms(ms);
    }
    printf("\n  Stopped after %d ticks.\n\n", tick_num);
}

// ═════════════════════════════════════════════════════════════
// REPL — command parser
// ═════════════════════════════════════════════════════════════

static int read_line(char *buf, int max) {
    int i = 0;
    while (i < max - 1) {
        int c = getchar_timeout_us(100000);
        if (c == PICO_ERROR_TIMEOUT) continue;
        if (c == '\r' || c == '\n') break;
        if (c == 127 || c == 8) {
            if (i > 0) { i--; printf("\b \b"); }
            continue;
        }
        buf[i++] = c;
        putchar(c);
    }
    buf[i] = '\0';
    printf("\n");
    return i;
}

// ═════════════════════════════════════════════════════════════
// Main
// ═════════════════════════════════════════════════════════════

int main(void) {
    stdio_init_all();
    sleep_ms(2000);

    v6_init(&grid);
    hal_init_pins();

    printf("\n");
    printf("  +-------------------------------------+\n");
    printf("  |  XYZT v6 Edge Node                  |\n");
    printf("  |  Pico RP2040 . 64-pos grid . 16 GPIO|\n");
    printf("  +-------------------------------------+\n");
    printf("\n");
    printf("  Type 'help' for commands\n\n");

    char cmd[64];

    while (1) {
        printf("v6> ");
        int len = read_line(cmd, sizeof(cmd));
        if (len == 0) continue;

        // Hardware commands
        if (strcmp(cmd, "help") == 0) {
            cmd_help();
        } else if (strcmp(cmd, "id") == 0) {
            cmd_id();
        } else if (strcmp(cmd, "pins") == 0) {
            cmd_pins();
        } else if (strcmp(cmd, "scan") == 0) {
            cmd_scan();
        } else if (strcmp(cmd, "monitor") == 0) {
            cmd_monitor();
        } else if (strcmp(cmd, "edges") == 0) {
            cmd_edges();
        } else if (strncmp(cmd, "raw", 3) == 0) {
            int count = 64;
            if (len > 4) count = atoi(cmd + 4);
            cmd_raw(count);
        } else if (strncmp(cmd, "speed ", 6) == 0) {
            float mhz = atof(cmd + 6);
            if (mhz > 0 && mhz <= 125) {
                current_div = 125.0f / mhz;
                printf("  Sample rate: %.1f MHz (div=%.1f)\n\n", mhz, current_div);
            } else {
                printf("  Range: 0.001 - 125 MHz\n\n");
            }

        // v6 engine commands
        } else if (strcmp(cmd, "grid") == 0) {
            cmd_grid();
        } else if (strncmp(cmd, "mark ", 5) == 0) {
            char *p = cmd + 5;
            int pos = strtol(p, &p, 10);
            while (*p == ' ') p++;
            int val = strtol(p, NULL, 10);
            cmd_mark(pos, val);
        } else if (strncmp(cmd, "wire ", 5) == 0) {
            char *p = cmd + 5;
            int pos = strtol(p, &p, 10);
            while (*p == ' ') p++;
            uint64_t mask = strtoull(p, NULL, 0);
            cmd_wire_pos(pos, mask);
        } else if (strcmp(cmd, "tick") == 0) {
            cmd_tick_exec();
        } else if (strncmp(cmd, "obs ", 4) == 0) {
            char *p = cmd + 4;
            int pos = strtol(p, &p, 10);
            while (*p == ' ') p++;
            cmd_obs(pos, p);
        } else if (strcmp(cmd, "sample") == 0) {
            cmd_sample();
        } else if (strncmp(cmd, "output", 6) == 0) {
            uint32_t mask = 0xFFFF;
            if (len > 7) mask = (uint32_t)strtoul(cmd + 7, NULL, 0);
            cmd_output(mask);
        } else if (strncmp(cmd, "word ", 5) == 0) {
            char *p = cmd + 5;
            int base = strtol(p, &p, 10);
            while (*p == ' ') p++;
            uint64_t val = strtoull(p, NULL, 0);
            cmd_word(base, val);
        } else if (strncmp(cmd, "read ", 5) == 0) {
            char *p = cmd + 5;
            int base = strtol(p, &p, 10);
            int width = 8;
            while (*p == ' ') p++;
            if (*p) width = strtol(p, NULL, 10);
            cmd_read_word(base, width);
        } else if (strncmp(cmd, "exec", 4) == 0) {
            int data_base = 0;
            if (len > 5) data_base = atoi(cmd + 5);
            cmd_exec(data_base);
        } else if (strcmp(cmd, "reset") == 0) {
            cmd_reset();
        } else if (strncmp(cmd, "auto ", 5) == 0) {
            int ms = atoi(cmd + 5);
            cmd_auto(ms);
        } else {
            printf("  Unknown: '%s' (type 'help')\n\n", cmd);
        }
    }

    return 0;
}
