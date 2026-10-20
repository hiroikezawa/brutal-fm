/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Hiro Ikezawa */
/* COMPLEX ARP: the pattern generator. Pure and deterministic: the same pool, settings, seed, locks and previous
 * pattern give the same pattern, byte for byte, on the device, in the browser and in the host tests. Nothing
 * here runs during playback: the generator writes every step's data once, and playback (bfm_seq.c) only reads
 * it, so a loop sounds the same every time round.
 *
 * The one hard rule: a step's note is always a member of the pool (the keys that were held). Nothing below
 * computes a note: every note is pool[idx] with idx < npool, and bfm_pat_valid() checks it.
 *
 * Musical shape, in three independent layers (each kept by its LOCK):
 *   RHYTHM  a short motif of hits on a metric grid (strong beats weighted), repeated over the pattern with
 *           small variations (a hit moved, dropped or added) as COMPLEXITY rises; DENSITY sets the share of
 *           hits; runs of hits are broken by rests; gates (GATE VARIATION around the base gate), ties over
 *           rests (long notes), accents (strong beats, the motif's head)
 *   PITCH   a motif of pool indices (steps between neighbours at low COMPLEXITY, leaps at high), repeated
 *           with transformations that stay inside the pool (shift by k pool places, reverse, mirror, rotate),
 *           then spread so every held note is heard when there are hits enough
 *   TIMBRE  per-step FM / FEEDBACK / MORPH offsets from one contour (ramp, alternation, motif, random) scaled
 *           by TIMBRE VARIATION; accents push FM up
 * FREEZE does not generate at all: bfm_pat_remap() lays a new pool onto the frozen structure by index. */

/* splitmix32: one 32-bit state, good mixing; the seed IS the state */
static uint32_t ar_next(uint32_t *s)
{
    uint32_t z = (*s += 0x9E3779B9u);
    z = (z ^ (z >> 16)) * 0x85EBCA6Bu;
    z = (z ^ (z >> 13)) * 0xC2B2AE35u;
    return z ^ (z >> 16);
}
static uint32_t ar_below(uint32_t *s, uint32_t n) { return n ? (uint32_t)(((uint64_t)ar_next(s) * n) >> 32) : 0u; }
static int ar_chance(uint32_t *s, uint32_t p, uint32_t of) { return ar_below(s, of) < p; }   /* p in `of` */

/* the next seed of a chain (REGENERATE): a fresh, but reproducible, starting point */
static uint32_t bfm_seed_next(uint32_t seed)
{
    uint32_t s = seed ^ 0xA5A5F00Du;
    uint32_t v = ar_next(&s);
    return v ? v : 1u;
}

/* metric weight of a step (16: a bar of 16ths; 8: half a bar) */
static uint32_t ar_weight(uint32_t i, uint32_t len)
{
    if (i == 0u)
        return 10u;
    if (i % 8u == 0u)
        return 7u;
    if (i % 4u == 0u)
        return 5u;
    if (i % 2u == 0u)
        return 3u;
    (void)len;
    return 1u;
}

typedef struct {
    const uint8_t *pool;
    uint32_t n, len, dens, cplx, gvar, tvar, gate;
    uint32_t s;                  /* rng state */
} ar_ctx_t;

/* ---------------------------------------------------------------- rhythm --- */
static uint32_t ar_motif_len(const ar_ctx_t *c)
{
    uint32_t m = c->cplx < 43u ? c->len / 4u : c->cplx < 86u ? c->len / 2u : c->len;
    return m < 4u ? 4u : m;
}

/* hits of one motif: the h best scores; the downbeat first unless COMPLEXITY is very high */
static void ar_motif_rhythm(ar_ctx_t *c, uint32_t m, uint8_t *on)
{
    uint32_t score[BFM_MAXSTEP], i, k, h;
    uint32_t dens = 26u + c->dens * 101u / 127u;          /* 26..127 of 127: never empty, never all */
    h = (m * dens + 63u) / 127u;
    if (h < 1u)
        h = 1u;
    if (h > m)
        h = m;
    for (i = 0; i < m; i++) {
        on[i] = 0;
        score[i] = ar_weight(i, c->len) * (128u - c->cplx) * 8u + ar_below(&c->s, 64u + c->cplx * 16u);
    }
    score[0] = 0xFFFFFFFFu;                                 /* the motif starts on its downbeat (and a key
                                                             * that starts the ARP is heard at once) */
    for (k = 0; k < h; k++) {
        uint32_t best = m;
        for (i = 0; i < m; i++)
            if (!on[i] && (best == m || score[i] > score[best]))
                best = i;
        on[best] = 1;
    }
}

static void ar_rhythm(ar_ctx_t *c, bfm_pattern_t *p)
{
    uint8_t mot[BFM_MAXSTEP], on[BFM_MAXSTEP];
    uint32_t m = ar_motif_len(c), i, r, reps = c->len / m, run, maxrun, nhit;
    ar_motif_rhythm(c, m, mot);
    for (r = 0; r < reps; r++) {
        for (i = 0; i < m; i++)
            on[r * m + i] = mot[i];
        if (r && ar_chance(&c->s, c->cplx, 180u)) {          /* a variation of this repeat */
            uint32_t a = r * m + 1u + ar_below(&c->s, m - 1u), kind = ar_below(&c->s, 3u);
            if (kind == 0u && on[a] && a + 1u < (r + 1u) * m && !on[a + 1u]) {
                on[a] = 0;
                on[a + 1u] = 1;                             /* moved late: a syncopation */
            } else if (kind == 1u && on[a]) {
                on[a] = 0;                                  /* dropped (the repeat's head stays) */
            } else if (!on[a]) {
                on[a] = 1;                                  /* an extra hit */
            }
        }
    }
    /* runs of hits longer than DENSITY allows are broken at their weakest step */
    maxrun = 2u + c->dens / 32u;
    for (i = 0, run = 0; i < c->len; i++) {
        run = on[i] ? run + 1u : 0u;
        if (run > maxrun) {
            uint32_t j, weakest = i;
            for (j = i + 1u - run; j <= i; j++)
                if (j && ar_weight(j, c->len) < ar_weight(weakest, c->len))
                    weakest = j;
            on[weakest] = 0;
            run = 0;
            i = weakest;
        }
    }
    for (i = 0, nhit = 0; i < c->len; i++)
        nhit += on[i];
    if (!nhit)
        on[0] = 1;
    /* gates, ties, accents */
    for (i = 0; i < c->len; i++) {
        bfm_step_t *st = &p->st[i];
        int32_t g = (int32_t)c->gate, span;
        st->flags = on[i] ? ST_ON : 0u;
        st->gate = 0;
        if (!on[i])
            continue;
        span = (int32_t)(c->gvar * 90u / 127u);
        if (span)
            g += (int32_t)ar_below(&c->s, (uint32_t)span * 2u + 1u) - span;
        if (on[i] && ar_weight(i, c->len) >= 5u)
            g += (int32_t)(c->gvar / 8u);                   /* strong beats a little longer */
        st->gate = (uint8_t)(g < 5 ? 5 : g > 100 ? 100 : g);
        if (i + 1u < c->len && !on[i + 1u] && st->gate >= 90u && ar_chance(&c->s, 40u + c->gvar, 254u))
            st->flags |= ST_TIE;                            /* a long note through the rests behind it */
        if ((i % m == 0u && ar_chance(&c->s, 3u, 4u)) ||
            (ar_weight(i, c->len) >= 5u && ar_chance(&c->s, 20u + c->cplx / 2u, 254u)))
            st->flags |= ST_ACC;
    }
}

/* -------------------------------------------------------------- pitches --- */
static uint32_t ar_transform(ar_ctx_t *c, uint32_t kind, uint32_t k, uint32_t idx, uint32_t pos, uint32_t cnt)
{
    uint32_t n = c->n;
    (void)pos;
    (void)cnt;
    switch (kind) {
    case 1: return (idx + k) % n;                            /* shifted k places along the pool */
    case 2: return n - 1u - idx;                             /* mirrored */
    default: return idx;
    }
}

/* a motif of `h` pool indices: steps at low COMPLEXITY, leaps at high, a few repeats */
static void ar_motif_pitch(ar_ctx_t *c, uint32_t h, uint8_t *mo)
{
    uint32_t i, n = c->n, cur = c->cplx < 40u ? 0u : ar_below(&c->s, n);
    for (i = 0; i < h; i++) {
        mo[i] = (uint8_t)cur;
        if (n == 1u)
            continue;
        if (ar_chance(&c->s, (127u - c->cplx) / 3u, 254u))
            continue;                                       /* the same note again */
        if (ar_chance(&c->s, 140u - c->cplx, 160u)) {         /* a step to a neighbour (bouncing at the ends) */
            if (cur == 0u)
                cur = 1u;
            else if (cur == n - 1u)
                cur = n - 2u;
            else
                cur = ar_chance(&c->s, 1u, 2u) ? cur + 1u : cur - 1u;
        } else {
            uint32_t j = ar_below(&c->s, n - 1u);           /* a leap anywhere else */
            cur = j >= cur ? j + 1u : j;
        }
    }
}

static void ar_pitch(ar_ctx_t *c, bfm_pattern_t *p, const uint8_t *keep, uint32_t nkeep)
{
    uint8_t hit[BFM_MAXSTEP], seq[BFM_MAXSTEP], mo[BFM_MAXSTEP], used[BFM_POOL];
    uint32_t i, nh = 0, m = ar_motif_len(c), hm = 0, r;
    for (i = 0; i < c->len; i++)
        if (p->st[i].flags & ST_ON)
            hit[nh++] = (uint8_t)i;
    if (nkeep) {                                            /* PITCH LOCK: the old order, over the new hits */
        for (i = 0; i < nh; i++)
            seq[i] = (uint8_t)(keep[i % nkeep] % c->n);
    } else {
        uint32_t reps = c->len / m, kind = 0, k = 0, h0 = 0;
        for (i = 0; i < nh; i++)
            hm += hit[i] < m;
        if (!hm)
            hm = 1;
        ar_motif_pitch(c, hm, mo);
        for (r = 0, i = 0; r < reps; r++) {
            uint32_t q = 0;
            if (r) {                                        /* how this repeat answers the motif */
                kind = ar_chance(&c->s, 64u + c->cplx, 254u) ? 1u + ar_below(&c->s, 3u) : 0u;
                k = 1u + ar_below(&c->s, c->n > 1u ? c->n - 1u : 1u);
            }
            for (; i < nh && hit[i] < (r + 1u) * m; i++, q++) {
                uint32_t src = kind == 3u ? (q + k) % hm : q % hm;   /* 3: rotated */
                seq[i] = (uint8_t)ar_transform(c, kind == 3u ? 0u : kind, k, mo[src], q, hm);
            }
            (void)h0;
        }
        /* no more than 3 of the same note in a row (a pool of one is all repeats) */
        for (i = 3; i < nh && c->n > 1u; i++)
            if (seq[i] == seq[i - 1u] && seq[i] == seq[i - 2u] && seq[i] == seq[i - 3u])
                seq[i] = (uint8_t)((seq[i] + 1u + ar_below(&c->s, c->n - 1u)) % c->n);
        /* spread: every held note is heard when there are enough hits; a missing one replaces a repeat, on
         * the weakest step that holds a note used more than once */
        for (;;) {
            uint32_t miss = c->n, best = nh, w;
            for (i = 0; i < c->n; i++)
                used[i] = 0;
            for (i = 0; i < nh; i++)
                used[seq[i]]++;
            for (i = 0; i < c->n && miss == c->n; i++)
                if (!used[i])
                    miss = i;
            if (miss == c->n || nh < c->n)
                break;
            for (i = 0, w = 0xFFFFu; i < nh; i++)
                if (used[seq[i]] > 1u && ar_weight(hit[i], c->len) < w) {
                    w = ar_weight(hit[i], c->len);
                    best = i;
                }
            if (best == nh)
                break;
            seq[best] = (uint8_t)miss;
        }
    }
    for (i = 0; i < c->len; i++) {
        p->st[i].idx = 0;
        p->st[i].note = 0;
    }
    for (i = 0; i < nh; i++) {
        bfm_step_t *st = &p->st[hit[i]];
        st->idx = seq[i];
        st->note = c->pool[seq[i]];
    }
}

/* --------------------------------------------------------------- timbre --- */
static int32_t ar_contour(ar_ctx_t *c, uint32_t kind, uint32_t i, uint32_t m, int32_t rnd)
{
    int32_t len = (int32_t)c->len;
    switch (kind) {
    case 0: return -64 + (int32_t)(128 * (int32_t)i / len);           /* rising */
    case 1: return 63 - (int32_t)(128 * (int32_t)i / len);            /* falling */
    case 2: return (i & 1u) ? 63 : -64;                               /* alternating */
    case 3: return -64 + (int32_t)(128 * (int32_t)(i % m) / (int32_t)m);   /* per motif */
    default: return rnd;                                              /* random, held per step */
    }
}

static void ar_timbre(ar_ctx_t *c, bfm_pattern_t *p)
{
    uint32_t i, m = ar_motif_len(c), k1 = ar_below(&c->s, 5u), k2 = ar_below(&c->s, 5u), k3 = ar_below(&c->s, 5u);
    int32_t a = (int32_t)c->tvar;
    for (i = 0; i < c->len; i++) {
        bfm_step_t *st = &p->st[i];
        int32_t r1 = (int32_t)ar_below(&c->s, 128u) - 64, r2 = (int32_t)ar_below(&c->s, 128u) - 64;
        int32_t r3 = (int32_t)ar_below(&c->s, 128u) - 64, fm, fb, mo;
        if (!(st->flags & ST_ON)) {
            st->fm = st->fb = st->morph = 0;
            continue;
        }
        fm = ar_contour(c, k1, i, m, r1) * a / 127;
        fb = ar_contour(c, k2, i, m, r2) * a / 254;
        mo = ar_contour(c, k3, i, m, r3) * a / 127;
        if (st->flags & ST_ACC)
            fm += a / 4;
        st->fm = (int8_t)(fm > 63 ? 63 : fm < -64 ? -64 : fm);
        st->fb = (int8_t)(fb > 63 ? 63 : fb < -64 ? -64 : fb);
        st->morph = (int8_t)(mo > 63 ? 63 : mo < -64 ? -64 : mo);
    }
}

/* ---------------------------------------------------------------- entry --- */
/* the pool: notes ascending, distinct, the lowest BFM_POOL of them */
static uint32_t bfm_pool_make(const uint8_t *notes, uint32_t n, uint8_t *pool)
{
    uint32_t i, k = 0, nt;
    for (nt = 0; nt < 128u && k < BFM_POOL; nt++)
        for (i = 0; i < n; i++)
            if (notes[i] == nt) {
                pool[k++] = (uint8_t)nt;
                break;
            }
    return k;
}

/* generate into *out. prev: the pattern before (the LOCKs keep its layers; may be out itself). len: 8 or 16 */
static void bfm_generate(bfm_pattern_t *out, const uint8_t *pool, uint32_t npool, uint32_t len,
                         const bfm_arpset_t *a, uint32_t seed, uint32_t locks, const bfm_pattern_t *prev)
{
    bfm_pattern_t p;
    ar_ctx_t c;
    uint8_t keep[BFM_MAXSTEP];
    uint32_t i, nkeep = 0, same_len = prev && prev->len == len && prev->npool;
    if (!npool) {
        memset(out, 0, sizeof *out);
        out->len = (uint8_t)len;
        out->seed = seed;
        return;
    }
    memset(&p, 0, sizeof p);
    p.len = (uint8_t)len;
    p.npool = (uint8_t)npool;
    for (i = 0; i < npool; i++)
        p.pool[i] = pool[i];
    p.seed = seed;
    c.pool = p.pool;
    c.n = npool;
    c.len = len;
    c.dens = a->density;
    c.cplx = a->complexity;
    c.gvar = a->gatevar;
    c.tvar = a->timbrevar;
    c.gate = a->gate ? a->gate : 1u;
    c.s = seed;
    /* every layer draws from its own stream, so keeping one never shifts the others' random numbers */
    if ((locks & LK_RHYTHM) && same_len) {
        for (i = 0; i < len; i++) {
            p.st[i].flags = prev->st[i].flags;
            p.st[i].gate = prev->st[i].gate;
        }
    } else {
        c.s = seed ^ 0x52485900u;                           /* "RHY" */
        ar_rhythm(&c, &p);
    }
    if ((locks & LK_PITCH) && prev && prev->npool) {
        for (i = 0; i < prev->len; i++)
            if (prev->st[i].flags & ST_ON)
                keep[nkeep++] = prev->st[i].idx;
    }
    c.s = seed ^ 0x50495400u;                               /* "PIT" */
    ar_pitch(&c, &p, keep, nkeep);
    if ((locks & LK_TIMBRE) && same_len) {
        for (i = 0; i < len; i++) {
            p.st[i].fm = (p.st[i].flags & ST_ON) ? prev->st[i].fm : 0;
            p.st[i].fb = (p.st[i].flags & ST_ON) ? prev->st[i].fb : 0;
            p.st[i].morph = (p.st[i].flags & ST_ON) ? prev->st[i].morph : 0;
        }
    } else {
        c.s = seed ^ 0x54494D00u;                           /* "TIM" */
        ar_timbre(&c, &p);
    }
    *out = p;
}

/* FREEZE: the structure stays, the notes come from the new pool by index */
static void bfm_pat_remap(bfm_pattern_t *p, const uint8_t *pool, uint32_t npool)
{
    uint32_t i;
    if (!npool)
        return;
    for (i = 0; i < npool; i++)
        p->pool[i] = pool[i];
    for (; i < BFM_POOL; i++)
        p->pool[i] = 0;
    p->npool = (uint8_t)npool;
    for (i = 0; i < p->len; i++)
        if (p->st[i].flags & ST_ON) {
            p->st[i].idx = (uint8_t)(p->st[i].idx % npool);
            p->st[i].note = pool[p->st[i].idx];
        }
}

/* every note in the pool, every index in range, the length 8 or 16: 1 = valid */
static int bfm_pat_valid(const bfm_pattern_t *p)
{
    uint32_t i, k, ok;
    if (p->len != 8u && p->len != 16u)
        return 0;
    if (p->npool > BFM_POOL)
        return 0;
    for (i = 0; i < p->len; i++) {
        const bfm_step_t *st = &p->st[i];
        if (!(st->flags & ST_ON))
            continue;
        if (!p->npool || st->idx >= p->npool || st->gate < 1u || st->gate > 100u)
            return 0;
        for (k = 0, ok = 0; k < p->npool; k++)
            ok |= p->pool[k] == st->note;
        if (!ok || p->pool[st->idx] != st->note)
            return 0;
    }
    return 1;
}
