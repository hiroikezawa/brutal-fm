/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Hiro Ikezawa */
/* Factory preset loudness: the loudest 50 ms of a held C3 (raw, without the limiter), sound A (MORPH 0) and
 * sound B (MORPH 127) of every preset. With CAL=1 prints the GAIN each sound needs to reach the target, the line
 * tools/gen_bfm_factory.py reads (tools/factory_gain.txt). Otherwise checks every preset against the target. */
#include "bfm_host.h"
#define SR BFM_FS
#define TARGET_DB (-15.0)
static int32_t B2[2 * SR * 2];

static double loudest(uint32_t p, uint32_t morph)
{
    static int16_t dummy;
    uint32_t i, w, n = SR * 3 / 2;
    double best = 0;
    host_init();
    bfm_lim_off = 1;
    bfm_p = BFM_FACTORY[p];
    bfm_p.arp.on = 0;
    bfm_p.morph = (uint8_t)morph;
    bfm_p.movement = 0;                        /* the static sound */
    bfm_p.dly_mix = 0;
    bfm_key_push(48, 100, 1);
    {
        int32_t out[2 * BFM_CTL];
        uint32_t f = 0;
        while (f < n) {
            bfm_block(out, BFM_CTL);
            for (i = 0; i < 2 * BFM_CTL; i++)
                B2[2 * f + i] = out[i];
            f += BFM_CTL;
        }
    }
    for (w = 0; w + SR / 20 <= n; w += SR / 100) {
        double sq = 0;
        for (i = w; i < w + SR / 20; i++)
            sq += (double)B2[2 * i] * B2[2 * i];
        sq = sqrt(sq / (SR / 20)) / 32768.0;
        if (sq > best)
            best = sq;
    }
    (void)dummy;
    bfm_lim_off = 0;
    return 20 * log10(best + 1e-9);
}

int main(void)
{
    uint32_t p, bad = 0;
    int cal = getenv("CAL") != 0;
    for (p = 0; p < BFM_NFACTORY; p++) {
        double a = loudest(p, 0), b = loudest(p, 127);
        if (cal) {
            int ga = BFM_FACTORY[p].s[0].gain + (int)lround((TARGET_DB - a) / 0.375);
            int gb = BFM_FACTORY[p].s[1].gain + (int)lround((TARGET_DB - b) / 0.375);
            ga = ga < 40 ? 40 : ga > 127 ? 127 : ga;
            gb = gb < 40 ? 40 : gb > 127 ? 127 : gb;
            printf("%s|%d|%d\n", BFM_FACTORY[p].name, ga, gb);
        } else {
            int ok = fabs(a - TARGET_DB) < 3.0 && fabs(b - TARGET_DB) < 3.0;
            bad += !ok;
            printf("  %s  %-12s A %6.1f dB  B %6.1f dB\n", ok ? "ok  " : "FAIL", BFM_FACTORY[p].name, a, b);
        }
    }
    if (!cal)
        printf("calibrate: %u presets outside %.0f +- 3 dB\n", bad, TARGET_DB);
    return bad != 0;
}
