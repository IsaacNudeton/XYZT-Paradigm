/*
 * transducer.c — Input layer implementation
 *
 * v3 analog->binary with auto-calibrating threshold.
 * Serial port support via Windows API.
 *
 * Isaac Oravec & Claude, February 2026
 */

#include "transducer.h"
#include "onetwo.h"
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
static HANDLE serial_handle = INVALID_HANDLE_VALUE;
#endif

void transducer_init(Transducer *t, int mode, const char *path, int baud) {
    memset(t, 0, sizeof(*t));
    t->mode = mode;
    t->baud = baud > 0 ? baud : 115200;
    t->threshold = 128;    /* Start at midpoint */
    t->min_seen = 255;
    t->max_seen = 0;
    if (path) strncpy(t->path, path, 255);
}

int transducer_read(Transducer *t, uint8_t *buf, int max_bytes) {
    switch (t->mode) {
    case XDUCE_STDIN: {
        /* Read one line from stdin */
        char line[4096];
        if (!fgets(line, sizeof(line), stdin)) return -1;
        int len = (int)strlen(line);
        if (len > max_bytes) len = max_bytes;
        memcpy(buf, line, len);
        return len;
    }

    case XDUCE_FILE: {
        FILE *f = fopen(t->path, "rb");
        if (!f) return -1;
        int n = (int)fread(buf, 1, max_bytes, f);
        fclose(f);
        return n;
    }

    case XDUCE_SERIAL: {
#ifdef _WIN32
        if (serial_handle == INVALID_HANDLE_VALUE) {
            /* Open serial port */
            char portname[32];
            snprintf(portname, sizeof(portname), "\\\\.\\%s", t->path);
            serial_handle = CreateFileA(portname, GENERIC_READ | GENERIC_WRITE,
                0, NULL, OPEN_EXISTING, 0, NULL);
            if (serial_handle == INVALID_HANDLE_VALUE) return -1;

            DCB dcb = {0};
            dcb.DCBlength = sizeof(dcb);
            GetCommState(serial_handle, &dcb);
            dcb.BaudRate = t->baud;
            dcb.ByteSize = 8;
            dcb.Parity = NOPARITY;
            dcb.StopBits = ONESTOPBIT;
            SetCommState(serial_handle, &dcb);

            COMMTIMEOUTS timeouts = {0};
            timeouts.ReadTotalTimeoutConstant = 1000;
            SetCommTimeouts(serial_handle, &timeouts);
        }

        DWORD bytes_read = 0;
        ReadFile(serial_handle, buf, max_bytes, &bytes_read, NULL);
        return (int)bytes_read;
#else
        return -1;  /* Serial not supported on this platform */
#endif
    }

    case XDUCE_RAW:
        /* Raw mode: caller provides data directly */
        return 0;

    default:
        return -1;
    }
}

void transducer_to_bits(Transducer *t, const uint8_t *raw, int len, BitStream *out) {
    bs_init(out);

    /* Update calibration from new samples */
    for (int i = 0; i < len; i++) {
        if (raw[i] < t->min_seen) t->min_seen = raw[i];
        if (raw[i] > t->max_seen) t->max_seen = raw[i];
        t->sum += raw[i];
        t->n_samples++;
    }

    /* Auto-calibrate threshold: midpoint of observed range.
     * Adapts to whatever the input distribution looks like. */
    if (t->n_samples > 0 && t->max_seen > t->min_seen) {
        t->threshold = (t->min_seen + t->max_seen) / 2;
    }

    /* Convert: above threshold = 1, below = 0 */
    for (int i = 0; i < len && out->len < BS_MAXBITS; i++) {
        bs_push(out, raw[i] >= t->threshold ? 1 : 0);
    }
}

int transducer_ingest(Transducer *t, BitStream *out) {
    uint8_t buf[4096];
    int n = transducer_read(t, buf, sizeof(buf));
    if (n <= 0) return n;

    transducer_to_bits(t, buf, n, out);
    return n;
}

int transducer_file(const char *path, BitStream *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz > 4096) sz = 4096;
    uint8_t *buf = (uint8_t *)malloc(sz);
    int n = (int)fread(buf, 1, sz, f);
    fclose(f);

    /* Raw bytes — let the field find structure, not the encoder */
    encode_bytes(out, buf, n);
    free(buf);
    return n;
}

int transducer_stdin(BitStream *out) {
    char line[4096];
    if (!fgets(line, sizeof(line), stdin)) return -1;
    int len = (int)strlen(line);
    /* Strip trailing newline */
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) len--;

    encode_bytes(out, (const uint8_t *)line, len);
    return len;
}
