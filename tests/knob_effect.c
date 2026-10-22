/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Hiro Ikezawa */
/* How much each PERFORM knob (MORPH, INTENSITY, FEEDBACK, MOVEMENT) changes each factory preset: a held C3 chord
 * rendered with the knob at 0 and at 127, the rest as the preset has it. The figure is the RMS of the difference over
 * the RMS of the sound (0 = the knob does nothing; the engine is integer and deterministic, so 0 is exact). */
#include "bfm_host.h"
#include <math.h>

#define SR BFM_FS
static int16_t W0[2 * SR * 2], W1[2 * SR * 2];

static void play(uint32_t p, int which, uint32_t v, int16_t *w)
{
    static const uint8_t N[3] = {48, 51, 55};
    uint32_t i;
    host_init();
    bfm_p = BFM_FACTORY[p];
    bfm_p.arp.on = 0;
    bfm_p.dly_mix = 0;
    if (which == 0) bfm_p.morph = (uint8_t)v;
    if (which == 1) bfm_p.intensity = (uint8_t)v;
    if (which == 2) bfm_p.feedback = (uint8_t)v;
    if (which == 3) bfm_p.movement = (uint8_t)v;
    for (i = 0; i < 16u; i++) {                  /* as the ARP plays it: a hit every 1/8 s, so the percussive */
        uint32_t k;                              /* presets are heard through their attack, not their tail */
        for (k = 0; k < 3u; k++)
            bfm_key_push(N[k], 100, 1);
        host_render(w + i * (SR / 8u) * 2u, SR / 16u);
        for (k = 0; k < 3u; k++)
            bfm_key_push(N[k], 0, 0);
        host_render(w + i * (SR / 8u) * 2u + (SR / 16u) * 2u, SR / 8u - SR / 16u);
    }
}

static double effect(uint32_t p, int which)
{
    double d = 0, a = 0;
    uint32_t i;
    play(p, which, 0, W0);
    play(p, which, 127, W1);
    for (i = 0; i < 2u * 16u * (SR / 8u); i++) {
        double x = W0[i], y = W1[i];
        d += (x - y) * (x - y);
        a += (x * x + y * y) / 2;
    }
    return a > 0 ? sqrt(d / a) : 0;
}

#define MIN_EFFECT 0.15

int main(void)
{
    uint32_t p, k, weak = 0, pitch = 0;
    printf("== the PERFORM knobs on every factory preset (0 = no effect)\n");
    printf("%-3s %-12s %7s %7s %7s %7s\n", "#", "NAME", "MORPH", "INTENS", "FEEDBK", "MOVE");
    for (p = 0; p < BFM_NFACTORY; p++) {
        const bfm_sound_t *a = &BFM_FACTORY[p].s[0], *b = &BFM_FACTORY[p].s[1];
        double e[4];
        for (k = 0; k < 4u; k++) {
            e[k] = effect(p, (int)k);
            weak += e[k] < MIN_EFFECT;
        }
        pitch += memcmp(a->ratio, b->ratio, 4) || memcmp(a->fine, b->fine, 4) || memcmp(a->fixed, b->fixed, 4) ||
                 (BFM_FACTORY[p].mode == MODE_METAL && a->character != b->character);
        printf("%-3u %-12s %7.2f %7.2f %7.2f %7.2f\n", p + 1, BFM_FACTORY[p].name, e[0], e[1], e[2], e[3]);
    }
    CHECK(!weak, "every knob changes every preset (%u below %.2f)", weak, MIN_EFFECT);
    CHECK(!pitch, "MORPH never moves the pitch: B keeps A's ratios, fine, fixed and METAL spread (%u differ)", pitch);
    printf("knobs: %d checks, %d failed\n", t_count, t_fail);
    return t_fail != 0;
}
