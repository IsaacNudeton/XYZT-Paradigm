/*
 * xyzt_hw.c — Hardware Interface Tool (v6 Engine)
 *
 * The physical world IS a grid.
 * GPIO pins ARE positions. Voltage IS distinction.
 * Reading pins IS tick(). Interpreting signals IS observation.
 *
 * This tool bridges the XYZT v6 engine to real hardware:
 *   - Auto-detects boards on COM/USB ports
 *   - Connects to Pico/Arduino/FTDI over serial
 *   - Reads pin states as co-presence snapshots
 *   - Applies v6 observers for protocol detection
 *   - Sends marks (signals) to hardware
 *
 * v6 mapping:
 *   Position  = GPIO pin number
 *   Mark      = pin HIGH (voltage present)
 *   Topology  = physical wiring between devices
 *   tick()    = one sample cycle (PIO on Pico, digitalRead on Arduino)
 *   Observer  = protocol decoder / signal analyzer
 *
 * Commands:
 *   xyzt_hw scan                    Find connected hardware
 *   xyzt_hw connect [port]          Connect to a device
 *   xyzt_hw pins                    Read pin co-presence snapshot
 *   xyzt_hw sniff [ticks]           Capture signal history
 *   xyzt_hw monitor                 Live pin display
 *   xyzt_hw send <pin> <0|1>        Set a pin state
 *   xyzt_hw detect                  Auto-detect protocol
 *   xyzt_hw edges                   Transition analysis
 *   xyzt_hw raw [count]             Raw bitstream dump
 *   xyzt_hw baud <pin>              Estimate baud rate on a pin
 *   xyzt_hw decode <proto> [pin]    Decode protocol stream
 *   xyzt_hw boards                  List known board profiles
 *   xyzt_hw help                    Show commands
 *
 * Compile: gcc -O3 -o xyzt_hw xyzt_hw.c
 *
 * Isaac Oravec & Claude — February 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#endif

/* ══════════════════════════════════════════════════════════════
 * V6 ENGINE — zero arithmetic, pure co-presence
 * ══════════════════════════════════════════════════════════════ */

#define MAX_POS  16     /* one tile = 16 GPIO pins */
#define BIT(n)   (1ULL << (n))

typedef struct {
    uint8_t  marked[MAX_POS];
    uint64_t reads[MAX_POS];
    uint64_t co_present[MAX_POS];
    uint8_t  active[MAX_POS];
    int      n_pos;
} Grid;

static void grid_init(Grid *g) { memset(g, 0, sizeof(*g)); g->n_pos = MAX_POS; }

static void mark(Grid *g, int pos, int val) {
    if (pos >= 0 && pos < MAX_POS) g->marked[pos] = val ? 1 : 0;
}

static void wire(Grid *g, int pos, uint64_t rd) {
    if (pos >= 0 && pos < MAX_POS) {
        g->reads[pos] = rd;
        g->active[pos] = 1;
    }
}

/* THE tick. Route distinctions through topology. Record co-presence. */
static void tick(Grid *g) {
    uint8_t snap[MAX_POS];
    memcpy(snap, g->marked, MAX_POS);
    for (int p = 0; p < g->n_pos; p++) {
        if (!g->active[p]) continue;
        uint64_t present = 0;
        uint64_t bits = g->reads[p];
        while (bits) {
            int b = __builtin_ctzll(bits);
            if (b < MAX_POS && snap[b]) present |= BIT(b);
            bits &= bits - 1;
        }
        g->co_present[p] = present;
    }
}

/* ══════════════════════════════════════════════════════════════
 * OBSERVERS — all meaning lives here
 * ══════════════════════════════════════════════════════════════ */

static int obs_any(uint64_t cp)              { return cp != 0; }
static int obs_all(uint64_t cp, uint64_t rd) { return (cp & rd) == rd; }
static int obs_none(uint64_t cp)             { return cp == 0; }
static int obs_parity(uint64_t cp)           { return __builtin_popcountll(cp) & 1; }
static int obs_count(uint64_t cp)            { return __builtin_popcountll(cp); }
static int obs_ge(uint64_t cp, int n)        { return __builtin_popcountll(cp) >= n; }
static int obs_exactly(uint64_t cp, int n)   { return __builtin_popcountll(cp) == n; }

/* Edge observer: what changed between two co-presence snapshots */
static uint16_t obs_edge(uint16_t prev, uint16_t curr)    { return prev ^ curr; }
static uint16_t obs_rising(uint16_t prev, uint16_t curr)  { return ~prev & curr; }
static uint16_t obs_falling(uint16_t prev, uint16_t curr) { return prev & ~curr; }

/* ══════════════════════════════════════════════════════════════
 * SERIAL PORT — Win32 COM interface
 * ══════════════════════════════════════════════════════════════ */

#define MAX_PORTS    32
#define CMD_BUF      256
#define RESP_BUF     8192
#define BAUD_RATE    115200

typedef struct {
    char port[16];          /* "COM6" */
    char board[32];         /* "Pico", "Arduino", "FTDI" */
    uint16_t vid;
    uint16_t pid;
} HWDevice;

#ifdef _WIN32
static HANDLE serial_handle = INVALID_HANDLE_VALUE;
#else
static int serial_fd = -1;
#endif

static char connected_port[16] = {0};
static int  is_connected = 0;

/* Scan COM ports for known hardware */
static int scan_ports(HWDevice *devs, int max_devs) {
    int found = 0;
#ifdef _WIN32
    for (int i = 1; i <= 256 && found < max_devs; i++) {
        char port[16];
        snprintf(port, sizeof(port), "COM%d", i);
        char path[32];
        snprintf(path, sizeof(path), "\\\\.\\%s", port);

        HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
            0, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            CloseHandle(h);
            strncpy(devs[found].port, port, 15);

            /* Try to identify by querying registry for VID/PID */
            char query[128];
            snprintf(query, sizeof(query),
                "powershell -NoProfile -Command \"Get-PnpDevice -Class Ports -Status OK | "
                "Where-Object { $_.FriendlyName -match '%s' } | "
                "Select-Object -ExpandProperty InstanceId\" 2>NUL", port);
            FILE *fp = _popen(query, "r");
            if (fp) {
                char line[256] = {0};
                if (fgets(line, sizeof(line), fp)) {
                    /* Parse VID/PID from instance ID */
                    char *vid = strstr(line, "VID_");
                    char *pid = strstr(line, "PID_");
                    if (vid) devs[found].vid = (uint16_t)strtol(vid + 4, NULL, 16);
                    if (pid) devs[found].pid = (uint16_t)strtol(pid + 4, NULL, 16);

                    /* Identify board by VID/PID */
                    if (devs[found].vid == 0x2E8A) {
                        strcpy(devs[found].board, "Raspberry Pi Pico");
                    } else if (devs[found].vid == 0x2341) {
                        strcpy(devs[found].board, "Arduino");
                    } else if (devs[found].vid == 0x0403) {
                        strcpy(devs[found].board, "FTDI");
                    } else if (devs[found].vid == 0x10C4) {
                        strcpy(devs[found].board, "CP2102 (Silicon Labs)");
                    } else if (devs[found].vid == 0x1A86) {
                        strcpy(devs[found].board, "CH340");
                    } else if (devs[found].vid == 0x0403 && devs[found].pid == 0x6014) {
                        strcpy(devs[found].board, "FTDI FT232H");
                    } else {
                        snprintf(devs[found].board, 31, "Unknown (%04X:%04X)",
                            devs[found].vid, devs[found].pid);
                    }
                } else {
                    strcpy(devs[found].board, "Unknown");
                }
                _pclose(fp);
            }
            found++;
        }
    }
#endif
    return found;
}

/* Open serial connection */
static int serial_open(const char *port) {
#ifdef _WIN32
    char path[32];
    snprintf(path, sizeof(path), "\\\\.\\%s", port);

    serial_handle = CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    if (serial_handle == INVALID_HANDLE_VALUE) return -1;

    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);
    GetCommState(serial_handle, &dcb);
    dcb.BaudRate = BAUD_RATE;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity   = NOPARITY;
    SetCommState(serial_handle, &dcb);

    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout         = 50;
    timeouts.ReadTotalTimeoutConstant    = 500;
    timeouts.ReadTotalTimeoutMultiplier  = 10;
    timeouts.WriteTotalTimeoutConstant   = 500;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(serial_handle, &timeouts);

    /* Flush */
    PurgeComm(serial_handle, PURGE_RXCLEAR | PURGE_TXCLEAR);

    strncpy(connected_port, port, 15);
    is_connected = 1;
    return 0;
#else
    return -1;
#endif
}

static void serial_close(void) {
#ifdef _WIN32
    if (serial_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(serial_handle);
        serial_handle = INVALID_HANDLE_VALUE;
    }
#endif
    is_connected = 0;
    connected_port[0] = '\0';
}

/* Send command to Pico, read response */
static int serial_cmd(const char *cmd, char *resp, int resp_size) {
#ifdef _WIN32
    if (serial_handle == INVALID_HANDLE_VALUE) return -1;

    /* Send command + newline */
    DWORD written;
    WriteFile(serial_handle, cmd, (DWORD)strlen(cmd), &written, NULL);
    WriteFile(serial_handle, "\r", 1, &written, NULL);

    /* Wait for response */
    Sleep(100);

    /* Read response */
    DWORD total = 0;
    int attempts = 0;
    while (total < (DWORD)(resp_size - 1) && attempts < 20) {
        DWORD bytesRead = 0;
        ReadFile(serial_handle, resp + total, resp_size - 1 - total, &bytesRead, NULL);
        if (bytesRead == 0) {
            attempts++;
            if (attempts > 3 && total > 0) break;
            Sleep(50);
            continue;
        }
        total += bytesRead;
        attempts = 0;
    }
    resp[total] = '\0';
    return (int)total;
#else
    return -1;
#endif
}

/* Read raw bytes from serial (for sniffing) */
static int serial_read_raw(uint8_t *buf, int max_bytes, int timeout_ms) {
#ifdef _WIN32
    if (serial_handle == INVALID_HANDLE_VALUE) return -1;

    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 10;
    timeouts.ReadTotalTimeoutConstant = timeout_ms;
    timeouts.ReadTotalTimeoutMultiplier = 1;
    SetCommTimeouts(serial_handle, &timeouts);

    DWORD bytesRead = 0;
    ReadFile(serial_handle, buf, max_bytes, &bytesRead, NULL);
    return (int)bytesRead;
#else
    return -1;
#endif
}

/* ══════════════════════════════════════════════════════════════
 * V6 PROTOCOL DETECTION — observers over time
 *
 * Sample pin states over many ticks.
 * Apply different observers to the same co-presence history.
 * The observer that finds structure = the protocol.
 * ══════════════════════════════════════════════════════════════ */

typedef enum {
    PROTO_UNKNOWN = 0,
    PROTO_UART,
    PROTO_SPI,
    PROTO_I2C,
    PROTO_PWM,
    PROTO_RAW
} Protocol;

static const char *proto_names[] = {
    "UNKNOWN", "UART", "SPI", "I2C", "PWM", "RAW"
};

/* Analyze transition patterns to detect protocol */
static Protocol detect_from_history(uint16_t *history, int len) {
    if (len < 100) return PROTO_RAW;

    int transitions[MAX_POS] = {0};
    int high_count[MAX_POS] = {0};

    for (int i = 0; i < len; i++) {
        for (int p = 0; p < MAX_POS; p++) {
            if (history[i] & (1 << p)) high_count[p]++;
        }
        if (i > 0) {
            uint16_t edges = obs_edge(history[i-1], history[i]);
            for (int p = 0; p < MAX_POS; p++) {
                if (edges & (1 << p)) transitions[p]++;
            }
        }
    }

    /* Find most active pins */
    int max_t = 0, max_p = -1;
    int sec_t = 0, sec_p = -1;
    for (int p = 0; p < MAX_POS; p++) {
        if (transitions[p] > max_t) {
            sec_t = max_t; sec_p = max_p;
            max_t = transitions[p]; max_p = p;
        } else if (transitions[p] > sec_t) {
            sec_t = transitions[p]; sec_p = p;
        }
    }

    if (max_p < 0 || max_t < 10) return PROTO_RAW;

    float duty = (float)high_count[max_p] / len;
    int is_clock = (duty > 0.35f && duty < 0.65f) && (max_t > len / 4);

    /* PWM: regular toggling with consistent duty cycle */
    if (is_clock && sec_t < max_t / 10) return PROTO_PWM;

    /* SPI: clock + data + optional CS */
    if (is_clock && sec_p >= 0 && sec_t > 10) {
        for (int p = 0; p < MAX_POS; p++) {
            if (p == max_p || p == sec_p) continue;
            int low = 0;
            for (int i = 0; i < len; i++)
                if (!(history[i] & (1 << p))) low++;
            if ((float)low / len > 0.3f && (float)low / len < 0.8f)
                return PROTO_SPI;
        }
        return PROTO_SPI;
    }

    /* I2C: two wires, both mostly high (pull-ups), open drain */
    if (max_p >= 0 && sec_p >= 0 && !is_clock) {
        float d1 = (float)high_count[max_p] / len;
        float d2 = (float)high_count[sec_p] / len;
        if (d1 > 0.6f && d2 > 0.6f) return PROTO_I2C;
    }

    /* UART: single data line, no clock, async */
    if (!is_clock && max_t > 10 && (sec_t == 0 || sec_t < max_t / 5))
        return PROTO_UART;

    return PROTO_UNKNOWN;
}

/* ══════════════════════════════════════════════════════════════
 * BOARD PROFILES
 * ══════════════════════════════════════════════════════════════ */

typedef struct {
    char name[32];
    int  n_gpio;
    int  has_adc;
    int  has_pio;
    int  has_spi;
    int  has_i2c;
    int  has_uart;
    int  clock_mhz;
    char mcu[32];
} BoardProfile;

static const BoardProfile known_boards[] = {
    {"Raspberry Pi Pico",  26, 3, 2, 2, 2, 2, 133, "RP2040 (Cortex-M0+)"},
    {"Arduino Uno",        14, 6, 0, 1, 1, 1,  16, "ATmega328P"},
    {"Arduino Mega",       54, 16,0, 1, 1, 4,  16, "ATmega2560"},
    {"Arduino Nano",       14, 8, 0, 1, 1, 1,  16, "ATmega328P"},
    {"FTDI FT232H",        16, 0, 0, 1, 1, 1,  60, "FT232H (MPSSE)"},
    {"Basys 3",            40, 0, 0, 0, 0, 1, 100, "Artix-7 XC7A35T (FPGA)"},
    {"Zynq-7020",          54, 0, 0, 2, 2, 2, 866, "Dual Cortex-A9 + Artix-7 PL"},
    {"Pi Zero 2",          26, 0, 0, 2, 2, 2,1000, "BCM2710A1 (Cortex-A53)"},
};
#define N_BOARDS (sizeof(known_boards)/sizeof(known_boards[0]))

/* ══════════════════════════════════════════════════════════════
 * COMMANDS
 * ══════════════════════════════════════════════════════════════ */

static void cmd_help(void) {
    printf("\n");
    printf("  xyzt_hw — Hardware Interface (v6 Engine)\n");
    printf("  ═════════════════════════════════════════\n");
    printf("\n");
    printf("  Positions = GPIO pins. Marks = voltage.\n");
    printf("  tick() = sample. Observers = protocol detection.\n");
    printf("\n");
    printf("  Connection:\n");
    printf("    scan                 Find hardware on COM/USB\n");
    printf("    connect [port]       Connect to a device (auto if one found)\n");
    printf("    disconnect           Close connection\n");
    printf("    boards               Show known board profiles\n");
    printf("\n");
    printf("  Observe:\n");
    printf("    pins                 Co-presence snapshot (all pin states)\n");
    printf("    monitor              Live pin display\n");
    printf("    sniff [ticks]        Capture signal history\n");
    printf("    raw [count]          Raw bitstream dump\n");
    printf("    edges                Transition analysis\n");
    printf("\n");
    printf("  Analyze:\n");
    printf("    detect               Auto-detect protocol from signals\n");
    printf("    baud <pin>           Estimate baud rate on a pin\n");
    printf("    decode <proto> [pin] Decode protocol stream\n");
    printf("\n");
    printf("  Act:\n");
    printf("    send <cmd>           Send raw command to device\n");
    printf("\n");
}

static void cmd_scan(void) {
    printf("\n  Scanning COM ports...\n\n");
    HWDevice devs[MAX_PORTS];
    int found = scan_ports(devs, MAX_PORTS);

    if (found == 0) {
        printf("  No hardware found.\n");
        printf("  Tip: plug in a board, or hold BOOT+plug for Pico BOOTSEL\n\n");
        return;
    }

    printf("  %-8s %-24s VID:PID\n", "Port", "Board");
    printf("  ──────── ──────────────────────── ─────────\n");
    for (int i = 0; i < found; i++) {
        printf("  %-8s %-24s %04X:%04X\n",
            devs[i].port, devs[i].board, devs[i].vid, devs[i].pid);
    }
    printf("\n  Found %d device%s.\n\n", found, found == 1 ? "" : "s");
}

static void cmd_connect(const char *port) {
    if (is_connected) {
        printf("  Already connected to %s. Disconnect first.\n\n", connected_port);
        return;
    }

    /* Auto-detect if no port specified */
    if (!port || !port[0]) {
        HWDevice devs[MAX_PORTS];
        int found = scan_ports(devs, MAX_PORTS);

        /* Skip COM1 (built-in), prefer USB serial */
        int target = -1;
        for (int i = 0; i < found; i++) {
            if (strcmp(devs[i].port, "COM1") != 0) {
                target = i;
                break;
            }
        }
        if (target < 0) {
            printf("  No USB serial device found. Specify port: xyzt_hw connect COM6\n\n");
            return;
        }
        port = devs[target].port;
        printf("  Auto-detected: %s (%s)\n", devs[target].board, port);
    }

    printf("  Connecting to %s @ %d baud...", port, BAUD_RATE);
    if (serial_open(port) == 0) {
        printf(" connected.\n\n");

        /* Read any startup banner */
        char resp[RESP_BUF];
        Sleep(1500);
        int n = serial_read_raw((uint8_t *)resp, RESP_BUF - 1, 500);
        if (n > 0) {
            resp[n] = '\0';
            printf("%s", resp);
        }
    } else {
        printf(" FAILED.\n");
        printf("  Check: is the port in use by another program?\n\n");
    }
}

static void cmd_disconnect(void) {
    if (!is_connected) {
        printf("  Not connected.\n\n");
        return;
    }
    printf("  Disconnected from %s.\n\n", connected_port);
    serial_close();
}

static void cmd_passthrough(const char *cmd) {
    if (!is_connected) {
        printf("  Not connected. Run: xyzt_hw connect\n\n");
        return;
    }

    char resp[RESP_BUF];
    int n = serial_cmd(cmd, resp, RESP_BUF);
    if (n > 0) {
        printf("%s", resp);
        if (resp[n-1] != '\n') printf("\n");
    } else {
        printf("  No response.\n\n");
    }
}

static void cmd_boards(void) {
    printf("\n  Known Board Profiles:\n\n");
    printf("  %-20s %-6s %-5s %-5s %-5s %-6s %s\n",
        "Board", "GPIO", "ADC", "PIO", "SPI", "Clock", "MCU");
    printf("  ──────────────────── ────── ───── ───── ───── ────── ──────────────────────\n");
    for (int i = 0; i < (int)N_BOARDS; i++) {
        const BoardProfile *b = &known_boards[i];
        printf("  %-20s %-6d %-5d %-5d %-5d %dMHz  %s\n",
            b->name, b->n_gpio, b->has_adc, b->has_pio,
            b->has_spi, b->clock_mhz, b->mcu);
    }
    printf("\n");
}

/* ══════════════════════════════════════════════════════════════
 * MAIN
 * ══════════════════════════════════════════════════════════════ */

int main(int argc, char *argv[]) {
    if (argc < 2) {
        cmd_help();
        return 0;
    }

    const char *cmd = argv[1];
    const char *arg = argc > 2 ? argv[2] : NULL;

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0) {
        cmd_help();
    }
    else if (strcmp(cmd, "scan") == 0) {
        cmd_scan();
    }
    else if (strcmp(cmd, "boards") == 0) {
        cmd_boards();
    }
    else if (strcmp(cmd, "connect") == 0) {
        cmd_connect(arg);

        /* Enter interactive mode after connecting */
        if (is_connected) {
            printf("  Interactive mode. Type commands (quit to exit).\n\n");
            char line[CMD_BUF];
            while (1) {
                printf("  hw> ");
                fflush(stdout);
                if (!fgets(line, CMD_BUF, stdin)) break;

                /* Strip newline */
                int len = (int)strlen(line);
                while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
                    line[--len] = '\0';

                if (len == 0) continue;
                if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) break;

                if (strcmp(line, "help") == 0) {
                    printf("\n  Device commands: pins, scan, raw, monitor, edges, speed\n");
                    printf("  Local commands:  detect, boards, quit\n\n");
                    continue;
                }
                if (strcmp(line, "boards") == 0) { cmd_boards(); continue; }

                /* Pass everything else to the Pico */
                cmd_passthrough(line);
            }
            serial_close();
            printf("  Disconnected.\n");
        }
    }
    else if (strcmp(cmd, "pins") == 0 ||
             strcmp(cmd, "monitor") == 0 ||
             strcmp(cmd, "edges") == 0 ||
             strcmp(cmd, "raw") == 0 ||
             strcmp(cmd, "sniff") == 0 ||
             strcmp(cmd, "detect") == 0) {
        /* Quick-connect mode: auto-connect, run command, disconnect */
        cmd_connect(NULL);
        if (is_connected) {
            /* Map our commands to Pico commands */
            if (strcmp(cmd, "sniff") == 0 || strcmp(cmd, "detect") == 0)
                cmd_passthrough("scan");
            else if (arg) {
                char full[CMD_BUF];
                snprintf(full, CMD_BUF, "%s %s", cmd, arg);
                cmd_passthrough(full);
            } else {
                cmd_passthrough(cmd);
            }
            serial_close();
        }
    }
    else {
        printf("  Unknown command: %s\n", cmd);
        printf("  Run: xyzt_hw help\n\n");
    }

    return 0;
}
