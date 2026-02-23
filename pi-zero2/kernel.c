/*
 * XYZT-OS v6 Kernel — Pure Co-Presence, On Metal
 *
 * Three axioms. One engine. Zero arithmetic.
 * The grid routes distinctions. The observer creates meaning.
 * AI IS the hardware.
 *
 * Isaac Oravec & Claude — February 2026
 */

#include "xyzt_os.h"
#include "hw.h"

/* =========================================================================
 * STRING UTILITIES
 * ========================================================================= */

static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return (*a == *b);
}

static int str_starts(const char *s, const char *prefix) {
    while (*prefix) { if (*s++ != *prefix++) return 0; }
    return 1;
}

static uint32_t parse_dec(const char *s) {
    uint32_t val = 0;
    while (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); s++; }
    return val;
}

static int32_t parse_int(const char *s) {
    int neg = 0;
    if (*s == '-') { neg = 1; s++; }
    int32_t val = (int32_t)parse_dec(s);
    return neg ? -val : val;
}

static uint64_t parse_hex64(const char *s) {
    uint64_t val = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    while (1) {
        char c = *s;
        if (c >= '0' && c <= '9')      val = (val << 4) | (uint64_t)(c - '0');
        else if (c >= 'a' && c <= 'f') val = (val << 4) | (uint64_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') val = (val << 4) | (uint64_t)(c - 'A' + 10);
        else break;
        s++;
    }
    return val;
}

static const char *skip_ws(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static const char *skip_word(const char *s) {
    while (*s && *s != ' ' && *s != '\t') s++;
    return s;
}

/* =========================================================================
 * THE MACHINE
 * ========================================================================= */

static V6Grid grid;
static volatile int sensing_active = 0;
static uint32_t sense_interval_us = 1000;
static uint32_t tick_count = 0;

/* =========================================================================
 * HAL — Pi Zero 2 Hardware Abstraction
 *
 * 28 GPIO pins. Pins 14/15 reserved for UART.
 * Everything else is available for the grid.
 * ========================================================================= */

static void hal_init_pins(void) {
    for (int i = 0; i < PI_GPIO_COUNT; i++) {
        if (i == 14 || i == 15) continue;  /* UART */
        gpio_set_function(i, GPIO_FUNC_INPUT);
        gpio_set_pull(i, 1);  /* pull-down */
    }
}

static uint32_t hal_read_pins(void) {
    return gpio_read_all() & 0x0FFFFFFF;  /* 28 bits */
}

static void hal_write_pins(uint32_t mask, uint32_t value) {
    mask &= 0x0FFFFFFF;
    mask &= ~((1 << 14) | (1 << 15));  /* protect UART */
    for (int i = 0; i < PI_GPIO_COUNT; i++) {
        if (!(mask & (1 << i))) continue;
        gpio_set_function(i, GPIO_FUNC_OUTPUT);
        if (value & (1 << i)) gpio_set(i); else gpio_clear(i);
    }
}

static void hal_restore_inputs(uint32_t mask) {
    mask &= ~((1 << 14) | (1 << 15));
    for (int i = 0; i < PI_GPIO_COUNT; i++) {
        if (mask & (1 << i)) gpio_set_function(i, GPIO_FUNC_INPUT);
    }
}

/* =========================================================================
 * BRIDGE — GPIO ↔ Grid
 * ========================================================================= */

static void bridge_sample(void) {
    uint32_t pins = hal_read_pins();
    for (int i = 0; i < PI_GPIO_COUNT; i++) {
        if (i == 14 || i == 15) continue;
        v6_mark(&grid, PI_GPIO_BASE + i, (pins >> i) & 1);
    }
}

static void bridge_output(uint32_t mask) {
    uint32_t val = 0;
    for (int i = 0; i < PI_GPIO_COUNT; i++) {
        if (i == 14 || i == 15) continue;
        if (grid.marked[PI_GPIO_BASE + i]) val |= (1 << i);
    }
    hal_write_pins(mask, val);
}

/* =========================================================================
 * SENSE LOOP
 * ========================================================================= */

static void sense_tick(void) {
    bridge_sample();
    v6_tick(&grid);
    tick_count++;
}

/* =========================================================================
 * V6 SELF-TEST — verify engine on boot
 * ========================================================================= */

static int v6_self_test(void) {
    int pass = 0, fail = 0;

    #define CHECK(name, expr) do { \
        if (expr) { pass++; } \
        else { fail++; uart_puts("  FAIL: " name "\r\n"); } \
    } while(0)

    /* Boolean building blocks */
    CHECK("AND(1,1)=1", v6_and(1,1) == 1);
    CHECK("AND(1,0)=0", v6_and(1,0) == 0);
    CHECK("AND(0,1)=0", v6_and(0,1) == 0);
    CHECK("AND(0,0)=0", v6_and(0,0) == 0);

    CHECK("OR(1,1)=1",  v6_or(1,1) == 1);
    CHECK("OR(1,0)=1",  v6_or(1,0) == 1);
    CHECK("OR(0,1)=1",  v6_or(0,1) == 1);
    CHECK("OR(0,0)=0",  v6_or(0,0) == 0);

    CHECK("NOT(0)=1",   v6_not(0) == 1);
    CHECK("NOT(1)=0",   v6_not(1) == 0);

    CHECK("XOR(1,0)=1", v6_xor(1,0) == 1);
    CHECK("XOR(0,1)=1", v6_xor(0,1) == 1);
    CHECK("XOR(1,1)=0", v6_xor(1,1) == 0);
    CHECK("XOR(0,0)=0", v6_xor(0,0) == 0);

    /* Grid wire + tick + observe */
    {
        V6Grid t; v6_init(&t);
        v6_mark(&t, 0, 1); v6_mark(&t, 1, 1); v6_mark(&t, 2, 0);
        v6_wire(&t, 3, V6_BIT(0)|V6_BIT(1)|V6_BIT(2));
        v6_tick(&t);
        CHECK("co_present has {0,1}", t.co_present[3] == (V6_BIT(0)|V6_BIT(1)));
        CHECK("obs_count=2", v6_obs_count(t.co_present[3]) == 2);
        CHECK("obs_any=1",   v6_obs_any(t.co_present[3]) == 1);
        CHECK("obs_all=0",   v6_obs_all(t.co_present[3], V6_BIT(0)|V6_BIT(1)|V6_BIT(2)) == 0);
        CHECK("obs_parity=0", v6_obs_parity(t.co_present[3]) == 0);
    }

    /* Word read/write */
    {
        V6Grid t; v6_init(&t);
        v6_word_write(&t, 10, 8, 0xA5);
        uint64_t w = v6_word_read(&t, 10, 8);
        CHECK("word_rw=0xA5", w == 0xA5);
    }

    /* OS exec */
    {
        V6Grid t; v6_init(&t);
        v6_word_write(&t, 0, 4, 0xF);     /* data: all 4 marked */
        v6_word_write(&t, 4, 4, 0x5);     /* program: read pos 0,2 */
        v6_word_write(&t, 8, 2, V6_OBS_COUNT); /* observer: count */
        int r = v6_os_exec(&t, 4, 4, 0, 10, 8);
        CHECK("os_exec count=2", r == 2);
    }

    #undef CHECK

    uart_puts("  Self-test: "); uart_dec(pass); uart_puts(" pass, ");
    uart_dec(fail); uart_puts(" fail\r\n");
    return (fail == 0);
}

/* =========================================================================
 * SHELL COMMANDS — Hardware
 * ========================================================================= */

static void cmd_help(void) {
    uart_puts("\r\nXYZT-OS v6 — Pure Co-Presence\r\n\r\n");
    uart_puts("  Grid:\r\n");
    uart_puts("    grid               Show active positions\r\n");
    uart_puts("    read <p>           Position detail\r\n");
    uart_puts("    mark <p> <0|1>     Set distinction\r\n");
    uart_puts("    wire <p> <hex>     Wire to sources\r\n");
    uart_puts("    unwire <p>         Remove wiring\r\n");
    uart_puts("    tick [n]           Manual tick(s)\r\n");
    uart_puts("    obs <p>            Observe co-presence\r\n");
    uart_puts("    reset              Reinit grid\r\n\r\n");
    uart_puts("  v6 Engine:\r\n");
    uart_puts("    word <base> <w>    Read word from grid\r\n");
    uart_puts("    wwrite <b> <w> <h> Write word to grid\r\n");
    uart_puts("    exec               OS exec from grid\r\n");
    uart_puts("    and <a> <b>        Boolean AND\r\n");
    uart_puts("    or <a> <b>         Boolean OR\r\n");
    uart_puts("    not <a>            Boolean NOT\r\n");
    uart_puts("    xor <a> <b>        Boolean XOR\r\n\r\n");
    uart_puts("  Hardware:\r\n");
    uart_puts("    pins               GPIO states\r\n");
    uart_puts("    gpio r <pin>       Read pin\r\n");
    uart_puts("    gpio w <pin> <v>   Write pin\r\n");
    uart_puts("    sample             GPIO -> grid\r\n");
    uart_puts("    output <mask>      Grid -> GPIO\r\n");
    uart_puts("    scan <mask> <ms>   Capture + detect\r\n");
    uart_puts("    timer              System timer\r\n\r\n");
    uart_puts("  Sensing:\r\n");
    uart_puts("    sense on|off       Auto sensing\r\n");
    uart_puts("    rate <us>          Tick interval\r\n");
    uart_puts("    auto [n]           Tick+observe loop\r\n\r\n");
    uart_puts("  Wire graph:\r\n");
    uart_puts("    wadd wlink wlearn wquery wgraph wreset\r\n\r\n");
    uart_puts("  System:\r\n");
    uart_puts("    test info status help\r\n\r\n");
}

static void cmd_info(void) {
    uart_puts("\r\n  XYZT-OS v6 — Pure Co-Presence, On Metal\r\n");
    uart_puts("  Board:  Pi Zero 2 / BCM2710A1 / Cortex-A53\r\n");
    uart_puts("  Engine: v6 co-presence (3 axioms)\r\n");
    uart_puts("  Grid:   "); uart_dec(V6_MAX_POS); uart_puts(" positions\r\n");
    uart_puts("  GPIO:   "); uart_dec(PI_GPIO_COUNT); uart_puts(" pins (14/15=UART)\r\n");
    uart_puts("  Data:   ["); uart_dec(PI_DATA_BASE); uart_puts("-");
    uart_dec(PI_DATA_BASE + PI_DATA_WIDTH - 1); uart_puts("]\r\n");
    uart_puts("  Prog:   ["); uart_dec(PI_PROG_BASE); uart_puts("-");
    uart_dec(PI_PROG_BASE + PI_PROG_WIDTH - 1); uart_puts("]\r\n");
    uart_puts("  Obs:    ["); uart_dec(PI_OBS_BASE); uart_puts("-");
    uart_dec(PI_OBS_BASE + 1); uart_puts("]\r\n");
    uart_puts("  Out:    ["); uart_dec(PI_OUT_POS); uart_puts("]\r\n");
    uart_puts("  Ticks:  "); uart_dec(tick_count); uart_puts("\r\n");
    uart_puts("  Sense:  "); uart_puts(sensing_active ? "ACTIVE" : "stopped");
    uart_puts("  Rate: "); uart_dec(sense_interval_us); uart_puts("us\r\n");
    uart_puts("  Uptime: "); uart_dec((uint32_t)(timer_get_ticks() / 1000000));
    uart_puts("s\r\n\r\n");
}

static void cmd_status(void) {
    int na = 0, nm = 0, nw = 0;
    for (int p = 0; p < grid.n_pos; p++) {
        if (grid.marked[p]) nm++;
        if (grid.active[p]) { na++; nw++; }
    }
    uart_puts("  tick="); uart_dec(tick_count);
    uart_puts("  marked="); uart_dec(nm);
    uart_puts("  wired="); uart_dec(nw);
    uart_puts("  sense="); uart_puts(sensing_active ? "ON" : "OFF");
    WireGraph *wg = wire_get();
    uart_puts("  wire="); uart_dec(wg->n_nodes); uart_puts("n/");
    uart_dec(wg->n_edges); uart_puts("e\r\n");
}

/* =========================================================================
 * SHELL COMMANDS — Grid
 * ========================================================================= */

static void cmd_grid(void) {
    int shown = 0;
    for (int p = 0; p < grid.n_pos; p++) {
        if (!grid.marked[p] && !grid.active[p] && !grid.co_present[p]) continue;
        uart_puts("  ["); uart_dec(p);
        /* zone tag */
        if (p < PI_GPIO_COUNT)                                        uart_puts("/G");
        else if (p >= PI_DATA_BASE && p < PI_DATA_BASE + PI_DATA_WIDTH) uart_puts("/D");
        else if (p >= PI_PROG_BASE && p < PI_PROG_BASE + PI_PROG_WIDTH) uart_puts("/P");
        else if (p >= PI_OBS_BASE && p <= PI_OBS_BASE + 1)             uart_puts("/O");
        else if (p == PI_OUT_POS)                                      uart_puts("/X");
        else                                                           uart_puts("/S");
        uart_puts("] m="); uart_dec(grid.marked[p]);
        if (grid.active[p]) {
            uart_puts(" rd=0x"); uart_hex64(grid.reads[p]);
            uart_puts(" cp=0x"); uart_hex64(grid.co_present[p]);
            uart_puts(" cnt="); uart_dec(v6_obs_count(grid.co_present[p]));
        }
        uart_puts("\r\n");
        shown++;
    }
    if (!shown) uart_puts("  (empty)\r\n");
}

static void cmd_read(const char *arg) {
    arg = skip_ws(arg);
    int pos = (int)parse_dec(arg);
    if (pos >= V6_MAX_POS) { uart_puts("  Bad pos\r\n"); return; }

    uart_puts("  ["); uart_dec(pos); uart_puts("]\r\n");
    uart_puts("    marked:     "); uart_dec(grid.marked[pos]); uart_puts("\r\n");
    uart_puts("    active:     "); uart_dec(grid.active[pos]); uart_puts("\r\n");
    uart_puts("    reads:      0x"); uart_hex64(grid.reads[pos]); uart_puts("\r\n");
    uart_puts("    co_present: 0x"); uart_hex64(grid.co_present[pos]); uart_puts("\r\n");

    if (grid.active[pos]) {
        uint64_t cp = grid.co_present[pos];
        uint64_t rd = grid.reads[pos];
        uart_puts("    obs_any:    "); uart_dec(v6_obs_any(cp)); uart_puts("\r\n");
        uart_puts("    obs_none:   "); uart_dec(v6_obs_none(cp)); uart_puts("\r\n");
        uart_puts("    obs_count:  "); uart_dec(v6_obs_count(cp)); uart_puts("\r\n");
        uart_puts("    obs_parity: "); uart_dec(v6_obs_parity(cp)); uart_puts("\r\n");
        uart_puts("    obs_all:    "); uart_dec(v6_obs_all(cp, rd)); uart_puts("\r\n");
    }
}

static void cmd_mark(const char *arg) {
    arg = skip_ws(arg);
    int pos = (int)parse_dec(arg);
    arg = skip_ws(skip_word(arg));
    int val = (int)parse_dec(arg);
    if (pos >= V6_MAX_POS) { uart_puts("  Bad pos\r\n"); return; }
    v6_mark(&grid, pos, val);
    uart_puts("  ["); uart_dec(pos); uart_puts("] = "); uart_dec(grid.marked[pos]);
    uart_puts("\r\n");
}

static void cmd_wire(const char *arg) {
    arg = skip_ws(arg);
    int pos = (int)parse_dec(arg);
    arg = skip_ws(skip_word(arg));
    uint64_t rd = parse_hex64(arg);
    if (pos >= V6_MAX_POS) { uart_puts("  Bad pos\r\n"); return; }
    v6_wire(&grid, pos, rd);
    uart_puts("  ["); uart_dec(pos); uart_puts("] reads=0x"); uart_hex64(rd);
    uart_puts("\r\n");
}

static void cmd_unwire(const char *arg) {
    arg = skip_ws(arg);
    int pos = (int)parse_dec(arg);
    if (pos >= V6_MAX_POS) { uart_puts("  Bad pos\r\n"); return; }
    v6_unwire(&grid, pos);
    uart_puts("  ["); uart_dec(pos); uart_puts("] unwired\r\n");
}

static void cmd_tick(const char *arg) {
    arg = skip_ws(arg);
    int n = (*arg >= '0' && *arg <= '9') ? (int)parse_dec(arg) : 1;
    if (n < 1) n = 1;
    if (n > 100000) n = 100000;
    for (int i = 0; i < n; i++) {
        v6_tick(&grid);
        tick_count++;
    }
    uart_puts("  "); uart_dec(n); uart_puts(" tick(s). Total: ");
    uart_dec(tick_count); uart_puts("\r\n");
}

static void cmd_obs(const char *arg) {
    arg = skip_ws(arg);
    int pos = (int)parse_dec(arg);
    if (pos >= V6_MAX_POS) { uart_puts("  Bad pos\r\n"); return; }
    uint64_t cp = grid.co_present[pos];
    uart_puts("  ["); uart_dec(pos); uart_puts("] co_present=0x");
    uart_hex64(cp); uart_puts("\r\n");
    uart_puts("    any="); uart_dec(v6_obs_any(cp));
    uart_puts(" count="); uart_dec(v6_obs_count(cp));
    uart_puts(" parity="); uart_dec(v6_obs_parity(cp));
    uart_puts(" members: ");
    uint64_t bits = cp;
    int first = 1;
    while (bits) {
        int b = v6_ctz64(bits);
        if (!first) uart_puts(",");
        uart_dec(b);
        bits &= bits - 1;
        first = 0;
    }
    if (first) uart_puts("(none)");
    uart_puts("\r\n");
}

static void cmd_reset(void) {
    int was = sensing_active;
    sensing_active = 0;
    v6_init(&grid);
    tick_count = 0;
    sensing_active = was;
    uart_puts("  Grid reset.\r\n");
}

/* =========================================================================
 * SHELL COMMANDS — v6 Engine
 * ========================================================================= */

static void cmd_word(const char *arg) {
    arg = skip_ws(arg);
    int base = (int)parse_dec(arg);
    arg = skip_ws(skip_word(arg));
    int width = (*arg >= '0' && *arg <= '9') ? (int)parse_dec(arg) : PI_DATA_WIDTH;
    if (base + width > V6_MAX_POS) { uart_puts("  Out of range\r\n"); return; }
    uint64_t w = v6_word_read(&grid, base, width);
    uart_puts("  word["); uart_dec(base); uart_puts(":"); uart_dec(base + width - 1);
    uart_puts("] = 0x"); uart_hex64(w);
    uart_puts(" ("); uart_dec((uint32_t)w); uart_puts(")\r\n");
    /* Show individual bits */
    uart_puts("    ");
    for (int i = width - 1; i >= 0; i--) {
        uart_putc((w & V6_BIT(i)) ? '1' : '0');
        if (i > 0 && i % 4 == 0) uart_putc('_');
    }
    uart_puts("\r\n");
}

static void cmd_wwrite(const char *arg) {
    arg = skip_ws(arg);
    int base = (int)parse_dec(arg);
    arg = skip_ws(skip_word(arg));
    int width = (int)parse_dec(arg);
    arg = skip_ws(skip_word(arg));
    uint64_t val = parse_hex64(arg);
    if (base + width > V6_MAX_POS) { uart_puts("  Out of range\r\n"); return; }
    v6_word_write(&grid, base, width, val);
    uart_puts("  Wrote 0x"); uart_hex64(val); uart_puts(" to [");
    uart_dec(base); uart_puts(":"); uart_dec(base + width - 1); uart_puts("]\r\n");
}

static void cmd_exec(void) {
    int r = v6_os_exec(&grid, PI_PROG_BASE, PI_PROG_WIDTH,
                        PI_DATA_BASE, PI_OUT_POS, PI_OBS_BASE);
    uart_puts("  exec result: "); uart_dec(r); uart_puts("\r\n");
    uart_puts("    prog=0x"); uart_hex64(v6_word_read(&grid, PI_PROG_BASE, PI_PROG_WIDTH));
    uart_puts("  data=0x"); uart_hex64(v6_word_read(&grid, PI_DATA_BASE, PI_DATA_WIDTH));
    uart_puts("\r\n    obs="); uart_dec((uint32_t)v6_word_read(&grid, PI_OBS_BASE, 2));
    uart_puts("  out_cp=0x"); uart_hex64(grid.co_present[PI_OUT_POS]);
    uart_puts("\r\n");
}

static void cmd_bool(const char *cmd, const char *arg) {
    arg = skip_ws(arg);
    int a = (int)parse_dec(arg);

    if (str_eq(cmd, "not")) {
        int r = v6_not(a);
        uart_puts("  NOT("); uart_dec(a); uart_puts(") = "); uart_dec(r);
        uart_puts("\r\n");
        return;
    }

    arg = skip_ws(skip_word(arg));
    int b = (int)parse_dec(arg);
    int r;
    if (str_eq(cmd, "and"))      r = v6_and(a, b);
    else if (str_eq(cmd, "or"))  r = v6_or(a, b);
    else                         r = v6_xor(a, b);

    uart_puts("  "); uart_puts(cmd); uart_puts("(");
    uart_dec(a); uart_puts(","); uart_dec(b);
    uart_puts(") = "); uart_dec(r); uart_puts("\r\n");
}

/* =========================================================================
 * SHELL COMMANDS — Hardware
 * ========================================================================= */

static void cmd_pins(void) {
    uint32_t pins = hal_read_pins();
    uart_puts("  GPIO: 0x"); uart_hex(pins); uart_puts("\r\n  ");
    for (int i = 27; i >= 0; i--) {
        if (i == 14 || i == 15) { uart_putc('U'); }
        else uart_putc((pins & (1 << i)) ? '1' : '0');
        if (i % 4 == 0 && i > 0) uart_putc('_');
    }
    uart_puts("\r\n");
}

static void cmd_gpio_read(const char *arg) {
    arg = skip_ws(arg);
    uint32_t pin = parse_dec(arg);
    if (pin > 27 || pin == 14 || pin == 15) {
        uart_puts("  0-27 (not 14/15)\r\n"); return;
    }
    gpio_set_function(pin, GPIO_FUNC_INPUT);
    uart_puts("  GPIO "); uart_dec(pin); uart_puts(" = ");
    uart_dec(gpio_read(pin)); uart_puts("\r\n");
}

static void cmd_gpio_write(const char *arg) {
    arg = skip_ws(arg);
    uint32_t pin = parse_dec(arg);
    arg = skip_ws(skip_word(arg));
    uint32_t val = parse_dec(arg);
    if (pin > 27 || pin == 14 || pin == 15) {
        uart_puts("  0-27 (not 14/15)\r\n"); return;
    }
    gpio_set_function(pin, GPIO_FUNC_OUTPUT);
    if (val) gpio_set(pin); else gpio_clear(pin);
    uart_puts("  GPIO "); uart_dec(pin); uart_puts(" -> ");
    uart_dec(val); uart_puts("\r\n");
}

static void cmd_sample(void) {
    bridge_sample();
    uart_puts("  GPIO -> grid [0-27]\r\n");
    /* Show which positions got marked */
    int nm = 0;
    for (int i = 0; i < PI_GPIO_COUNT; i++) {
        if (i == 14 || i == 15) continue;
        if (grid.marked[i]) nm++;
    }
    uart_puts("  "); uart_dec(nm); uart_puts(" pins active\r\n");
}

static void cmd_output(const char *arg) {
    arg = skip_ws(arg);
    uint32_t mask = (uint32_t)parse_hex64(arg);
    if (mask == 0) { uart_puts("  Usage: output <hex_mask>\r\n"); return; }
    bridge_output(mask);
    uart_puts("  Grid -> GPIO (mask=0x"); uart_hex(mask); uart_puts(")\r\n");
    hal_restore_inputs(mask);
}

static void cmd_scan(const char *arg) {
    arg = skip_ws(arg);
    uint32_t mask = (uint32_t)parse_hex64(arg);
    arg = skip_ws(skip_word(arg));
    uint32_t ms = parse_dec(arg);
    if (ms == 0) ms = 100;
    if (mask == 0) { uart_puts("  Usage: scan <mask> <ms>\r\n"); return; }
    uart_puts("  Scanning...\r\n");
    static capture_buf_t buf;
    capture_init(&buf, mask);
    capture_for_duration(&buf, (uint64_t)ms * 1000);
    uart_puts("  "); uart_dec(buf.count); uart_puts(" edges\r\n");
    if (buf.count >= 2) {
        sense_result_t sr;
        sense_decompose(&buf, &sr);
        sense_print(&sr);
    }
}

/* =========================================================================
 * SHELL COMMANDS — Sensing
 * ========================================================================= */

static void cmd_auto(const char *arg) {
    arg = skip_ws(arg);
    int n = (*arg >= '0' && *arg <= '9') ? (int)parse_dec(arg) : 10;
    if (n < 1) n = 1;
    if (n > 10000) n = 10000;
    uart_puts("  Auto: "); uart_dec(n); uart_puts(" cycles\r\n");
    for (int i = 0; i < n; i++) {
        bridge_sample();
        v6_tick(&grid);
        tick_count++;
        /* Show co-presence at OUT_POS if wired */
        if (grid.active[PI_OUT_POS]) {
            uart_puts("  ["); uart_dec(i);
            uart_puts("] cp=0x"); uart_hex64(grid.co_present[PI_OUT_POS]);
            uart_puts(" cnt="); uart_dec(v6_obs_count(grid.co_present[PI_OUT_POS]));
            uart_puts("\r\n");
        }
        timer_wait_us(sense_interval_us);
        /* Check for keypress to abort */
        if (uart_data_ready()) {
            uart_getc();
            uart_puts("  Aborted\r\n");
            break;
        }
    }
    uart_puts("  Done. Ticks: "); uart_dec(tick_count); uart_puts("\r\n");
}

/* =========================================================================
 * SHELL COMMANDS — Wire Graph
 * ========================================================================= */

static void cmd_wadd(const char *arg) {
    arg = skip_ws(arg);
    char name[WIRE_NAME_LEN]; int i = 0;
    while (*arg && *arg != ' ' && i < WIRE_NAME_LEN - 1) name[i++] = *arg++;
    name[i] = '\0';
    arg = skip_ws(arg);
    uint8_t type = (*arg >= '0' && *arg <= '2') ? (*arg - '0') : WNODE_CONCEPT;
    int id = wire_add(name, type);
    if (id >= 0) { uart_puts("  ["); uart_dec(id); uart_puts("] "); uart_puts(name); uart_puts("\r\n"); }
    else uart_puts("  Full\r\n");
}

static void cmd_wlink(const char *arg) {
    arg = skip_ws(arg);
    char n1[WIRE_NAME_LEN], n2[WIRE_NAME_LEN]; int i;
    i = 0; while (*arg && *arg != ' ' && i < WIRE_NAME_LEN - 1) n1[i++] = *arg++;
    n1[i] = '\0'; arg = skip_ws(arg);
    i = 0; while (*arg && *arg != ' ' && i < WIRE_NAME_LEN - 1) n2[i++] = *arg++;
    n2[i] = '\0';
    int a = wire_add(n1, WNODE_CONCEPT), b = wire_add(n2, WNODE_CONCEPT);
    if (a >= 0 && b >= 0) {
        wire_connect(a, b);
        uart_puts("  "); uart_puts(n1); uart_puts(" <-> "); uart_puts(n2); uart_puts("\r\n");
    }
}

static void cmd_wlearn(const char *arg) {
    arg = skip_ws(arg);
    char name[WIRE_NAME_LEN]; int i = 0;
    while (*arg && *arg != ' ' && i < WIRE_NAME_LEN - 1) name[i++] = *arg++;
    name[i] = '\0'; arg = skip_ws(arg);
    int pass = (*arg == 'p' || *arg == '1');
    int id = wire_find(name);
    if (id < 0) { uart_puts("  Not found\r\n"); return; }
    wire_learn(id, pass);
    uart_puts("  "); uart_puts(pass ? "+" : "-"); uart_puts(" ");
    uart_puts(name); uart_puts("\r\n");
}

/* =========================================================================
 * COMMAND DISPATCH
 * ========================================================================= */

static void read_line(char *buf, uint32_t max) {
    uint32_t pos = 0;
    while (pos < max - 1) {
        char c = uart_getc();
        if (c == '\r' || c == '\n') { uart_puts("\r\n"); break; }
        if (c == 0x7F || c == '\b') {
            if (pos > 0) { pos--; uart_puts("\b \b"); }
            continue;
        }
        if (c >= 0x20 && c < 0x7F) { buf[pos++] = c; uart_putc(c); }
    }
    buf[pos] = '\0';
}

static void handle_command(char *cmd) {
    const char *c = skip_ws(cmd);

    /* System */
    if (str_eq(c, "help") || str_eq(c, "?"))        cmd_help();
    else if (str_eq(c, "info"))                       cmd_info();
    else if (str_eq(c, "status"))                     cmd_status();
    else if (str_eq(c, "test"))                       { uart_puts("  Running self-test...\r\n"); v6_self_test(); }

    /* Grid */
    else if (str_eq(c, "grid"))                       cmd_grid();
    else if (str_starts(c, "read "))                  cmd_read(c + 5);
    else if (str_starts(c, "mark "))                  cmd_mark(c + 5);
    else if (str_starts(c, "wire "))                  cmd_wire(c + 5);
    else if (str_starts(c, "unwire "))                cmd_unwire(c + 7);
    else if (str_starts(c, "tick"))                   cmd_tick(c + 4);
    else if (str_starts(c, "obs "))                   cmd_obs(c + 4);
    else if (str_eq(c, "reset"))                      cmd_reset();

    /* v6 engine */
    else if (str_starts(c, "word "))                  cmd_word(c + 5);
    else if (str_starts(c, "wwrite "))                cmd_wwrite(c + 7);
    else if (str_eq(c, "exec"))                       cmd_exec();
    else if (str_starts(c, "and "))                   cmd_bool("and", c + 4);
    else if (str_starts(c, "or "))                    cmd_bool("or", c + 3);
    else if (str_starts(c, "not "))                   cmd_bool("not", c + 4);
    else if (str_starts(c, "xor "))                   cmd_bool("xor", c + 4);

    /* Hardware */
    else if (str_eq(c, "pins"))                       cmd_pins();
    else if (str_starts(c, "gpio r "))                cmd_gpio_read(c + 7);
    else if (str_starts(c, "gpio w "))                cmd_gpio_write(c + 7);
    else if (str_eq(c, "sample"))                     cmd_sample();
    else if (str_starts(c, "output "))                cmd_output(c + 7);
    else if (str_starts(c, "scan "))                  cmd_scan(c + 5);
    else if (str_eq(c, "timer"))                      { uart_puts("  "); uart_hex64(timer_get_ticks()); uart_puts("\r\n"); }

    /* Sensing */
    else if (str_starts(c, "sense on"))               { sensing_active = 1; uart_puts("  Sensing ON\r\n"); }
    else if (str_starts(c, "sense off"))              { sensing_active = 0; uart_puts("  Sensing OFF\r\n"); }
    else if (str_starts(c, "rate "))                  { sense_interval_us = parse_dec(skip_ws(c + 5)); uart_puts("  Rate: "); uart_dec(sense_interval_us); uart_puts("us\r\n"); }
    else if (str_starts(c, "auto"))                   cmd_auto(c + 4);

    /* Wire graph */
    else if (str_starts(c, "wadd "))                  cmd_wadd(c + 5);
    else if (str_starts(c, "wlink "))                 cmd_wlink(c + 6);
    else if (str_starts(c, "wlearn "))                cmd_wlearn(c + 7);
    else if (str_starts(c, "wquery "))                wire_query(skip_ws(c + 7));
    else if (str_eq(c, "wgraph"))                     wire_print();
    else if (str_eq(c, "wreset"))                     wire_reset();

    else if (*c != '\0')                              { uart_puts("  ? "); uart_puts(c); uart_puts("\r\n"); }
}

/* =========================================================================
 * KERNEL MAIN
 * ========================================================================= */

void xyzt_kernel_main(void) {
    uart_init();
    v6_init(&grid);
    wire_init();
    hal_init_pins();

    uart_puts("\r\n\r\n");
    uart_puts("==========================================\r\n");
    uart_puts("  XYZT-OS v6\r\n");
    uart_puts("  Pure Co-Presence, On Metal\r\n");
    uart_puts("  Distinction. Position. Observation.\r\n");
    uart_puts("  Pi Zero 2 / BCM2710A1 / Cortex-A53\r\n");
    uart_puts("==========================================\r\n\r\n");

    uart_puts("[BOOT] Verifying v6 engine...\r\n");
    int ok = v6_self_test();
    if (!ok) {
        uart_puts("[BOOT] ENGINE FAILURE. Halting.\r\n");
        while (1) asm volatile("wfe");
    }
    uart_puts("[BOOT] V6 ENGINE VERIFIED.\r\n\r\n");

    /* Initial GPIO sample */
    bridge_sample();

    uart_puts("[BOOT] Grid: "); uart_dec(V6_MAX_POS);
    uart_puts(" positions, "); uart_dec(PI_GPIO_COUNT);
    uart_puts(" GPIO pins bridged\r\n");
    uart_puts("[BOOT] Type 'help' for commands.\r\n\r\n");

    char cmd[256];
    uint64_t next_tick = timer_get_ticks();

    while (1) {
        if (sensing_active) {
            uint64_t now = timer_get_ticks();
            if (now >= next_tick) {
                sense_tick();
                next_tick = now + sense_interval_us;
            }
        }

        if (uart_data_ready()) {
            int was = sensing_active;
            sensing_active = 0;
            uart_puts("v6> ");
            read_line(cmd, sizeof(cmd));
            handle_command(cmd);
            sensing_active = was;
        }
    }
}
