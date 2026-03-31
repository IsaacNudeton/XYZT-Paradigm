/*
 * onetwo.h — ONETWO encoding API
 *
 * "Don't send the AI data. Send the AI rules OF the data."
 * 4096-bit structural fingerprint: repetition, opposition, nesting, meta.
 *
 * Isaac Oravec & Claude, February 2026
 */

#ifndef ONETWO_H
#define ONETWO_H

#include "engine.h"

/* Fingerprint width must fit in BS_MAXBITS (BS_WORDS * 64).
 * Step 1: clamp to 2048 to stop the overflow.
 * Step 2: raise BS_WORDS to 64 and restore 4096. */
#define OT_TOTAL 2048

/* Section offsets */
#define OT_RUN_OFF     0
#define OT_RUN_LEN     256
#define OT_AUTO_OFF    256
#define OT_AUTO_LEN    256
#define OT_FREQ_OFF    512
#define OT_FREQ_LEN    256
#define OT_DIV_OFF     768
#define OT_DIV_LEN     256
#define OT_DELTA_OFF   1024
#define OT_DELTA_LEN   256
#define OT_XOR_OFF     1280
#define OT_XOR_LEN     256
#define OT_CLASS_OFF   1536
#define OT_CLASS_LEN   256
#define OT_SYM_OFF     1792
#define OT_SYM_LEN     256
#define OT_SCALE_OFF   2048
#define OT_SCALE_LEN   256
#define OT_DEPTH_OFF   2304
#define OT_DEPTH_LEN   256
#define OT_SUBREP_OFF  2560
#define OT_SUBREP_LEN  256
#define OT_ENTG_OFF    2816
#define OT_ENTG_LEN    256
#define OT_MLEN_OFF    3072
#define OT_MLEN_LEN    64
#define OT_MENT_OFF    3136
#define OT_MENT_LEN    64
#define OT_MUNIQ_OFF   3200
#define OT_MUNIQ_LEN   64
#define OT_MDENS_OFF   3264
#define OT_MDENS_LEN   64

/* Main encoding function: raw bytes -> 4096-bit structural rules */
void onetwo_parse(const uint8_t *raw, size_t len, BitStream *out);

/* Self-observation: feed bitstream bytes back through parse */
void onetwo_self_observe(const BitStream *bs, BitStream *out);

#endif /* ONETWO_H */
