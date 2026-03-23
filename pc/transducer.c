/*
 * transducer.c — Input layer: file and byte encoding
 *
 * Isaac Oravec & Claude, March 2026
 */

#include "transducer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    encode_bytes(out, buf, n);
    free(buf);
    return n;
}
