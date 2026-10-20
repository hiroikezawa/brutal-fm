/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Hiro Ikezawa */
/* The host side of the tests: the portable core (firmware/brutal/bfm_core.c) built with the host compiler,
 * plus helpers (WAV out, a render loop, statistics). Integer DSP: the host computes exactly what the FM-1
 * computes, so a hash of a render here is a hash of the device's output. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define BFM_POOL_SECTION
#include "../firmware/brutal/bfm_core.c"
#ifndef BFM_FLASH
#define BFM_FLASH 0                     /* store_test.c builds storage.c over a RAM NOR */
static void str_cpy(char *d, const char *s, unsigned max)
{
    unsigned i = 0;
    while (i + 1u < max && s[i]) { d[i] = s[i]; i++; }
    if (max) d[i] = 0;
}
#endif
#include "../firmware/brutal/bfm_store.c"

static int t_fail, t_count;
#define CHECK(cond, ...) do { t_count++; if (cond) { printf("  ok    "); } else { t_fail++; printf("  FAIL  "); } \
    printf(__VA_ARGS__); printf("\n"); } while (0)

static void host_init(void)
{
    memset(&bfm_set, 0, sizeof bfm_set);
    bfm_set.magic = BFM_SETTINGS_MAGIC;
    bfm_set.bpm = 120 - 40;
    bfm_set.quality = 1;
    bfm_set.voices = 8;
    bfm_set.cc[0] = 1; bfm_set.cc[1] = 74; bfm_set.cc[2] = 71; bfm_set.cc[3] = 76;
    bfm_set.cc_on = 1;
    bfm_set.pc_on = 1;
    bfm_dsp_init();
    bfm_seq_init();
    bfm_p = BFM_FACTORY[0];
}

static void host_load(uint32_t i)                       /* a factory preset, as the UI loads it */
{
    bfm_stage = BFM_FACTORY[i % BFM_NFACTORY];
    rq_load = 1;
}

/* render n stereo frames (blocks of BFM_CTL) into buf (may be NULL); returns the peak */
static int32_t host_render(int16_t *buf, uint32_t frames)
{
    int32_t out[2 * BFM_CTL], pk = 0;
    uint32_t f = 0, i;
    while (f < frames) {
        uint32_t n = frames - f < BFM_CTL ? frames - f : BFM_CTL;
        bfm_block(out, n);
        for (i = 0; i < 2u * n; i++) {
            int32_t v = out[i];
            if (v > pk) pk = v;
            if (-v > pk) pk = -v;
            if (buf)
                buf[2u * f + i] = (int16_t)v;
        }
        f += n;
    }
    return pk;
}

static void wav_write(const char *path, const int16_t *s, uint32_t frames)
{
    FILE *f = fopen(path, "wb");
    uint32_t v;
    uint16_t a = 1, ch = 2, ba = 4, bits = 16;
    uint32_t sr = BFM_FS, br = BFM_FS * 4u;
    if (!f) { perror(path); return; }
    fwrite("RIFF", 1, 4, f); v = 36u + frames * 4u; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    fwrite(&a, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); v = frames * 4u; fwrite(&v, 4, 1, f);
    fwrite(s, 4, frames, f);
    fclose(f);
}

static uint32_t fnv(const void *p, uint32_t n, uint32_t h)
{
    const uint8_t *b = p;
    while (n--) { h ^= *b++; h *= 16777619u; }
    return h;
}
