/*
 * io.h — Streaming input (ring buffer + stdin listener thread)
 *
 * Thread-safe circular buffer for continuous stdin ingestion.
 * Producer thread reads stdin, consumer loop ingests into engine.
 *
 * Isaac Oravec & Claude, March 2026
 */

#ifndef IO_H
#define IO_H

#include <stdint.h>

#define STREAM_BUF_SIZE  1024
#define STREAM_WINDOW    256   /* max bytes per ingest chunk */

typedef struct {
    char     buf[STREAM_BUF_SIZE];
    int      head;        /* write position */
    int      tail;        /* read position */
    int      length;      /* bytes available */
    int      eof;         /* stdin closed */
    void    *lock;        /* CRITICAL_SECTION* (opaque for C) */
    void    *thread;      /* thread handle */
} StreamContext;

/* Initialize stream context, start stdin listener thread */
int  io_init(StreamContext *ctx);

/* Check if data is available in the ring buffer */
int  io_has_data(StreamContext *ctx);

/* Read up to max_len bytes from ring buffer into out.
 * In text mode: reads until newline or max_len.
 * Returns number of bytes read (0 if no complete line). */
int  io_read_line(StreamContext *ctx, char *out, int max_len);

/* Read up to max_len bytes from ring buffer (binary, no delimiter).
 * Returns number of bytes read. */
int  io_read_raw(StreamContext *ctx, char *out, int max_len);

/* Check if stdin has been closed */
int  io_eof(StreamContext *ctx);

/* Sleep for ms milliseconds (portable wrapper) */
void io_sleep_ms(int ms);

/* Check if stdout is a console (not redirected to file/pipe) */
int io_stdout_is_console(void);

/* Cleanup: signal thread to stop, join, free resources */
void io_destroy(StreamContext *ctx);

#endif /* IO_H */
