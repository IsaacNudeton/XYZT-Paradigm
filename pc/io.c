/*
 * io.c — Streaming input (ring buffer + stdin listener thread)
 *
 * Thread-safe circular buffer fed by a dedicated stdin reader thread.
 * Consumer reads lines (text mode) or raw chunks (binary mode).
 * Windows threading: CRITICAL_SECTION + CreateThread + ReadFile.
 *
 * Isaac Oravec & Claude, March 2026
 */

#include "io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

/* ══════════════════════════════════════════════════════════════
 * RING BUFFER OPERATIONS (caller must hold lock)
 * ══════════════════════════════════════════════════════════════ */

static void ring_push(StreamContext *ctx, char c) {
    if (ctx->length >= STREAM_BUF_SIZE) return;  /* drop if full */
    ctx->buf[ctx->head] = c;
    ctx->head = (ctx->head + 1) % STREAM_BUF_SIZE;
    ctx->length++;
}

static char ring_pop(StreamContext *ctx) {
    if (ctx->length <= 0) return 0;
    char c = ctx->buf[ctx->tail];
    ctx->tail = (ctx->tail + 1) % STREAM_BUF_SIZE;
    ctx->length--;
    return c;
}

static char ring_peek(StreamContext *ctx, int offset) {
    if (offset >= ctx->length) return 0;
    return ctx->buf[(ctx->tail + offset) % STREAM_BUF_SIZE];
}

/* ══════════════════════════════════════════════════════════════
 * STDIN READER THREAD
 * ══════════════════════════════════════════════════════════════ */

static DWORD WINAPI stdin_reader_thread(LPVOID param) {
    StreamContext *ctx = (StreamContext *)param;
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    char tmp[256];
    DWORD n_read;

    while (ReadFile(hStdin, tmp, sizeof(tmp), &n_read, NULL) && n_read > 0) {
        EnterCriticalSection((CRITICAL_SECTION *)ctx->lock);
        for (DWORD i = 0; i < n_read; i++)
            ring_push(ctx, tmp[i]);
        LeaveCriticalSection((CRITICAL_SECTION *)ctx->lock);
    }

    /* stdin closed (pipe ended, Ctrl+Z, etc.) */
    EnterCriticalSection((CRITICAL_SECTION *)ctx->lock);
    ctx->eof = 1;
    LeaveCriticalSection((CRITICAL_SECTION *)ctx->lock);

    return 0;
}

/* ══════════════════════════════════════════════════════════════
 * PUBLIC API
 * ══════════════════════════════════════════════════════════════ */

int io_init(StreamContext *ctx) {
    memset(ctx, 0, sizeof(StreamContext));

    CRITICAL_SECTION *cs = (CRITICAL_SECTION *)malloc(sizeof(CRITICAL_SECTION));
    if (!cs) return -1;
    InitializeCriticalSection(cs);
    ctx->lock = cs;

    HANDLE h = CreateThread(NULL, 0, stdin_reader_thread, ctx, 0, NULL);
    if (!h) {
        DeleteCriticalSection(cs);
        free(cs);
        return -1;
    }
    ctx->thread = h;

    return 0;
}

int io_has_data(StreamContext *ctx) {
    EnterCriticalSection((CRITICAL_SECTION *)ctx->lock);
    int has = ctx->length > 0;
    LeaveCriticalSection((CRITICAL_SECTION *)ctx->lock);
    return has;
}

int io_read_line(StreamContext *ctx, char *out, int max_len) {
    EnterCriticalSection((CRITICAL_SECTION *)ctx->lock);

    /* Find newline in buffer */
    int nl_pos = -1;
    for (int i = 0; i < ctx->length && i < max_len; i++) {
        if (ring_peek(ctx, i) == '\n') {
            nl_pos = i;
            break;
        }
    }

    /* If no newline found and buffer is full enough, flush anyway */
    int read_len = 0;
    if (nl_pos >= 0) {
        read_len = nl_pos + 1;  /* include the newline */
    } else if (ctx->length >= max_len) {
        read_len = max_len;     /* buffer pressure: flush what we have */
    } else if (ctx->eof && ctx->length > 0) {
        read_len = ctx->length; /* EOF: flush remaining */
    }

    if (read_len > max_len) read_len = max_len;

    for (int i = 0; i < read_len; i++)
        out[i] = ring_pop(ctx);

    /* Strip trailing newline/CR for clean ingest */
    while (read_len > 0 && (out[read_len - 1] == '\n' || out[read_len - 1] == '\r'))
        read_len--;

    LeaveCriticalSection((CRITICAL_SECTION *)ctx->lock);
    return read_len;
}

int io_read_raw(StreamContext *ctx, char *out, int max_len) {
    EnterCriticalSection((CRITICAL_SECTION *)ctx->lock);

    int read_len = ctx->length;
    if (read_len > max_len) read_len = max_len;

    for (int i = 0; i < read_len; i++)
        out[i] = ring_pop(ctx);

    LeaveCriticalSection((CRITICAL_SECTION *)ctx->lock);
    return read_len;
}

int io_eof(StreamContext *ctx) {
    EnterCriticalSection((CRITICAL_SECTION *)ctx->lock);
    int eof = ctx->eof && ctx->length == 0;
    LeaveCriticalSection((CRITICAL_SECTION *)ctx->lock);
    return eof;
}

void io_sleep_ms(int ms) {
    Sleep(ms);
}

int io_stdout_is_console(void) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    return GetConsoleMode(h, &mode) ? 1 : 0;
}

void io_destroy(StreamContext *ctx) {
    if (ctx->thread) {
        /* Wait for thread to exit (ReadFile returns 0 on pipe close).
         * If it's stuck on a console read, give it 500ms then move on. */
        DWORD rc = WaitForSingleObject((HANDLE)ctx->thread, 500);
        if (rc == WAIT_TIMEOUT) {
            /* Thread still blocked — try cancelling its I/O */
            CancelSynchronousIo((HANDLE)ctx->thread);
            WaitForSingleObject((HANDLE)ctx->thread, 500);
        }
        CloseHandle((HANDLE)ctx->thread);
        ctx->thread = NULL;
    }
    if (ctx->lock) {
        DeleteCriticalSection((CRITICAL_SECTION *)ctx->lock);
        free(ctx->lock);
        ctx->lock = NULL;
    }
}
