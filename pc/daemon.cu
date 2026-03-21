/*
 * daemon.cu — Persistent GPU Substrate + CPU Observer
 *
 * The engine runs continuously on GPU. The CPU observes and decides.
 * Hooks feed events as wave injections. The cortex loop verifies.
 * The daemon is the shared substrate between human and AI.
 *
 * Architecture:
 *   GPU thread: Yee grid ticking, Hebbian carving, sponge absorbing
 *   CPU thread: reads resonance, makes decisions, dispatches tools
 *   IPC: named pipe or file-based event queue (.xyzt/events/)
 *
 * The GPU doesn't think. It computes. It doesn't decide. It resonates.
 * The CPU doesn't compute. It observes. It doesn't resonate. It decides.
 * Together: perception + reasoning. Body + brain. {2,3}.
 *
 * Usage: xyzt_daemon [state.xyzt]
 *        Runs until killed. Loads state, starts ticking, watches for events.
 *
 * Isaac & Claude — March 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#include <io.h>
#define SLEEP_MS(ms) Sleep(ms)
#define MKDIR(d) _mkdir(d)
#define STAT_FUNC _stat
#define STAT_STRUCT struct _stat
#else
#include <unistd.h>
#define SLEEP_MS(ms) usleep((ms)*1000)
#define MKDIR(d) mkdir(d, 0755)
#define STAT_FUNC stat
#define STAT_STRUCT struct stat
#endif

extern "C" {
#include "engine.h"
#include "infer.h"
}

/* Forward declarations for CUDA functions */
extern "C" {
    int yee_init(void);
    void yee_destroy(void);
    int yee_tick(void);
    int yee_clear_fields(void);
    int yee_inject(const void *sources, int n);
    int yee_apply_sponge(int width, float rate);
    int yee_hebbian(float strengthen, float weaken);
    int yee_download_acc_raw(float *h_acc, int n);
    int yee_download_V(float *h_V, int n);
    int wire_engine_to_yee(const Engine *eng);
    int wire_yee_to_engine(Engine *eng);
}

/* ═══════════════════════════════════════════════════════
   Event system — hooks write events, daemon reads them
   ═══════════════════════════════════════════════════════ */

#define EVENT_DIR   ".xyzt/events"
#define MAX_EVENT   4096

typedef struct {
    uint32_t timestamp;
    uint8_t  type;       /* 0=file_touch, 1=test_result, 2=verify, 3=query */
    uint8_t  _pad[3];
    char     data[256];  /* filename, test command, query text, etc */
    float    amplitude;  /* injection strength */
} DaemonEvent;

/* Check for new events in the event directory */
static int poll_events(DaemonEvent *events, int max) {
    int count = 0;

#ifdef _WIN32
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*.evt", EVENT_DIR);
    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return 0;

    do {
        if (count >= max) break;
        char path[512];
        snprintf(path, sizeof(path), "%s\\%s", EVENT_DIR, fd.cFileName);

        FILE *fp = fopen(path, "rb");
        if (fp) {
            if (fread(&events[count], sizeof(DaemonEvent), 1, fp) == 1) {
                count++;
            }
            fclose(fp);
        }
        /* Consume the event by deleting the file */
        remove(path);
    } while (FindNextFileA(hFind, &fd));
    FindClose(hFind);
#else
    /* POSIX: scan EVENT_DIR for .evt files */
    DIR *dp = opendir(EVENT_DIR);
    if (!dp) return 0;
    struct dirent *de;
    while ((de = readdir(dp)) && count < max) {
        if (!strstr(de->d_name, ".evt")) continue;
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", EVENT_DIR, de->d_name);
        FILE *fp = fopen(path, "rb");
        if (fp) {
            if (fread(&events[count], sizeof(DaemonEvent), 1, fp) == 1)
                count++;
            fclose(fp);
        }
        remove(path);
    }
    closedir(dp);
#endif
    return count;
}

/* Write an event for the daemon to consume */
static void emit_event(uint8_t type, const char *data, float amplitude) {
    MKDIR(EVENT_DIR);

    DaemonEvent ev = {0};
    ev.timestamp = (uint32_t)time(NULL);
    ev.type = type;
    strncpy(ev.data, data, 255);
    ev.amplitude = amplitude;

    /* Unique filename from timestamp + random */
    char path[512];
    snprintf(path, sizeof(path), "%s/%u_%d.evt",
             EVENT_DIR, ev.timestamp, rand() % 10000);
    FILE *fp = fopen(path, "wb");
    if (fp) { fwrite(&ev, sizeof(ev), 1, fp); fclose(fp); }
}

/* ═══════════════════════════════════════════════════════
   GPU substrate — continuous wave propagation
   ═══════════════════════════════════════════════════════ */

#define TICKS_PER_CYCLE    155    /* one SUBSTRATE_INT heartbeat */
#define DREAM_NOISE_AMP    0.001f
#define SPONGE_WIDTH       4
#define SPONGE_RATE        0.15f
#define HEBBIAN_STRENGTHEN 0.008f
#define HEBBIAN_WEAKEN     0.004f

typedef struct {
    Engine eng;
    int    running;
    int    tick_count;
    int    cycle_count;
    int    events_processed;
    int    dreams;          /* cycles with no input = dreaming */
} DaemonState;

static volatile int g_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
    printf("\n[daemon] shutting down...\n");
}

/* Inject an event as a wave source at the node's coordinates */
static void inject_event(DaemonState *ds, const DaemonEvent *ev) {
    Graph *g = &ds->eng.shells[0].g;

    /* Find or create the node for this event's data */
    int nid = graph_find(g, ev->data);
    if (nid < 0) {
        /* New node — ingest it */
        BitStream bs;
        bs_init(&bs);
        encode_text(&bs, ev->data);
        nid = engine_ingest(&ds->eng, ev->data, &bs);
    }

    if (nid < 0 || nid >= g->n_nodes) return;

    /* Inject wave at node's coordinates */
    Node *n = &g->nodes[nid];
    int gx = coord_x(n->coord) % 64;
    int gy = coord_y(n->coord) % 64;
    int gz = coord_z(n->coord) % 64;

    typedef struct { int voxel_id; float amplitude; float strength; } YeeSource;
    YeeSource src;
    src.voxel_id = gx * 64 * 64 + gy * 64 + gz;
    src.amplitude = ev->amplitude;
    src.strength = ev->amplitude;
    yee_inject(&src, 1);

    n->hit_count++;
    n->last_active = (uint32_t)time(NULL);
}

/* ═══════════════════════════════════════════════════════
   CPU observer — reads GPU state, makes decisions
   ═══════════════════════════════════════════════════════ */

#define YEE_TOTAL (64*64*64)

typedef struct {
    int    node_id;
    char   name[128];
    float  energy;
    float  coherence;
    float  dream_score;
} Observation;

/* Read the GPU's current state and find what's resonating */
static int observe(DaemonState *ds, Observation *obs, int max) {
    Graph *g = &ds->eng.shells[0].g;
    float *h_acc = (float *)malloc(YEE_TOTAL * sizeof(float));
    float *h_V = (float *)malloc(YEE_TOTAL * sizeof(float));
    if (!h_acc || !h_V) { free(h_acc); free(h_V); return 0; }

    yee_download_acc_raw(h_acc, YEE_TOTAL);
    yee_download_V(h_V, YEE_TOTAL);

    int n_obs = 0;
    for (int i = 0; i < g->n_nodes && n_obs < max; i++) {
        Node *n = &g->nodes[i];
        if (!n->alive) continue;

        int gx = coord_x(n->coord) % 64;
        int gy = coord_y(n->coord) % 64;
        int gz = coord_z(n->coord) % 64;
        int vid = gx * 64 * 64 + gy * 64 + gz;

        float energy = h_acc[vid];
        float voltage = h_V[vid];
        float coherence = (energy > 0.001f) ? fabsf(voltage) / energy : 1.0f;
        if (coherence > 1.0f) coherence = 1.0f;
        float dream = energy * (1.0f - coherence);

        if (energy < 0.001f) continue;

        Observation *o = &obs[n_obs];
        o->node_id = i;
        strncpy(o->name, n->name, 127);
        o->energy = energy;
        o->coherence = coherence;
        o->dream_score = dream;
        n_obs++;
    }

    /* Sort by dream_score descending */
    for (int i = 0; i < n_obs - 1; i++)
        for (int j = i + 1; j < n_obs; j++)
            if (obs[j].dream_score > obs[i].dream_score) {
                Observation tmp = obs[i]; obs[i] = obs[j]; obs[j] = tmp;
            }

    free(h_acc);
    free(h_V);
    return n_obs;
}

/* Decide what action to take based on observation */
static void decide(DaemonState *ds, Observation *obs, int n_obs) {
    if (n_obs == 0) return;

    /* The top resonating node is what the engine "thinks about" */
    Observation *top = &obs[0];

    /* Log what the engine is focused on */
    static char last_focus[128] = {0};
    if (strcmp(last_focus, top->name) != 0) {
        printf("[daemon] focus: %s (energy=%.4f, coh=%.3f, dream=%.4f)\n",
               top->name, top->energy, top->coherence, top->dream_score);
        strncpy(last_focus, top->name, 127);
    }

    /* Action dispatch based on node type and state */
    Graph *g = &ds->eng.shells[0].g;
    Node *n = &g->nodes[top->node_id];

    /* If a file node has high dream score + low coherence = oscillating
       → the engine is "worried" about this file. Write a priority event. */
    if (top->coherence < 0.3f && top->energy > 0.1f) {
        char action[512];
        snprintf(action, sizeof(action),
            "engine:concern:%s:energy=%.4f:coh=%.3f",
            top->name, top->energy, top->coherence);

        /* Write to action queue for xyzt_loop or human to pick up */
        FILE *fp = fopen(".xyzt/daemon_actions.txt", "a");
        if (fp) {
            fprintf(fp, "[%u] %s\n", (uint32_t)time(NULL), action);
            fclose(fp);
        }
    }
}

/* ═══════════════════════════════════════════════════════
   Main loop — GPU ticks, CPU observes
   ═══════════════════════════════════════════════════════ */

int main(int argc, char **argv) {
    /* Unbuffered stdout — every printf shows immediately */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    const char *state_path = argc >= 2 ? argv[1] : "state.xyzt";

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    fprintf(stderr, "[daemon] starting...\n");

    printf("═══════════════════════════════════════════\n");
    printf("  XYZT DAEMON — GPU Substrate + CPU Observer\n");
    printf("  Body (GPU): Yee grid, Hebbian, wave physics\n");
    printf("  Brain (CPU): observe, decide, dispatch\n");
    printf("═══════════════════════════════════════════\n\n");

    DaemonState ds = {0};
    fprintf(stderr, "[daemon] engine_init...\n");
    engine_init(&ds.eng);

    /* Initialize GPU */
    fprintf(stderr, "[daemon] yee_init...\n");
    if (yee_init() != 0) {
        fprintf(stderr, "[daemon] FATAL: Yee GPU init failed\n");
        engine_destroy(&ds.eng);
        return 1;
    }
    fprintf(stderr, "[daemon] GPU ready\n");

    /* Load saved state if exists */
    STAT_STRUCT st;
    if (STAT_FUNC(state_path, &st) == 0) {
        if (engine_load(&ds.eng, state_path) == 0) {
            printf("[daemon] loaded state: %s (%d nodes)\n",
                   state_path, ds.eng.shells[0].g.n_nodes);
        }
    } else {
        printf("[daemon] no saved state, starting fresh\n");
    }

    /* Wire engine state to GPU */
    wire_engine_to_yee(&ds.eng);

    /* Create event directory */
    MKDIR(EVENT_DIR);

    printf("[daemon] running. Ctrl+C to stop.\n\n");
    ds.running = 1;

    /* === MAIN LOOP === */
    while (g_running) {
        /* GPU: tick the substrate */
        for (int t = 0; t < TICKS_PER_CYCLE; t++) {
            /* Check for events mid-cycle */
            if (t % 16 == 0) {  /* check every 16 ticks (one natural period) */
                DaemonEvent events[8];
                int n_events = poll_events(events, 8);
                for (int e = 0; e < n_events; e++) {
                    inject_event(&ds, &events[e]);
                    ds.events_processed++;
                    ds.dreams = 0;  /* input received, not dreaming */
                }
            }

            /* If no events for a full cycle, inject dream noise */
            if (ds.dreams > 0 && t % 32 == 0) {
                typedef struct { int voxel_id; float amplitude; float strength; } YeeSource;
                YeeSource noise[4];
                for (int i = 0; i < 4; i++) {
                    noise[i].voxel_id = rand() % YEE_TOTAL;
                    noise[i].amplitude = DREAM_NOISE_AMP * ((rand() % 2000 - 1000) / 1000.0f);
                    noise[i].strength = DREAM_NOISE_AMP;
                }
                yee_inject(noise, 4);
            }

            yee_tick();
            yee_apply_sponge(SPONGE_WIDTH, SPONGE_RATE);
            ds.tick_count++;
        }

        /* GPU: Hebbian learning at end of cycle */
        yee_hebbian(HEBBIAN_STRENGTHEN, HEBBIAN_WEAKEN);

        /* CPU: observe what resonated */
        Observation obs[16];
        int n_obs = observe(&ds, obs, 16);

        /* CPU: decide and act */
        decide(&ds, obs, n_obs);

        ds.cycle_count++;

        /* Track dreaming: if no events this cycle, increment dream counter */
        ds.dreams++;

        /* Periodic status */
        if (ds.cycle_count % 10 == 0) {
            printf("[daemon] cycle=%d ticks=%d events=%d dreams=%d focus=%s\n",
                   ds.cycle_count, ds.tick_count, ds.events_processed,
                   ds.dreams, n_obs > 0 ? obs[0].name : "(silence)");
        }

        /* Auto-save every 100 cycles */
        if (ds.cycle_count % 100 == 0) {
            wire_yee_to_engine(&ds.eng);
            engine_save(&ds.eng, state_path);
            printf("[daemon] auto-saved to %s\n", state_path);
        }

        /* Brief pause between cycles to not monopolize GPU */
        SLEEP_MS(10);
    }

    /* Shutdown: save state */
    printf("\n[daemon] saving final state...\n");
    wire_yee_to_engine(&ds.eng);
    engine_save(&ds.eng, state_path);
    printf("[daemon] saved %d nodes to %s\n",
           ds.eng.shells[0].g.n_nodes, state_path);
    printf("[daemon] lifetime: %d cycles, %d ticks, %d events\n",
           ds.cycle_count, ds.tick_count, ds.events_processed);

    yee_destroy();
    engine_destroy(&ds.eng);
    return 0;
}
