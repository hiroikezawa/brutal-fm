/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Hiro Ikezawa */
/* BRUTAL FM: the sound engine. Integer only (Q15 audio, Q32 phases, Q12 octaves), runs in the audio ISR.
 *
 * VOICE   four sine operators, phase modulation through a full 4x4 matrix (the patch's mat[], A and B
 *         interpolated by MORPH, so changing the routing is a continuous move and never a switch). Operators
 *         are computed OP4 -> OP1 each sample: an edge from a higher operator to a lower one sees this
 *         sample's output (no delay); an edge the other way, or onto itself, sees the previous sample (one
 *         sample of delay breaks the algebraic loop of bidirectional cross FM). Self feedback averages the
 *         last two outputs (as the DX7 does) so high feedback turns to noise rather than to a stuck square.
 *         Matrix amounts slew per block (no click when ALGORITHM rewrites the matrix).
 * MODES   METAL  CHARACTER stretches the operator ratios apart (inharmonic, bell / metal)
 *         GROWL  CHARACTER adds a clean body: the fundamental and its sub octave, after the folder, so the
 *                low end never disappears under the modulation
 *         CHAOS  CHARACTER bends every delayed path (cross and self feedback) through a sine fold and feeds
 *                the voice's own output back into every operator's phase
 *         NOISE  CHARACTER injects noise into every operator's phase and reduces the operators' bit depth
 * FOLDER  sine wavefolder after the operators: FOLD (gain into the fold), SYMMETRY, BIAS, MIX
 * QUALITY HQ renders every voice (operators and folder) at 2x and decimates the sum with a half-band FIR;
 *         ECO renders at 1x (twice the voices for the same cost)
 * MASTER  DC blocker, DRIVE (tanh), CRUSH (rate + bits), a ping-pong DELAY, a peak LIMITER, a soft clip, the
 *         MASTER pot. Nothing past the soft clip can exceed full scale, whatever the patch does.
 * Every value that reaches a multiply is clamped first: NaN and Inf do not exist in integers, and no
 * product here can overflow its int32 (bfm_dsp_test.c drives every parameter to its ends). */

#define BFM_Q24 (1 << 24)
#define BFM_ENV_OFF 64           /* Q24 envelope below this (-108 dB): the stage is over */

typedef struct {
    uint8_t active, gate, note, vel, acc;
    uint8_t kill;                /* stolen: fades over one block, then stops */
    uint8_t fresh;               /* started this block: the matrix amounts are taken, not slewed to */
    int8_t fm_off, fb_off, morph_off;   /* ARP step timbre */
    uint32_t age;
    int32_t pitch, pitch_to;     /* Q12 octaves from MIDI note 0 (glide slews pitch to pitch_to) */
    uint32_t ph[BFM_NOP];
    int32_t y1[BFM_NOP], y2[BFM_NOP];  /* outputs one and two samples back */
    int32_t env[BFM_NOP];        /* Q24 */
    uint8_t stage[BFM_NOP];      /* 0 attack 1 decay 2 sustain 3 release 4 off */
    int32_t amt[BFM_NOP * BFM_NOP];   /* matrix amounts in use, Q12 cycles (slewed) */
    int32_t vout;                /* the voice's last output (CHAOS global feedback) */
    uint32_t pbody;              /* GROWL body oscillator phase (the sub octave) */
    uint32_t nz;                 /* noise state */
    int32_t gain;                /* Q15 smoothed output gain (velocity, sound gain, the kill fade) */
} bfm_voice_t;

static bfm_patch_t bfm_p;        /* the playing patch (the UI writes its bytes; the ISR reads them per block) */
static bfm_settings_t bfm_set;
static bfm_voice_t bv[BFM_MAXV];
static uint32_t bfm_vage;        /* voice clock: the oldest is stolen first */
static int32_t bfm_bend;         /* Q12 octaves */

/* performance state the UI and MIDI write (0..127): CC overrides are stored straight into the patch */
static struct {
    int32_t morph, inten, fb, move;      /* smoothed, Q8 (value << 8) */
    uint32_t lfo_ph, drift_ph;
    int32_t lfo_v, drift_v, drift_to, drift_from;   /* Q15 */
    uint32_t nz;
    int32_t dc_x1;
    int64_t dc_y1;               /* Q16 above the sample */
    int32_t dc2_x1;
    int64_t dc2_y1;              /* the second blocker, after DRIVE / CRUSH */
    int32_t lim_g;               /* Q16 */
    int32_t crush_hold_l, crush_hold_r;
    uint32_t crush_ph;
    uint32_t dly_w;
    int32_t peak;                /* output meter, Q15 */
    int32_t hb_hist[BFM_HB_N + 2 * BFM_CTL];   /* half-band history */
    uint32_t sh_ph;              /* LFO S&H / chaos step */
    int32_t chaos_x;             /* logistic map state, Q16 */
} bm;

#define BFM_DLY_N 16384u         /* per side: 371 ms at 44.1 kHz */
static int16_t bfm_dly[2][BFM_DLY_N] BFM_POOL_SECTION;

/* ---------------------------------------------------------------- math --- */
static inline int32_t bfm_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static inline int32_t bfm_sin(uint32_t ph)       /* Q15, linear interpolation over 1024 points */
{
    uint32_t i = ph >> 22, f = (ph >> 6) & 0xFFFFu;
    int32_t a = BFM_SINE[i], b = BFM_SINE[i + 1u];
    return a + (int32_t)(((b - a) * (int32_t)f) >> 16);
}

/* phase increment (Q32 cycles per sample) of log pitch L (Q12 octaves from MIDI note 0) at the base rate;
 * os 1: half of it (the 2x rate). Clamped below Nyquist */
static uint32_t bfm_inc(int32_t l, uint32_t os)
{
    uint32_t oct, fr, i, f, m;
    if (l < -8 * 4096)
        l = -8 * 4096;
    if (l > 46000)
        l = 46000;                                   /* ~20 kHz */
    l -= (int32_t)os << 12;
    l += 8 << 12;                                    /* biased: oct >= 0 */
    oct = (uint32_t)l >> 12;
    fr = (uint32_t)l & 4095u;
    i = fr >> 4;
    f = fr & 15u;
    m = BFM_EXP2[i] + (((BFM_EXP2[i + 1u] - BFM_EXP2[i]) * f) >> 4);
    {
        uint64_t v = (uint64_t)BFM_INC0 * m;         /* Q30 */
        uint32_t sh = 30u + 8u;
        v = oct >= sh ? v << (oct - sh) : v >> (sh - oct);
        return v > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)v;
    }
}

static inline int32_t bfm_lerp(int32_t a, int32_t b, int32_t m)   /* m Q7 (0..128) */
{
    return a + (((b - a) * m) >> 7);
}

static inline int32_t bfm_tanh(int32_t x)        /* x Q15 (+-1 = +-32768), any range; tanh(4 x) */
{
    int32_t i = x >> 8, f, a, b;
    if (i <= -128)
        return BFM_TANH[0];
    if (i >= 127)
        return BFM_TANH[256];
    f = x & 255;
    a = BFM_TANH[i + 128];
    b = BFM_TANH[i + 129];
    return a + (((b - a) * f) >> 8);
}

/* --------------------------------------------------------------- voices --- */
/* the polyphony: VOICES (2..8, a setting); HQ renders every voice twice as fast, so it allows half */
static uint32_t bfm_maxv(void)
{
    uint32_t v = bfm_set.voices < 1u ? 1u : bfm_set.voices > BFM_MAXV ? BFM_MAXV : bfm_set.voices;
    return bfm_set.quality ? (v + 1u) / 2u : v;
}

static void bfm_voice_start(bfm_voice_t *v, uint32_t note, uint32_t vel, int legato)
{
    uint32_t j;
    const bfm_sound_t *a = &bfm_p.s[0];
    v->pitch_to = (int32_t)((note << 12) / 12u);
    if (!legato || !v->active || !bfm_p.glide)
        v->pitch = v->pitch_to;
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->gate = 1;
    v->kill = 0;
    v->age = ++bfm_vage;
    if (legato && v->active)
        return;                                      /* mono legato: the envelopes go on */
    for (j = 0; j < BFM_NOP; j++) {
        uint32_t phs = a->phase[j];
        if (phs)
            v->ph[j] = (phs - 1u) * (0xFFFFFFFFu / 127u);
        v->stage[j] = 0;
        if (!v->active) {
            v->env[j] = 0;
            v->y1[j] = v->y2[j] = 0;
        }
    }
    if (!v->active) {
        v->fresh = 1;                                /* first block: the matrix as it is, no slew from zero */
        v->vout = 0;
        v->pbody = 0;
        v->gain = 0;
        v->nz = 0x1234567u + note * 977u;
    }
    v->active = 1;
}

static void bfm_voice_release(bfm_voice_t *v)
{
    uint32_t j;
    v->gate = 0;
    for (j = 0; j < BFM_NOP; j++)
        if (v->stage[j] < 3u)
            v->stage[j] = 3;
}

static bfm_voice_t *bfm_voice_alloc(void)
{
    uint32_t i, n = bfm_maxv();
    bfm_voice_t *best = 0;
    for (i = 0; i < n; i++)
        if (!bv[i].active)
            return &bv[i];
    for (i = 0; i < n; i++)                          /* the oldest released, else the oldest */
        if (!bv[i].gate && (!best || bv[i].age < best->age))
            best = &bv[i];
    if (!best)
        for (i = 0; i < n; i++)
            if (!best || bv[i].age < best->age)
                best = &bv[i];
    return best;
}

#ifndef BFM_TRACE_ON
#define BFM_TRACE_ON(note, vel) ((void)0)   /* the host tests watch every note the engine starts */
#endif
#ifndef BFM_TRACE_OFF
#define BFM_TRACE_OFF(note) ((void)0)
#endif
/* note on with the ARP step's timbre (0s for a played key) */
static void bfm_note_on_t(uint32_t note, uint32_t vel, int32_t fm, int32_t fb, int32_t mo, uint32_t acc)
{
    bfm_voice_t *v;
    int legato = 0;
    BFM_TRACE_ON(note, vel);
    if (!bfm_p.poly) {
        v = &bv[0];
        legato = v->active && v->gate;
    } else {
        v = bfm_voice_alloc();                       /* a stolen voice restarts from where its envelopes are */
    }
    v->fm_off = (int8_t)fm;
    v->fb_off = (int8_t)fb;
    v->morph_off = (int8_t)mo;
    v->acc = (uint8_t)acc;
    bfm_voice_start(v, note, vel, legato);
}
static void bfm_note_on(uint32_t note, uint32_t vel) { bfm_note_on_t(note, vel, 0, 0, 0, 0); }

static void bfm_note_off(uint32_t note)
{
    uint32_t i;
    BFM_TRACE_OFF(note);
    for (i = 0; i < BFM_MAXV; i++)
        if (bv[i].active && bv[i].gate && bv[i].note == note)
            bfm_voice_release(&bv[i]);
}

static void bfm_all_off(void)
{
    uint32_t i;
    for (i = 0; i < BFM_MAXV; i++)
        if (bv[i].active)
            bfm_voice_release(&bv[i]);
}

static void bfm_panic(void)
{
    uint32_t i;
    for (i = 0; i < BFM_MAXV; i++)
        bv[i].active = 0;
}

static uint32_t bfm_voices_on(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < BFM_MAXV; i++)
        n += bv[i].active;
    return n;
}

/* --------------------------------------------------------- the patch --- */
/* what the macros and modulation leave for one block (global part) */
typedef struct {
    int32_t morph, inten, fb;    /* 0..127 (after LFOs), before the step offsets */
    int32_t fold_mod;            /* -127..127 */
    int32_t pitch_mod;           /* Q12 octaves */
    uint32_t os;                 /* 0: 1x, 1: 2x */
} bfm_glob_t;

static int32_t bfm_bip(uint32_t v) { return (int32_t)v - 64; }

/* the LFO (shape, rate) and the drift (a smoothed random walk / chaos), once a block */
static void bfm_lfos(void)
{
    uint32_t sh = bfm_p.lfo_shape % LFO_N, prev = bm.lfo_ph;
    int32_t v;
    bm.lfo_ph += BFM_LFO_INC[bfm_p.lfo_rate & 127u];
    switch (sh) {
    case LFO_SINE: v = bfm_sin(bm.lfo_ph); break;
    case LFO_TRI: v = (int32_t)(bm.lfo_ph >> 15) - 65536; v = (v < 0 ? -v : v) - 32768; break;
    case LFO_SAW: v = (int32_t)(bm.lfo_ph >> 16) - 32768; break;
    case LFO_SQR: v = bm.lfo_ph < 0x80000000u ? 32767 : -32767; break;
    default:
        if (bm.lfo_ph < prev) {                          /* a new cycle: a new value */
            if (sh == LFO_SH) {
                bm.nz = bm.nz * 1664525u + 1013904223u;
                bm.lfo_v = (int32_t)(bm.nz >> 16) - 32768;
            } else {                                     /* logistic map x' = r x (1 - x), r ~ 3.99 */
                int32_t x = bm.chaos_x ? bm.chaos_x : 21000;
                x = (int32_t)(((int64_t)261488 * x >> 16) * (65536 - x) >> 16);
                bm.chaos_x = bfm_clamp(x, 1, 65535);
                bm.lfo_v = bm.chaos_x - 32768;
            }
        }
        v = bm.lfo_v;
        break;
    }
    if (sh < LFO_SH)
        bm.lfo_v = v;
    /* drift: towards a new random target each cycle, smoothly (cosine-free: a linear blend of the step) */
    prev = bm.drift_ph;
    bm.drift_ph += BFM_LFO_INC[bfm_p.drift_rate & 127u];
    if (bm.drift_ph < prev) {
        bm.drift_from = bm.drift_to;
        bm.nz = bm.nz * 1664525u + 1013904223u;
        bm.drift_to = (int32_t)(bm.nz >> 16) - 32768;
    }
    bm.drift_v = bm.drift_from + (int32_t)(((int64_t)(bm.drift_to - bm.drift_from) * (bm.drift_ph >> 16)) >> 16);
}

/* one-pole smoothing of a 0..127 macro, Q8 */
static int32_t bfm_smooth(int32_t *s, uint32_t target)
{
    int32_t t = (int32_t)target << 8;
    *s += (t - *s) / 8;                                 /* ~6 ms at the block rate */
    if (*s - t < 8 && t - *s < 8)
        *s = t;
    return *s;
}

static void bfm_glob(bfm_glob_t *g)
{
    int32_t mv, lfo, dr;
    bfm_lfos();
    mv = bfm_smooth(&bm.move, bfm_p.movement) >> 8;    /* 0..127: the depth of all motion (64 = as set) */
    lfo = (int32_t)((int64_t)bm.lfo_v * mv >> 6);      /* Q15 x (0..2) */
    dr = (int32_t)((int64_t)bm.drift_v * mv >> 6);
    g->morph = (bfm_smooth(&bm.morph, bfm_p.morph) >> 8) + ((dr * bfm_bip(bfm_p.mod_morph)) >> 15);
    g->inten = (bfm_smooth(&bm.inten, bfm_p.intensity) >> 8) + ((lfo * bfm_bip(bfm_p.mod_int)) >> 15);
    g->fb = (bfm_smooth(&bm.fb, bfm_p.feedback) >> 8) + ((dr * bfm_bip(bfm_p.mod_fb)) >> 15);
    g->fold_mod = (lfo * bfm_bip(bfm_p.mod_fold)) >> 14;
    g->pitch_mod = ((lfo * bfm_bip(bfm_p.mod_pitch)) >> 15) * 4;   /* up to ~+-1 semitone x MOVEMENT */
    g->pitch_mod += bfm_bend;
    g->os = bfm_set.quality ? 1u : 0u;
}

/* a macro (0..127, may be out of range after offsets) -> scale Q12 (64 = 1.0, 127 ~ 3.9, 0 = 0) */
static int32_t bfm_macro_scale(int32_t v)
{
    v = bfm_clamp(v, 0, 127);
    return v * v;
}

/* ------------------------------------------------------------- render --- */
/* the per-sample work of one voice, with everything the block worked out in front of it */
typedef struct {
    int32_t cur[BFM_NOP], y1[BFM_NOP], fb[BFM_NOP];   /* this sample, the last, the last two averaged */
    const int32_t *src[BFM_NOP][BFM_NOP];            /* the edges into each operator: what they read .. */
    int32_t amt[BFM_NOP][BFM_NOP];                   /* .. and how much (Q8 cycles) */
    uint8_t bend[BFM_NOP][BFM_NOP];                  /* a delayed path (CHAOS bends these) */
    uint8_t ne[BFM_NOP];
    uint32_t ph[BFM_NOP], inc[BFM_NOP];
    int32_t amp[BFM_NOP], damp[BFM_NOP], outg[BFM_NOP];
    int32_t fold_g, sym_n, bias, fmix, body, gain, dgain;
    uint32_t binc, pbody;
    int32_t nzamt, crush_mask, chaos_g, gfb, vout;
    uint32_t nz;
} bfm_kern_t;

#define BFM_OP(K, k, X)                                                                                      \
    do {                                                                                                     \
        uint32_t pm_ = 0, q_;                                                                                \
        int32_t y_;                                                                                          \
        for (q_ = 0; q_ < (K)->ne[k]; q_++) {                                                                \
            int32_t v_ = *(K)->src[k][q_];                                                                   \
            if ((X) && (K)->chaos_g && (K)->bend[k][q_])                                                     \
                v_ = bfm_sin((uint32_t)(v_ * (K)->chaos_g) << 7);                                            \
            pm_ += (uint32_t)(v_ * (K)->amt[k][q_]);                                                         \
        }                                                                                                    \
        if (X)                                                                                               \
            pm_ += (uint32_t)(nzv * (K)->nzamt) + (uint32_t)((K)->vout * (K)->gfb);                          \
        (K)->ph[k] += (K)->inc[k];                                                                           \
        y_ = (bfm_sin((K)->ph[k] + (pm_ << 9)) * (K)->amp[k]) >> 15;                                         \
        if (X)                                                                                               \
            y_ = y_ < 0 ? -((-y_) & (K)->crush_mask) : y_ & (K)->crush_mask;   /* symmetric: no DC */        \
        (K)->cur[k] = y_;                                                                                    \
        (K)->amp[k] += (K)->damp[k];  /* (linear between two levels >= 0: never below 0) */                  \
    } while (0)

static inline __attribute__((always_inline)) void bfm_kernel_body(bfm_kern_t *K, int32_t *acc, uint32_t n,
                                                                   const int X)
{
    uint32_t s, k;
    for (s = 0; s < n; s++) {
        int32_t out, nzv = 0;
        if (X && K->nzamt) {
            K->nz = K->nz * 1664525u + 1013904223u;
            nzv = (int32_t)(K->nz >> 16) - 32768;
        }
        BFM_OP(K, 3, X);
        BFM_OP(K, 2, X);
        BFM_OP(K, 1, X);
        BFM_OP(K, 0, X);
        out = ((K->cur[0] * K->outg[0]) >> 15) + ((K->cur[1] * K->outg[1]) >> 15) +
              ((K->cur[2] * K->outg[2]) >> 15) + ((K->cur[3] * K->outg[3]) >> 15);
        for (k = 0; k < BFM_NOP; k++) {
            K->fb[k] = (K->cur[k] + K->y1[k]) >> 1;
            K->y1[k] = K->cur[k];
        }
        if (X)
            K->vout = out >> 2;
        if (K->fmix) {                                    /* the sine folder */
            int32_t u = ((out * K->fold_g) >> 8) + K->bias, y;
            if (u < 0)
                u = (u * K->sym_n) >> 8;
            y = bfm_sin((uint32_t)u << 15);
            out += ((y - out) * (K->fmix >> 3)) >> 12;   /* (|y - out| < 2^18: 32-bit) */
        }
        if (K->body) {                                    /* GROWL: the sub octave and the fundamental */
            int32_t b;
            K->pbody += K->binc;
            b = (bfm_sin(K->pbody) + bfm_sin(K->pbody << 1)) >> 1;
            out += (((b * (K->body >> 7)) >> 8) * K->amp[0]) >> 15;
        }
        acc[s] += (out * (K->gain >> 5)) >> 10;          /* (|out| < 2^18, gain < 2^17: 32-bit) */
        K->gain += K->dgain;
    }
}
static __attribute__((noinline)) void bfm_kernel(bfm_kern_t *K, int32_t *acc, uint32_t n)
{
    bfm_kernel_body(K, acc, n, 0);
}
static __attribute__((noinline)) void bfm_kernel_x(bfm_kern_t *K, int32_t *acc, uint32_t n)
{
    bfm_kernel_body(K, acc, n, 1);
}

/* one voice over n samples at the rate 1 << os, added into acc[] (Q15) */
static void bfm_voice_render(bfm_voice_t *v, const bfm_glob_t *g, int32_t *acc, uint32_t n)
{
    const bfm_sound_t *A = &bfm_p.s[0], *B = &bfm_p.s[1];
    uint32_t mode = bfm_p.mode % MODE_N, j, k, os = g->os;
    int32_t m = bfm_clamp(g->morph + v->morph_off, 0, 127), mq;
    int32_t si = bfm_macro_scale(g->inten + v->fm_off + (v->acc ? 16 : 0) +
                                 (((int32_t)v->vel - 100) * (int32_t)bfm_p.vel_sens >> 8));
    int32_t sf = bfm_macro_scale(g->fb + v->fb_off);
    uint32_t inc[BFM_NOP];
    int32_t amp0[BFM_NOP], damp[BFM_NOP], outg[BFM_NOP];
    bfm_kern_t K;
    int32_t chr, fold_g, sym_n, bias, fmix, gain_to, dgain, body = 0, nzamt = 0, crush_mask = -1, gfb = 0;
    int32_t chaos_g = 0, eoff = 0;
    uint32_t binc = 0;
    mq = m * 128 / 127;                                  /* Q7 */
    chr = bfm_lerp(A->character, B->character, mq);
    /* glide */
    if (v->pitch != v->pitch_to) {
        int32_t d = v->pitch_to - v->pitch, st = bfm_p.glide ? 4096 / (1 + (int32_t)bfm_p.glide * 4) + 1 : 1 << 20;
        v->pitch += d > st ? st : d < -st ? -st : d;
    }
    /* operators: pitch, envelope over the block, carrier gain */
    for (j = 0; j < BFM_NOP; j++) {
        int32_t la, lb, l, e = v->env[j], e1, lvl, sus;
        la = A->fixed[j] ? BFM_FIXED_LOG[A->fixed[j]] : v->pitch + BFM_RATIO_LOG[A->ratio[j] % BFM_NRATIO];
        lb = B->fixed[j] ? BFM_FIXED_LOG[B->fixed[j]] : v->pitch + BFM_RATIO_LOG[B->ratio[j] % BFM_NRATIO];
        la += ((int32_t)A->fine[j] - 64) * 4096 / 600;   /* 2 cents a step */
        lb += ((int32_t)B->fine[j] - 64) * 4096 / 600;
        l = bfm_lerp(la, lb, mq) + g->pitch_mod;
        if (mode == MODE_METAL)
            l += (int32_t)j * chr * 6;                   /* inharmonic spread: up to ~0.56 octave on OP4 */
        inc[j] = bfm_inc(l, os);
        /* envelope: block-rate segments, linear inside the block */
        sus = bfm_lerp(BFM_LEVEL[A->sus[j] & 127u], BFM_LEVEL[B->sus[j] & 127u], mq) << 9;   /* Q24 */
        switch (v->stage[j]) {
        case 0:
            e1 = e + (int32_t)(BFM_ENV_ATK[bfm_lerp(A->atk[j] & 127u, B->atk[j] & 127u, mq)] * BFM_CTL);
            if (e1 >= BFM_Q24) {
                e1 = BFM_Q24;
                v->stage[j] = 1;
            }
            break;
        case 1:
        case 2:
            e1 = sus + (int32_t)(((int64_t)(e - sus) * BFM_ENV_DEC[bfm_lerp(A->dec[j] & 127u, B->dec[j] & 127u, mq)]) >> 16);
            break;
        case 3:
            e1 = (int32_t)(((int64_t)e * BFM_ENV_DEC[bfm_lerp(A->rel[j] & 127u, B->rel[j] & 127u, mq)]) >> 16);
            if (e1 < BFM_ENV_OFF) {
                e1 = 0;
                v->stage[j] = 4;
            }
            break;
        default:
            e1 = 0;
            break;
        }
        v->env[j] = e1;
        lvl = bfm_lerp(BFM_LEVEL[A->level[j] & 127u], BFM_LEVEL[B->level[j] & 127u], mq);   /* Q15 */
        amp0[j] = (int32_t)(((int64_t)e * lvl) >> 24);   /* Q15 */
        damp[j] = ((int32_t)(((int64_t)e1 * lvl) >> 24) - amp0[j]) / (int32_t)(n);
        outg[j] = bfm_lerp(BFM_LEVEL[A->out[j] & 127u], BFM_LEVEL[B->out[j] & 127u], mq);
        if (outg[j] && v->stage[j] < 4u)
            eoff = 1;                                    /* a carrier still sounds */
    }
    /* the matrix: target amounts (Q12 cycles) x the macro, slewed, then edge lists per destination. Each edge
     * points at the value it reads: this sample's output (from a higher operator), the last sample's (from a
     * lower one), or the last two averaged (self feedback), so the sample loop has no branches for it */
    for (j = 0; j < BFM_NOP; j++)
        K.ne[j] = 0;
    for (k = 0; k < BFM_NOP; k++)                        /* src */
        for (j = 0; j < BFM_NOP; j++) {                  /* dst */
            uint32_t e = k * BFM_NOP + j;
            int32_t ta = BFM_MODAMT[A->mat[e] & 127u], tb = BFM_MODAMT[B->mat[e] & 127u], t;
            if (A->neg[e >> 3] & (1u << (e & 7u)))
                ta = -ta;                                /* polarity: a negative amount (A and B may differ: */
            if (B->neg[e >> 3] & (1u << (e & 7u)))
                tb = -tb;                                /* MORPH then passes through zero, no jump) */
            t = bfm_lerp(ta, tb, mq);
            t = (int32_t)(((int64_t)t * (k > j ? si : sf)) >> 12);
            if (mode == MODE_CHAOS && k <= j)
                t += t >> 1;                             /* CHAOS: cross paths 1.5x */
            t = bfm_clamp(t, -65535, 65535);
            if (!v->fresh)
                v->amt[e] += (t - v->amt[e]) / 4;
            else
                v->amt[e] = t;
            if (v->amt[e] >= 16 || v->amt[e] <= -16) {   /* below 1/256 cycle: nothing */
                uint32_t q = K.ne[j]++;
                K.src[j][q] = k > j ? &K.cur[k] : k < j ? &K.y1[k] : &K.fb[k];
                K.bend[j][q] = (uint8_t)(k <= j);
                K.amt[j][q] = v->amt[e] / 16;            /* Q8 cycles: x Q15 fits int32 with 4 edges */
            }
        }
    v->fresh = 0;
    /* folder and mode extras */
    {   /* the folder's gain, less the higher the note above A4 (to 20 % five octaves up): a fold multiplies the
         * partials, and high notes would alias (after Felucca's PHASE FOLD) */
        int32_t f = bfm_clamp(bfm_lerp(A->fold, B->fold, mq) + g->fold_mod, 0, 127), up = (int32_t)v->note - 69;
        if (up > 0)
            f = f * bfm_clamp(60 - up, 12, 60) / 60;
        fold_g = 256 + f * 14;                           /* Q8: 1 .. 8 */
    }
    {
        int32_t sy = bfm_lerp(A->sym, B->sym, mq);       /* 64: even; 0 / 127: one side squashed or boosted */
        sym_n = 256 + (64 - sy) * 3;                     /* the negative half's gain, Q8 */
    }
    bias = (bfm_lerp(A->bias, B->bias, mq) - 64) * 300;  /* Q15 */
    fmix = bfm_lerp(A->fmix, B->fmix, mq) * 258;         /* Q15 */
    if (!bfm_lerp(A->fold, B->fold, mq) && g->fold_mod <= 0 && bias == 0)
        fmix = 0;                                        /* a folder at rest costs nothing */
    if (mode == MODE_GROWL) {
        body = chr * 200;                                /* Q15 */
        binc = bfm_inc(v->pitch + g->pitch_mod - 4096, os);   /* the sub octave; its double is the fundamental */
    } else if (mode == MODE_NOISE) {
        nzamt = chr * 12;                                /* Q8 cycles x Q15 noise */
        crush_mask = (int32_t)(0xFFFFFFFFu << (chr / 12));   /* 0 .. 10 bits off */
    } else if (mode == MODE_CHAOS) {
        chaos_g = 256 + chr * 6;                         /* Q8 gain into the bend */
        gfb = chr * 10;                                  /* Q8 cycles of the voice output into every phase */
    }
    /* the output gain: velocity, the sound's gain, the steal fade */
    {
        int32_t vs = bfm_p.vel_sens, vg = 32768 - (((127 - (int32_t)v->vel) * vs) << 1);
        int32_t sg = BFM_GAIN[bfm_lerp(A->gain & 127u, B->gain & 127u, mq)];   /* Q12, 100 = 0 dB */
        gain_to = (bfm_clamp(vg, 0, 32768) * sg) >> 12;  /* Q15, up to ~3.2 (+10 dB) */
        if (v->kill)
            gain_to = 0;
        dgain = (gain_to - v->gain) / (int32_t)n;
    }
    for (k = 0; k < BFM_NOP; k++) {
        K.y1[k] = v->y1[k];
        K.fb[k] = (v->y1[k] + v->y2[k]) >> 1;
        K.ph[k] = v->ph[k];
        K.inc[k] = inc[k];
        K.amp[k] = amp0[k];
        K.damp[k] = damp[k];
        K.outg[k] = outg[k];
    }
    K.fold_g = fold_g; K.sym_n = sym_n; K.bias = bias; K.fmix = fmix;
    K.body = body; K.binc = binc; K.pbody = v->pbody;
    K.nzamt = nzamt; K.crush_mask = crush_mask; K.chaos_g = chaos_g; K.gfb = gfb; K.nz = v->nz; K.vout = v->vout;
    K.gain = v->gain; K.dgain = dgain;
    if (nzamt || chaos_g || gfb || crush_mask != -1)
        bfm_kernel_x(&K, acc, n);                        /* CHAOS / NOISE extras */
    else
        bfm_kernel(&K, acc, n);
    for (k = 0; k < BFM_NOP; k++) {
        v->y2[k] = 2 * K.fb[k] - K.y1[k];                /* (fb = (y1 + y2) / 2, rounded down: within 1 LSB) */
        v->y1[k] = K.y1[k];
        v->ph[k] = K.ph[k];
    }
    v->pbody = K.pbody;
    v->nz = K.nz;
    v->vout = K.vout;
    v->gain = gain_to;
    if (v->kill || (!eoff && !v->gate)) {
        v->active = 0;                                   /* (a stolen voice was faded over this block) */
        v->kill = 0;
    }
}

/* half-band 2x -> 1x: x[2n] -> y[n]. A half-band FIR is symmetric and every other tap (but the centre) is zero,
 * so an output costs (taps + 1) / 4 multiplies: pairs of samples around the centre share a coefficient */
#define BFM_HB_C ((BFM_HB_N - 1u) / 2u)          /* the centre tap */
static void bfm_decimate(const int32_t *x, int32_t *y, uint32_t n)
{
    int32_t *h = bm.hb_hist;
    uint32_t i, k, nx = 2u * n;
    for (i = 0; i < nx; i++)
        h[BFM_HB_N - 1u + i] = x[i];
    for (i = 0; i < n; i++) {
        const int32_t *p = &h[2u * i + 1u];
        int64_t a = (int64_t)p[BFM_HB_C] * BFM_HB[BFM_HB_C];
        for (k = 1; k <= BFM_HB_C; k += 2u)
            a += (int64_t)(p[BFM_HB_C - k] + p[BFM_HB_C + k]) * BFM_HB[BFM_HB_C + k];
        y[i] = (int32_t)(a >> 16);
    }
    for (i = 0; i < BFM_HB_N - 1u; i++)
        h[i] = h[nx + i];
}

/* ------------------------------------------------------------- master --- */
static uint32_t bfm_master_q15 = 32767;    /* the MASTER pot */
static uint8_t bfm_lim_off;                /* host tests only: no limiter, no soft knee (to measure raw levels) */

static void bfm_master(const int32_t *mono, int32_t *out, uint32_t n)
{
    uint32_t i;
    int32_t drive = bfm_p.drive, crush = bfm_p.crush;
    int32_t dg = 32 + drive * 2;                          /* tanh(4 x dg / 32): 1x .. 9x into the curve */
    uint32_t dly = bfm_p.dly_mix ? 64u + (uint32_t)bfm_p.dly_time * 127u : 0u;
    int32_t dfb = bfm_p.dly_fb * 230, dmix = bfm_p.dly_mix * 258;
    uint32_t crush_step = crush ? (1u << 16) / (1u + (uint32_t)crush / 6u) : (1u << 16);
    int32_t cmask = crush ? (int32_t)(0xFFFFFFFFu << (crush / 13)) : -1;
    for (i = 0; i < n; i++) {
        int32_t x = mono[i], l, r, a, p;
        /* DC blocker, ~7 Hz; the state keeps 16 more bits (a floored Q15 feedback would stick at an offset) */
        int64_t y = ((int64_t)(x - bm.dc_x1) * 65536) + ((bm.dc_y1 * 32702) >> 15);
        bm.dc_x1 = x;
        if (y > ((int64_t)8 * 32768 << 16))
            y = (int64_t)8 * 32768 << 16;
        if (y < -((int64_t)8 * 32768 << 16))
            y = -((int64_t)8 * 32768 << 16);
        bm.dc_y1 = y;
        x = (int32_t)(y >> 16);
        if (drive) {
            int32_t t = bfm_tanh((int32_t)(((int64_t)x * dg) >> 7));   /* tanh(4 * x * dg / 32 / 4) */
            x = t;
        }
        if (crush) {
            bm.crush_ph += crush_step;
            if (bm.crush_ph >= (1u << 16)) {
                bm.crush_ph -= (1u << 16);
                bm.crush_hold_l = x < 0 ? -((-x) & cmask) : x & cmask;   /* symmetric: zero stays zero */
            }
            x = bm.crush_hold_l;
        }
        if (drive || crush) {                             /* saturation of an uneven wave makes DC: block it again */
            int64_t y2 = ((int64_t)(x - bm.dc2_x1) * 65536) + ((bm.dc2_y1 * 32702) >> 15);
            bm.dc2_x1 = x;
            bm.dc2_y1 = y2;
            x = (int32_t)(y2 >> 16);
        }
        l = r = x;
        if (dly) {                                        /* ping-pong: L feeds R, R feeds L */
            uint32_t rd = (bm.dly_w - dly) & (BFM_DLY_N - 1u), wr = bm.dly_w & (BFM_DLY_N - 1u);
            int32_t dl = bfm_dly[0][rd], dr = bfm_dly[1][rd];
            bfm_dly[0][wr] = (int16_t)bfm_clamp((x >> 1) + ((dr * dfb) >> 15), -32767, 32767);
            bfm_dly[1][wr] = (int16_t)bfm_clamp(((dl * dfb) >> 15), -32767, 32767);
            bm.dly_w++;
            l += (dl * dmix) >> 15;
            r += (dr * dmix) >> 15;
        }
        /* peak limiter (instant attack, ~50 ms release), then a soft knee */
        a = l < 0 ? -l : l;
        p = r < 0 ? -r : r;
        if (p > a)
            a = p;
        p = (int32_t)(((int64_t)a * bm.lim_g) >> 16);
        if (bfm_lim_off)
            p = 0;
        if (p > 29000)
            bm.lim_g = (int32_t)((29000u << 16) / (uint32_t)a);   /* (a > 29000 here: 32-bit) */
        else
            bm.lim_g += (65536 - bm.lim_g) >> 11;
        l = (int32_t)(((int64_t)l * bm.lim_g) >> 16);
        r = (int32_t)(((int64_t)r * bm.lim_g) >> 16);
        if (!bfm_lim_off && (l > 24576 || l < -24576)) {
            int32_t s = l < 0 ? -1 : 1, e = (l < 0 ? -l : l) - 24576;
            l = s * (24576 + ((8191 * bfm_tanh(e)) >> 15));
        }
        if (!bfm_lim_off && (r > 24576 || r < -24576)) {
            int32_t s = r < 0 ? -1 : 1, e = (r < 0 ? -r : r) - 24576;
            r = s * (24576 + ((8191 * bfm_tanh(e)) >> 15));
        }
        l = (int32_t)(((int64_t)l * bfm_master_q15) >> 15);
        r = (int32_t)(((int64_t)r * bfm_master_q15) >> 15);
        out[2u * i] = bfm_lim_off ? l : bfm_clamp(l, -32767, 32767);
        out[2u * i + 1u] = bfm_lim_off ? r : bfm_clamp(r, -32767, 32767);
        a = out[2u * i] < 0 ? -out[2u * i] : out[2u * i];
        if (a > bm.peak)
            bm.peak = a;
    }
}

static void bfm_dsp_init(void)
{
    memset(bv, 0, sizeof bv);
    memset(&bm, 0, sizeof bm);
    memset(bfm_dly, 0, sizeof bfm_dly);
    bm.lim_g = 65536;
    bm.nz = 0xBADC0DEu;
    bm.chaos_x = 21000;
}

/* render n (<= BFM_CTL) stereo samples, Q15: the voices, then the master */
static void bfm_render_voices(int32_t *out, uint32_t n)
{
    int32_t acc[2 * BFM_CTL], mono[BFM_CTL];
    bfm_glob_t g;
    uint32_t i, nn;
    bfm_glob(&g);
    nn = n << g.os;
    for (i = 0; i < nn; i++)
        acc[i] = 0;
    for (i = 0; i < BFM_MAXV; i++)
        if (bv[i].active) {
            if (i >= bfm_maxv())
                bv[i].kill = 1;                          /* fewer VOICES or HQ: the extra ones fade out */
            bfm_voice_render(&bv[i], &g, acc, nn);
        }
    if (g.os)
        bfm_decimate(acc, mono, n);
    else
        for (i = 0; i < n; i++)
            mono[i] = acc[i];
    bfm_master(mono, out, n);
}
