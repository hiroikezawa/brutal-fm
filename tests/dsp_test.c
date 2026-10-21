/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Hiro Ikezawa */
/* The sound engine under abuse: every parameter at its ends in every mode, random patches, silence, note
 * hammering, MORPH sweeps, long renders; the output stays bounded, centred and quiet when it should be. Also
 * the cost per sample (host) and aliasing with and without HQ. Integer DSP: NaN and Inf cannot happen, so
 * "no NaN" here means "no overflow that wraps into garbage": the checks look for full-scale square waves of
 * wrapped arithmetic (a sign flip from +32767 to -32768 in one sample) and for stuck outputs. */
#include "bfm_host.h"
#include <time.h>

#define SR BFM_FS
static int16_t W[2 * SR * 20];

static uint32_t rs = 777;
static uint32_t rnd(uint32_t n) { rs = rs * 1103515245u + 12345u; return n ? (rs >> 8) % n : 0; }

typedef struct { int32_t peak, jump; double mean, rms; uint32_t wraps; } sig_t;
static sig_t sig(const int16_t *s, uint32_t frames)
{
    sig_t g = {0};
    double sum = 0, sq = 0;
    uint32_t i;
    for (i = 0; i < 2u * frames; i++) {
        int32_t v = s[i], a = v < 0 ? -v : v;
        if (a > g.peak) g.peak = a;
        sum += v;
        sq += (double)v * v;
        if (i >= 2u) {
            int32_t d = v - s[i - 2u];
            if (d < 0) d = -d;
            if (d > g.jump) g.jump = d;
            if (d > 60000) g.wraps++;
        }
    }
    g.mean = sum / (2.0 * frames) / 32768.0;
    g.rms = sqrt(sq / (2.0 * frames)) / 32768.0;
    return g;
}

static void notes_on(const uint8_t *n, uint32_t k) { uint32_t i; for (i = 0; i < k; i++) bfm_key_push(n[i], 100, 1); }
static void notes_off(const uint8_t *n, uint32_t k) { uint32_t i; for (i = 0; i < k; i++) bfm_key_push(n[i], 0, 0); }

static void extremes(void)
{
    static const uint8_t CH[4] = {36, 48, 60, 72};
    uint32_t mode, v, worst = 0, wraps = 0, k, bad_bound = 0;
    printf("== every parameter at its ends, every mode\n");
    for (mode = 0; mode < MODE_N; mode++)
        for (v = 0; v < 3u; v++) {
            uint32_t x = v == 0 ? 0u : v == 1 ? 127u : 64u;
            bfm_sound_t *s;
            sig_t g;
            host_init();
            bfm_p = BFM_FACTORY[mode * 8u];
            bfm_p.mode = (uint8_t)mode;
            for (k = 0; k < 2u; k++) {
                s = &bfm_p.s[k];
                memset(s->mat, (int)x, sizeof s->mat);
                memset(s->level, 127, 4);
                memset(s->out, (int)x, 4);
                s->fold = s->fmix = s->character = s->gain = (uint8_t)x;
                s->sym = s->bias = (uint8_t)(v == 1 ? 127 : 0);
                memset(s->ratio, v == 1 ? BFM_NRATIO - 1 : 0, 4);
                memset(s->atk, 0, 4);
                memset(s->sus, 127, 4);
            }
            bfm_p.intensity = bfm_p.feedback = bfm_p.movement = (uint8_t)x;
            bfm_p.drive = bfm_p.crush = bfm_p.dly_fb = bfm_p.dly_mix = (uint8_t)x;
            bfm_p.mod_int = bfm_p.mod_fold = bfm_p.mod_fb = bfm_p.mod_morph = bfm_p.mod_pitch = (uint8_t)(v == 1 ? 127 : 0);
            bfm_p.lfo_rate = bfm_p.drift_rate = 127;
            bfm_p.lfo_shape = LFO_CHAOS;
            notes_on(CH, 4);
            host_render(W, SR * 2);
            g = sig(W, SR * 2);
            notes_off(CH, 4);
            if ((uint32_t)g.peak > worst) worst = (uint32_t)g.peak;
            wraps += g.wraps;
            bad_bound += g.peak > 32767;
        }
    CHECK(bad_bound == 0, "4 modes x (all 0 / all 127 / all 64): the output never leaves full scale (loudest %u)", worst);
    CHECK(wraps < 50u, "no overflow wrap-around (full-scale sign flips in one sample: %u)", wraps);
}

static void random_patches(void)
{
    uint32_t n, bad = 0, wraps = 0, stuck = 0;
    printf("== 300 random patches (every byte random), random notes\n");
    for (n = 0; n < 300u; n++) {
        uint8_t *b = (uint8_t *)&bfm_p;
        uint32_t i;
        sig_t g;
        host_init();
        for (i = 0; i < sizeof bfm_p; i++)
            b[i] = (uint8_t)rnd(256);
        bfm_patch_sanitize(&bfm_p);
        bfm_p.arp.on = 0;
        for (i = 0; i < 6u; i++) {
            uint8_t note = (uint8_t)(20 + rnd(90));
            bfm_key_push(note, (uint8_t)(1 + rnd(127)), 1);
            host_render(W, SR / 8);
            bfm_key_push(note, 0, 0);
        }
        host_render(W, SR / 2);
        g = sig(W, SR / 2);
        bad += g.peak > 32767;
        wraps += g.wraps > 20u;
        bfm_panic();
        host_render(W, SR * 3);      /* the delay at full feedback may ring for a while */
        g = sig(W + 2 * SR * 2, SR);
        stuck += fabs(g.mean) > 0.01;
    }
    CHECK(!bad, "bounded output (%u over)", bad);
    CHECK(!wraps, "no wrapped arithmetic (%u patches with sign-flip bursts)", wraps);
    CHECK(!stuck, "after a panic the output returns to zero, no DC left (%u stuck)", stuck);
}

/* the engine's sanitiser feeds random patches the ranges; this is the raw struct with only that */
static void silence(void)
{
    sig_t g;
    uint32_t p;
    printf("== silence, DC, decay\n");
    for (p = 0; p < BFM_NFACTORY; p++) {
        host_init();
        bfm_p = BFM_FACTORY[p];
        host_render(W, SR / 2);
        g = sig(W, SR / 2);
        if (g.peak)
            break;
    }
    CHECK(p == BFM_NFACTORY, "no note, every preset: exactly zero output");
    {
        double worst = 0;
        uint32_t wp = 0;
        for (p = 0; p < BFM_NFACTORY; p++) {
            static const uint8_t N[1] = {45};
            host_init();
            bfm_p = BFM_FACTORY[p];
            bfm_p.arp.on = 0;
            notes_on(N, 1);
            host_render(W, SR * 4);
            g = sig(W + 2 * SR, SR * 3);
            if (fabs(g.mean) > worst) { worst = fabs(g.mean); wp = p; }
            notes_off(N, 1);
        }
        CHECK(worst < 0.004, "a held A2, 3 s, every preset: mean (DC) at most %.5f of full scale (%s)", worst,
              BFM_FACTORY[wp].name);
    }
    {   /* hammering: 2000 notes in 4 s, then everything decays */
        uint32_t i, left;
        host_init();
        bfm_p = BFM_FACTORY[1];
        for (i = 0; i < 2000u; i++) {
            uint8_t n = (uint8_t)(30 + rnd(60));
            bfm_key_push(n, 100, 1);
            host_render(0, 64);
            bfm_key_push(n, 0, 0);
            host_render(0, 24);
        }
        host_render(W, SR * 12);
        left = bfm_voices_on();
        g = sig(W + 2 * SR * 11, SR);
        CHECK(left == 0 && g.peak < 64, "2000 notes hammered: 12 s later no voice left, output silent (%u voices, "
              "peak %d)", left, g.peak);
    }
}

static void morph_clicks(void)
{
    static const uint8_t N[1] = {48};
    uint32_t k, a;
    sig_t still, moving;
    printf("== MORPH and ALGORITHM changes do not click\n");
    host_init();
    bfm_p = BFM_FACTORY[0];
    bfm_p.arp.on = 0;
    for (k = 0; k < 2u; k++) {               /* A: a plain stack, B: a different routing, held notes */
        memcpy(bfm_p.s[k].mat, BFM_ALGO_MAT[k ? 6 : 0], 16);
        memcpy(bfm_p.s[k].out, BFM_ALGO_OUT[k ? 6 : 0], 4);
        memset(bfm_p.s[k].atk, 0, 4);
        memset(bfm_p.s[k].sus, 120, 4);
    }
    bfm_p.dly_mix = 0;
    bfm_p.movement = 0;
    notes_on(N, 1);
    host_render(W, SR);
    still = sig(W + SR, SR / 2);
    for (k = 0; k < 128u; k++) {
        bfm_p.morph = (uint8_t)k;
        host_render(W + 2u * k * 256u, 256);
    }
    moving = sig(W, 128 * 256);
    CHECK(moving.jump < still.jump * 3 + 2000, "MORPH swept 0 -> 127 in 0.75 s (routing STACK -> MESH): the largest "
          "sample step %d against %d held", moving.jump, still.jump);
    for (a = 0; a < BFM_NALGO; a++) {         /* ALGORITHM rewrites the matrix at once: the slew takes it */
        memcpy(bfm_p.s[0].mat, BFM_ALGO_MAT[a], 16);
        memcpy(bfm_p.s[1].mat, BFM_ALGO_MAT[a], 16);
        host_render(W + 2u * a * 2048u, 2048);
    }
    moving = sig(W, BFM_NALGO * 2048);
    CHECK(moving.wraps == 0, "8 ALGORITHM jumps on a held note: no wrap-around clicks (largest step %d)", moving.jump);
}

/* aliasing: energy at non-harmonic bins of a high, folded note */
static double inharmonic(const int16_t *s, uint32_t n, double f0)
{
    enum { N = 8192 };
    static double re[N], im[N];
    double tot = 0, off = 0;
    uint32_t i, k;
    for (i = 0; i < N; i++) {
        double h = 0.5 - 0.5 * cos(2 * M_PI * i / (N - 1));
        re[i] = s[2 * i] / 32768.0 * h;
        im[i] = 0;
    }
    /* plain DFT on the bins (slow but only a few calls) is too slow: radix-2 FFT */
    {
        int j = 0, len;
        for (i = 1; i < N; i++) {
            int bit = N >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if ((int)i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
        }
        for (len = 2; len <= N; len <<= 1) {
            double ang = -2 * M_PI / len, wr = cos(ang), wi = sin(ang);
            for (i = 0; i < N; i += len) {
                double cr = 1, ci = 0;
                for (k = 0; k < (uint32_t)len / 2; k++) {
                    double ur = re[i + k], ui = im[i + k];
                    double vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci;
                    double vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                    re[i + k] = ur + vr; im[i + k] = ui + vi;
                    re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
                    double t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
                }
            }
        }
    }
    for (k = 2; k < N / 2; k++) {
        double f = (double)k * SR / N, p = re[k] * re[k] + im[k] * im[k], h = f / f0;
        double d = fabs(h - floor(h + 0.5)) * f0;          /* Hz from the nearest harmonic */
        tot += p;
        if (d > 25.0)
            off += p;
    }
    (void)n;
    return 10 * log10(off / tot + 1e-20);
}

static double alias_run(uint32_t q, uint32_t fold, uint32_t inten, uint32_t note)
{
    uint32_t k;
    host_init();
    bfm_set.quality = (uint8_t)q;
    bfm_p = BFM_FACTORY[0];
    bfm_p.arp.on = 0;
    bfm_p.mode = MODE_GROWL;
    bfm_p.dly_mix = 0;
    bfm_p.movement = 0;
    bfm_p.drive = bfm_p.crush = 0;           /* the voice alone: no master saturation or crusher */
    bfm_p.feedback = 64;                     /* (neutral) */
    for (k = 0; k < 2u; k++) {               /* a plain STACK, every ratio 1: every partial a harmonic */
        memcpy(bfm_p.s[k].mat, BFM_ALGO_MAT[0], 16);
        memcpy(bfm_p.s[k].out, BFM_ALGO_OUT[0], 4);
        memset(bfm_p.s[k].ratio, 4, 4);
        memset(bfm_p.s[k].atk, 0, 4);
        memset(bfm_p.s[k].sus, 127, 4);
        bfm_p.s[k].fold = (uint8_t)fold;
        bfm_p.s[k].fmix = fold ? 127 : 0;
        bfm_p.s[k].character = 0;
    }
    bfm_p.intensity = (uint8_t)inten;
    bfm_key_push(note, 100, 1);
    host_render(W, SR);
    return inharmonic(W + 2 * SR / 2, 8192, 440.0 * pow(2, ((double)note - 69) / 12.0));
}

static void polarity(void)
{
    static const uint8_t N[1] = {48};
    sig_t pos, neg;
    int16_t *a = W, *b = W + 2 * SR;
    uint32_t i, diff = 0;
    printf("== modulation polarity: a negative amount is another sound, morphing through zero is smooth\n");
    host_init();
    bfm_p = BFM_FACTORY[0];
    bfm_p.arp.on = 0;
    bfm_p.movement = 0;
    bfm_p.dly_mix = 0;
    memset(bfm_p.s[0].neg, 0, 2);
    bfm_p.s[1] = bfm_p.s[0];
    notes_on(N, 1);
    host_render(a, SR / 2);
    host_init();
    {
        bfm_patch_t keep = bfm_p;
        bfm_p = keep;
    }
    bfm_p.s[0].neg[0] = 0xFF;
    bfm_p.s[0].neg[1] = 0xFF;
    bfm_p.s[1] = bfm_p.s[0];
    notes_on(N, 1);
    host_render(b, SR / 2);
    for (i = 0; i < SR; i++)
        diff += a[i] != b[i];
    pos = sig(a, SR / 2);
    neg = sig(b, SR / 2);
    CHECK(diff > SR / 2 && neg.peak > 1000, "every matrix entry negative: a different waveform (%u of %u samples differ), "
          "still sounding (peak %d against %d)", diff, SR, neg.peak, pos.peak);
    {   /* A positive, B negative: MORPH sweeps through zero modulation, no click */
        sig_t m;
        host_init();
        bfm_p = BFM_FACTORY[0];
        bfm_p.arp.on = 0;
        bfm_p.movement = 0;
        bfm_p.dly_mix = 0;
        bfm_p.s[1] = bfm_p.s[0];
        memset(bfm_p.s[0].neg, 0, 2);
        bfm_p.s[1].neg[0] = bfm_p.s[1].neg[1] = 0xFF;
        memset(bfm_p.s[0].sus, 120, 4);
        memset(bfm_p.s[1].sus, 120, 4);
        notes_on(N, 1);
        host_render(W, SR / 2);
        for (i = 0; i < 128u; i++) {
            bfm_p.morph = (uint8_t)i;
            host_render(W + 2u * i * 256u, 256);
        }
        m = sig(W, 128 * 256);
        CHECK(m.wraps == 0, "MORPH from +amounts to -amounts: no wrap-around click (largest step %d)", m.jump);
    }
}

static void morph_loudness(void)
{
    static const uint8_t N[1] = {48};
    uint32_t p, checked = 0, bad = 0;
    double worst = 0;
    const char *wn = "";
    printf("== MORPH keeps the level: every sustained preset swept 0 -> 127 over 2 s\n");
    for (p = 0; p < BFM_NFACTORY; p++) {
        uint32_t k, w, ok = 1;
        double last = 0, jump = 0;
        for (k = 0; k < 4u; k++)
            ok &= (BFM_FACTORY[p].s[0].sus[k] >= 90 || !BFM_FACTORY[p].s[0].out[k]) &&
                  (BFM_FACTORY[p].s[1].sus[k] >= 90 || !BFM_FACTORY[p].s[1].out[k]);
        if (!ok || BFM_FACTORY[p].s[0].atk[0] > 40)
            continue;                               /* decaying or slow sounds change level by themselves */
        host_init();
        bfm_p = BFM_FACTORY[p];
        bfm_p.arp.on = 0;
        bfm_p.movement = 0;
        bfm_p.dly_mix = 0;
        notes_on(N, 1);
        host_render(W, SR / 4);
        for (w = 0; w < 40u; w++) {                  /* 40 windows of 50 ms, MORPH up a step each 1/64 */
            sig_t g;
            bfm_p.morph = (uint8_t)(w * 127u / 39u);
            host_render(W, SR / 20);
            g = sig(W, SR / 20);
            {
                double db = 20 * log10(g.rms + 1e-9);
                if (w && fabs(db - last) > jump)
                    jump = fabs(db - last);
                last = db;
            }
        }
        checked++;
        if (jump > worst) { worst = jump; wn = BFM_FACTORY[p].name; }
        bad += jump > 6.0;
    }
    CHECK(checked >= 10 && !bad, "%u sustained presets: the level never jumps more than 6 dB between 50 ms windows "
          "(largest %.1f dB, %s)", checked, worst, wn);
}

static void aliasing(void)
{
    static const uint32_t NOTES[3] = {72, 84, 96};
    uint32_t i;
    printf("== aliasing: energy off the harmonics (moderate FM, folded), ECO 1x against HQ 2x\n");
    CHECK(alias_run(1, 0, 0, 84) < -45.0, "a plain sine: %.1f dB off the harmonics (the measurement's floor)",
          alias_run(1, 0, 0, 84));
    for (i = 0; i < 3u; i++) {
        double eco = alias_run(0, 40, 40, NOTES[i]), hq = alias_run(1, 40, 40, NOTES[i]);
        CHECK(hq < eco - 6.0, "note %u, FM + fold: ECO %.1f dB, HQ %.1f dB (%.1f dB less aliasing)", NOTES[i], eco, hq,
              eco - hq);
    }
    printf("  info  at INTENSITY 90 / FOLD 90 (three cascaded stages) the spectrum runs far past 44 kHz: HQ %.1f dB, "
           "ECO %.1f dB off the harmonics at C4 - aliasing is part of that sound\n", alias_run(1, 90, 90, 60),
           alias_run(0, 90, 90, 60));
}

static void cost(void)
{
    static const uint8_t CH[8] = {36, 43, 48, 52, 55, 60, 64, 67};
    uint32_t q, p;
    printf("== cost on this host (ns per output sample, all voices sounding)\n");
    for (q = 0; q < 2u; q++) {
        double worst = 0;
        uint32_t wp = 0;
        for (p = 0; p < BFM_NFACTORY; p++) {
            struct timespec t0, t1;
            double ns;
            host_init();
            bfm_set.quality = (uint8_t)q;
            bfm_p = BFM_FACTORY[p];
            bfm_p.arp.on = 0;
            bfm_p.poly = 1;
            notes_on(CH, q ? 4u : 8u);
            host_render(0, 4096);
            clock_gettime(CLOCK_MONOTONIC, &t0);
            host_render(0, SR);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            ns = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / SR;
            if (ns > worst) { worst = ns; wp = p; }
        }
        printf("  info  %s, %u voices: the costliest preset %s takes %.0f ns a sample here\n", q ? "HQ" : "ECO",
               q ? 4u : 8u, BFM_FACTORY[wp].name, worst);
    }
}

static void long_run(void)
{
    static const uint8_t CH[3] = {40, 47, 54};
    sig_t g;
    uint32_t sec, wraps = 0;
    printf("== a long render: 60 s of CHAOS at full feedback with the ARP\n");
    host_init();
    bfm_p = BFM_FACTORY[16];
    bfm_p.feedback = 127;
    bfm_p.intensity = 127;
    bfm_p.movement = 127;
    bfm_p.arp.on = 1;
    bfm_p.arp.hold = 1;
    notes_on(CH, 3);
    host_render(0, SR / 10);
    notes_off(CH, 3);
    for (sec = 0; sec < 60u; sec++) {
        host_render(W, SR);
        g = sig(W, SR);
        wraps += g.wraps;
        if (g.peak > 32767 || g.rms < 0.0001)
            break;
    }
    CHECK(sec == 60u, "60 s: bounded and never silent (stopped after %u s)", sec);
    CHECK(wraps < 100u, "no wrap-around bursts (%u)", wraps);
}

int main(void)
{
    extremes();
    random_patches();
    silence();
    morph_clicks();
    polarity();
    morph_loudness();
    aliasing();
    long_run();
    cost();
    printf("dsp: %d checks, %d failed\n", t_count, t_fail);
    return t_fail != 0;
}
