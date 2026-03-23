/*
 * onetwo.c — ONETWO encoding layer
 *
 * Extracted from onetwo_encode.c. 4096-bit structural fingerprint.
 * Repetition (0-1023), Opposition (1024-2047), Nesting (2048-3071), Meta (3072-4095).
 *
 * Isaac Oravec & Claude, February 2026
 */

#include "onetwo.h"

static int ot_char_class(uint8_t c) {
    if (c >= 'A' && c <= 'Z') return 0;
    if (c >= 'a' && c <= 'z') return 1;
    if (c >= '0' && c <= '9') return 2;
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') return 3;
    if (c < 32 || c == 127) return 4;
    return 5;
}

static void ot_thermo(BitStream *bs, int base, int nbits, int on) {
    if (on > nbits) on = nbits;
    for (int i = 0; i < on; i++)
        bs_set(bs, base + i, 1);
}

void onetwo_parse(const uint8_t *raw, size_t len, BitStream *out) {
    bs_init(out);
    out->len = OT_TOTAL;
    if (!raw || len == 0) return;

    int n = (int)len;
    if (n > 1024) n = 1024;

    int freq[256] = {0};
    for (int i = 0; i < n; i++) freq[raw[i]]++;
    int unique = 0;
    for (int c = 0; c < 256; c++) if (freq[c] > 0) unique++;

    /* 1a. Run-length spectrum */
    {
        int i = 0;
        int seen[256] = {0};
        while (i < n) {
            int j = i + 1;
            while (j < n && raw[j] == raw[i]) j++;
            int rlen = j - i;
            if (rlen > 0 && rlen <= 255 && !seen[rlen]) {
                seen[rlen] = 1;
                uint8_t buf[4] = { (uint8_t)(rlen & 0xFF), (uint8_t)((rlen >> 8) & 0xFF), 'R', 'L' };
                bloom_set(out, OT_RUN_OFF, OT_RUN_LEN, buf, 4, 3);
            }
            i = j;
        }
    }

    /* 1b. Autocorrelation */
    {
        int max_lag = 32;
        if (max_lag > n - 1) max_lag = n - 1;
        for (int lag = 1; lag <= max_lag; lag++) {
            int matches = 0, pairs = n - lag;
            if (pairs <= 0) break;
            for (int i = 0; i < pairs; i++)
                if (raw[i] == raw[i + lag]) matches++;
            int score = matches * 8 / pairs;
            if (score > 8) score = 8;
            ot_thermo(out, OT_AUTO_OFF + (lag - 1) * 8, 8, score);
        }
    }

    /* 1c. Frequency shape */
    for (int c = 0; c < 256; c++) {
        if (freq[c] == 0) continue;
        int bucket;
        if      (freq[c] == 1)  bucket = 0;
        else if (freq[c] <= 3)  bucket = 1;
        else if (freq[c] <= 7)  bucket = 2;
        else if (freq[c] <= 15) bucket = 3;
        else if (freq[c] <= 31) bucket = 4;
        else if (freq[c] <= 63) bucket = 5;
        else                    bucket = 6;
        uint8_t buf[3] = { (uint8_t)c, (uint8_t)bucket, 'F' };
        bloom_set(out, OT_FREQ_OFF, OT_FREQ_LEN, buf, 3, 3);
    }

    /* 1d. Byte diversity */
    ot_thermo(out, OT_DIV_OFF, OT_DIV_LEN, unique);

    /* 2a. Delta spectrum */
    if (n >= 2) {
        for (int i = 0; i < n - 1; i++) {
            int delta = (int)raw[i + 1] - (int)raw[i];
            int sign = delta >= 0 ? 0 : 1;
            int mag = delta < 0 ? -delta : delta;
            int mb;
            if      (mag == 0)   mb = 0;
            else if (mag <= 3)   mb = 1;
            else if (mag <= 15)  mb = 2;
            else if (mag <= 31)  mb = 3;
            else if (mag <= 63)  mb = 4;
            else if (mag <= 127) mb = 5;
            else                 mb = 6;
            uint8_t buf[3] = { (uint8_t)sign, (uint8_t)mb, 'D' };
            bloom_set(out, OT_DELTA_OFF, OT_DELTA_LEN, buf, 3, 3);
        }
    }

    /* 2b. XOR spectrum */
    if (n >= 2) {
        int xhist[256] = {0};
        for (int i = 0; i < n - 1; i++)
            xhist[raw[i] ^ raw[i + 1]]++;
        int tv[32], tf[32], nt = 0;
        for (int v = 0; v < 256; v++) {
            if (xhist[v] == 0) continue;
            if (nt < 32) { tv[nt] = v; tf[nt] = xhist[v]; nt++; }
            else {
                int mk = 0;
                for (int k = 1; k < 32; k++) if (tf[k] < tf[mk]) mk = k;
                if (xhist[v] > tf[mk]) { tv[mk] = v; tf[mk] = xhist[v]; }
            }
        }
        int mf = 1;
        for (int k = 0; k < nt; k++) if (tf[k] > mf) mf = tf[k];
        for (int k = 0; k < nt; k++) {
            int on = tf[k] * 8 / mf;
            if (on > 8) on = 8;
            ot_thermo(out, OT_XOR_OFF + k * 8, 8, on);
        }
    }

    /* 2c. Class transitions */
    if (n >= 2) {
        for (int i = 0; i < n - 1; i++) {
            uint8_t ca = (uint8_t)ot_char_class(raw[i]);
            uint8_t cb = (uint8_t)ot_char_class(raw[i + 1]);
            uint8_t buf[3] = { ca, cb, 'C' };
            bloom_set(out, OT_CLASS_OFF, OT_CLASS_LEN, buf, 3, 3);
        }
    }

    /* 2d. Symmetry */
    {
        int half = n / 2;
        if (half >= 2) {
            int scales[] = {1, 2, 4, 8};
            for (int si = 0; si < 4 && scales[si] <= half; si++) {
                int s = scales[si];
                int chunks = half / s;
                if (chunks < 1) continue;
                int matches = 0, total = 0;
                for (int c = 0; c < chunks && c < 32; c++) {
                    int off1 = c * s, off2 = half + c * s;
                    if (off2 + s > n) break;
                    for (int b = 0; b < s; b++) {
                        if (raw[off1 + b] == raw[off2 + b]) matches++;
                        total++;
                    }
                }
                int score = total > 0 ? matches * 64 / total : 0;
                if (score > 64) score = 64;
                ot_thermo(out, OT_SYM_OFF + si * 64, 64, score);
            }
        }
    }

    /* 3a. Multi-scale similarity */
    {
        int scales[] = {2, 4, 8, 16};
        for (int si = 0; si < 4; si++) {
            int s = scales[si];
            if (s > n) break;
            int nc = n / s;
            for (int c = 0; c + 1 < nc && c < 32; c++) {
                int o1 = c * s, o2 = (c + 1) * s;
                int diff = 0;
                for (int b = 0; b < s; b++) {
                    diff += xyzt_popcnt32(raw[o1 + b] ^ raw[o2 + b]);
                }
                int md = s * 8, q = diff * 4 / (md + 1);
                if (q > 3) q = 3;
                uint8_t buf[4] = { (uint8_t)si, (uint8_t)q, (uint8_t)(c & 31), 'S' };
                bloom_set(out, OT_SCALE_OFF, OT_SCALE_LEN, buf, 4, 3);
            }
        }
    }

    /* 3b. Delimiter depth */
    {
        int depth[3] = {0}, maxd[3] = {0};
        for (int i = 0; i < n; i++) {
            int dt = -1, dir = 0;
            switch (raw[i]) {
                case '(': dt=0; dir=1; break; case ')': dt=0; dir=-1; break;
                case '{': dt=1; dir=1; break; case '}': dt=1; dir=-1; break;
                case '[': dt=2; dir=1; break; case ']': dt=2; dir=-1; break;
            }
            if (dt >= 0) {
                depth[dt] += dir;
                if (depth[dt] < 0) depth[dt] = 0;
                if (depth[dt] > maxd[dt]) maxd[dt] = depth[dt];
            }
        }
        for (int d = 0; d < 3; d++)
            if (maxd[d] > 0)
                for (int lev = 1; lev <= maxd[d] && lev <= 16; lev++) {
                    uint8_t buf[3] = { (uint8_t)d, (uint8_t)lev, 'N' };
                    bloom_set(out, OT_DEPTH_OFF, OT_DEPTH_LEN, buf, 3, 2);
                }
    }

    /* 3c. Substring repeats */
    {
        int sub_lens[] = {2, 3, 4};
        for (int si = 0; si < 3; si++) {
            int slen = sub_lens[si];
            if (slen > n) continue;
            int ns = n - slen + 1;
            if (ns <= 0) continue;
            int cap = ns > 128 ? 128 : ns;
            int rc = 0;
            for (int i = 0; i < cap; i++) {
                uint32_t h1 = hash32(raw + i, slen);
                for (int j = i + 1; j < cap; j++) {
                    uint32_t h2 = hash32(raw + j, slen);
                    if (h1 == h2) { rc++; break; }
                }
            }
            int bucket;
            if      (rc == 0)  bucket = 0;
            else if (rc <= 3)  bucket = 1;
            else if (rc <= 10) bucket = 2;
            else if (rc <= 30) bucket = 3;
            else               bucket = 4;
            uint8_t buf[3] = { (uint8_t)slen, (uint8_t)bucket, 'P' };
            bloom_set(out, OT_SUBREP_OFF, OT_SUBREP_LEN, buf, 3, 2);
        }
    }

    /* 3d. Entropy gradient */
    {
        double gent = 0.0;
        for (int c = 0; c < 256; c++) {
            if (freq[c] == 0) continue;
            double p = (double)freq[c] / n;
            gent -= p * log2(p);
        }
        int bsz = n >= 64 ? n / 32 : 2;
        if (bsz < 2) bsz = 2;
        int nb = n / bsz;
        if (nb > 32) nb = 32;
        for (int b = 0; b < nb; b++) {
            int off = b * bsz;
            int bl = (off + bsz <= n) ? bsz : n - off;
            if (bl < 1) continue;
            int bf[256] = {0};
            for (int k = 0; k < bl; k++) bf[raw[off + k]]++;
            double bent = 0.0;
            for (int c = 0; c < 256; c++) {
                if (bf[c] == 0) continue;
                double p = (double)bf[c] / bl;
                bent -= p * log2(p);
            }
            int ratio = gent > 0.01 ? (int)(bent * 8 / gent) : 0;
            if (ratio > 8) ratio = 8;
            ot_thermo(out, OT_ENTG_OFF + b * 8, 8, ratio);
        }
    }

    /* 4a. Length class */
    { int lc = 0, tmp = n; while (tmp > 1) { lc++; tmp >>= 1; } if (lc > 64) lc = 64; ot_thermo(out, OT_MLEN_OFF, OT_MLEN_LEN, lc); }

    /* 4b. Overall entropy */
    {
        double gent = 0.0;
        for (int c = 0; c < 256; c++) { if (freq[c] == 0) continue; double p = (double)freq[c] / n; gent -= p * log2(p); }
        int es = (int)(gent * 8); if (es > 64) es = 64;
        ot_thermo(out, OT_MENT_OFF, OT_MENT_LEN, es);
    }

    /* 4c. Unique byte ratio */
    { int ur = unique * 64 / 256; if (ur > 64) ur = 64; ot_thermo(out, OT_MUNIQ_OFF, OT_MUNIQ_LEN, ur); }

    /* 4d. Density */
    {
        int sb = 0;
        for (int i = 0; i < n; i++) {
            sb += xyzt_popcnt32(raw[i]);
        }
        int d = sb * 64 / (n * 8); if (d > 64) d = 64;
        ot_thermo(out, OT_MDENS_OFF, OT_MDENS_LEN, d);
    }
}

void onetwo_self_observe(const BitStream *bs, BitStream *out) {
    int byte_len = (bs->len + 7) / 8;
    if (byte_len > 1024) byte_len = 1024;
    onetwo_parse((const uint8_t *)bs->w, byte_len, out);
}
