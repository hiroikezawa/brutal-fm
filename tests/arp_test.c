/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Hiro Ikezawa */
/* COMPLEX ARP: the generator and its playback.
 * The rule above all: every note the ARP plays is one of the keys held (the pool) - checked on the pattern data
 * of hundreds of thousands of generated patterns, and on every note the engine is actually asked to start while
 * the ARP plays (the trace hook in bfm_dsp.c). Then: 8 / 16 steps, determinism (the same input and seed give
 * the same bytes), playback never changes the pattern, the capture window, loop-boundary switching, HOLD,
 * FREEZE, the three LOCKs, REGENERATE, UNDO, LENGTH, the timing against the clock. */
#include <stdint.h>
static void trace_on(uint32_t note, uint32_t vel);
static void trace_off(uint32_t note);
#define BFM_TRACE_ON(n, v) trace_on(n, v)
#define BFM_TRACE_OFF(n) trace_off(n)
#include "bfm_host.h"

/* ---------------------------------------------------------------- trace --- */
#define TMAX 100000
static struct { uint32_t t, note, vel, on; } tr[TMAX];
static uint32_t ntr;
static void trace_on(uint32_t note, uint32_t vel)
{
    if (ntr < TMAX) { tr[ntr].t = sq.now; tr[ntr].note = note; tr[ntr].vel = vel; tr[ntr].on = 1; ntr++; }
}
static void trace_off(uint32_t note)
{
    if (ntr < TMAX) { tr[ntr].t = sq.now; tr[ntr].note = note; tr[ntr].vel = 0; tr[ntr].on = 0; ntr++; }
}

static uint32_t rnd_s = 12345;
static uint32_t rnd(uint32_t n) { rnd_s = rnd_s * 1103515245u + 12345u; return n ? (rnd_s >> 8) % n : 0; }

static int in_pool(const bfm_pattern_t *p, uint32_t note)
{
    uint32_t k;
    for (k = 0; k < p->npool; k++)
        if (p->pool[k] == note)
            return 1;
    return 0;
}

static int pat_notes_ok(const bfm_pattern_t *p, const uint8_t *keys, uint32_t nk)
{
    uint32_t i, k, ok;
    for (i = 0; i < p->len; i++) {
        if (!(p->st[i].flags & ST_ON))
            continue;
        for (k = 0, ok = 0; k < nk; k++)
            ok |= keys[k] == p->st[i].note;
        if (!ok)
            return 0;
    }
    return 1;
}

static bfm_arpset_t rnd_arp(void)
{
    bfm_arpset_t a;
    memset(&a, 0, sizeof a);
    a.density = (uint8_t)rnd(128);
    a.complexity = (uint8_t)rnd(128);
    a.gatevar = (uint8_t)rnd(128);
    a.timbrevar = (uint8_t)rnd(128);
    a.gate = (uint8_t)(1 + rnd(100));
    return a;
}

/* ---------------------------------------------------------- generator --- */
static void test_generator(void)
{
    uint32_t i, bad = 0, bad_valid = 0, hits_min = 99, len8 = 0, len16 = 0, spread_bad = 0, down = 0, total = 0;
    uint32_t n_rep;
    bfm_pattern_t p, q, prev;
    printf("== generator: the pool rule, 8 / 16 steps, determinism\n");
    for (i = 0; i < 200000u; i++) {
        uint8_t keys[24], pool[BFM_POOL];
        uint32_t nk = 1 + rnd(i % 7u == 0u ? 24u : 6u), k, np, len = rnd(2) ? 16u : 8u, hits = 0;
        bfm_arpset_t a = rnd_arp();
        uint32_t seed = rnd_s ^ i * 2654435761u, locks = rnd(16);
        for (k = 0; k < nk; k++)
            keys[k] = (uint8_t)(24 + rnd(80));
        np = bfm_pool_make(keys, nk, pool);
        memset(&prev, 0, sizeof prev);
        if (rnd(2)) {
            bfm_generate(&prev, pool, np, rnd(2) ? 16u : 8u, &a, seed + 1u, 0, 0);
        }
        bfm_generate(&p, pool, np, len, &a, seed, locks, prev.npool ? &prev : 0);
        total++;
        if (!pat_notes_ok(&p, keys, nk))
            bad++;
        if (!bfm_pat_valid(&p))
            bad_valid++;
        len8 += p.len == 8u;
        len16 += p.len == 16u;
        for (k = 0; k < p.len; k++)
            hits += (p.st[k].flags & ST_ON) != 0u;
        if (hits < hits_min)
            hits_min = hits;
        down += (p.st[0].flags & ST_ON) != 0u;
        if (!(locks & LK_PITCH) && hits >= np) {      /* spread: every key heard when there are hits enough */
            uint32_t used = 0, m;
            for (m = 0; m < np; m++)
                for (k = 0; k < p.len; k++)
                    if ((p.st[k].flags & ST_ON) && p.st[k].note == pool[m]) {
                        used++;
                        break;
                    }
            spread_bad += used != np;
        }
        bfm_generate(&q, pool, np, len, &a, seed, locks, prev.npool ? &prev : 0);
        if (memcmp(&p, &q, sizeof p))
            bad++;
    }
    CHECK(bad == 0, "%u random patterns (1..24 keys, any settings, seeds, locks): every note is a held key, the "
          "same input twice gives the same bytes (%u failures)", total, bad);
    CHECK(bad_valid == 0, "bfm_pat_valid accepts them all: note == pool[idx], gates 1..100 (%u failures)", bad_valid);
    CHECK(len8 && len16 && len8 + len16 == total, "lengths 8 (%u) and 16 (%u) only", len8, len16);
    CHECK(hits_min >= 1, "never an empty pattern (fewest hits %u)", hits_min);
    CHECK(down == total, "the downbeat always plays (a key that starts the ARP is heard at once): %u / %u", down, total);
    CHECK(spread_bad == 0, "with hits enough, every held key is heard (%u patterns missed one)", spread_bad);
    {   /* single key, two keys, four keys, many keys */
        static const uint8_t K1[1] = {60}, K2[2] = {60, 67}, K4[4] = {48, 51, 55, 58};
        uint8_t KM[20], pool[BFM_POOL];
        bfm_arpset_t a = {0};
        uint32_t np, k, ok = 1;
        a.density = 90; a.complexity = 64; a.gatevar = 40; a.timbrevar = 40; a.gate = 60;
        np = bfm_pool_make(K1, 1, pool);
        bfm_generate(&p, pool, np, 16, &a, 1, 0, 0);
        for (k = 0; k < 16u; k++)
            ok &= !(p.st[k].flags & ST_ON) || p.st[k].note == 60u;
        CHECK(np == 1 && ok && bfm_pat_valid(&p), "one key (C4): every hit is C4");
        np = bfm_pool_make(K2, 2, pool);
        bfm_generate(&p, pool, np, 8, &a, 2, 0, 0);
        CHECK(np == 2 && pat_notes_ok(&p, K2, 2) && bfm_pat_valid(&p), "two keys: only C4 and G4 (8 steps)");
        np = bfm_pool_make(K4, 4, pool);
        bfm_generate(&p, pool, np, 16, &a, 3, 0, 0);
        CHECK(np == 4 && pat_notes_ok(&p, K4, 4), "C3 Eb3 G3 Bb3: only those four (16 steps)");
        for (k = 0; k < 20u; k++)
            KM[k] = (uint8_t)(40 + 3 * k);
        np = bfm_pool_make(KM, 20, pool);
        bfm_generate(&p, pool, np, 16, &a, 4, 0, 0);
        CHECK(np == BFM_POOL && pat_notes_ok(&p, KM, 20) && pool[0] == 40 && pool[15] == 85,
              "20 keys: the lowest 16 make the pool (40..85), every note one of them");
        {   /* no octave is ever added: every note's pitch class and octave are a key's */
            uint32_t oct_bad = 0;
            for (k = 0; k < 16u; k++)
                if ((p.st[k].flags & ST_ON) && (p.st[k].note < 40 || p.st[k].note > 85 || (p.st[k].note - 40) % 3))
                    oct_bad++;
            CHECK(!oct_bad, "no octave copies, no passing notes: the notes are exactly the keys");
        }
    }
    {   /* different seeds give different patterns (a seed changes something) */
        static const uint8_t K4[4] = {48, 51, 55, 58};
        uint8_t pool[BFM_POOL];
        bfm_arpset_t a = {0};
        uint32_t np = bfm_pool_make(K4, 4, pool), s, diff = 0;
        a.density = 80; a.complexity = 64; a.gatevar = 40; a.timbrevar = 60; a.gate = 60;
        bfm_generate(&q, pool, np, 16, &a, 100, 0, 0);
        for (s = 101; s < 201; s++) {
            bfm_generate(&p, pool, np, 16, &a, s, 0, 0);
            diff += memcmp(&p.st, &q.st, sizeof p.st) != 0;
        }
        CHECK(diff >= 99, "100 other seeds: %u give another pattern", diff);
    }
    {   /* the musical rules: motifs repeat at low COMPLEXITY, hits follow DENSITY */
        static const uint8_t K3[3] = {50, 53, 57};
        uint8_t pool[BFM_POOL];
        bfm_arpset_t a = {0};
        uint32_t np = bfm_pool_make(K3, 3, pool), s, lo = 0, hi = 0, rep = 0;
        a.gate = 60;
        for (s = 1; s <= 200u; s++) {
            uint32_t k, h;
            a.density = 10; a.complexity = 10;
            bfm_generate(&p, pool, np, 16, &a, s, 0, 0);
            for (k = 0, h = 0; k < 16u; k++)
                h += (p.st[k].flags & ST_ON) != 0u;
            lo += h;
            for (k = 0, n_rep = 1; k < 4u; k++)
                n_rep &= (p.st[k].flags & ST_ON) == (p.st[k + 4u].flags & ST_ON) ||
                         (p.st[k].flags & ST_ON) == (p.st[k + 8u].flags & ST_ON);
            rep += n_rep;
            a.density = 120; a.complexity = 60;
            bfm_generate(&p, pool, np, 16, &a, s, 0, 0);
            for (k = 0, h = 0; k < 16u; k++)
                h += (p.st[k].flags & ST_ON) != 0u;
            hi += h;
        }
        CHECK(hi > lo * 2u, "DENSITY: low %.1f hits / 16, high %.1f", lo / 200.0, hi / 200.0);
        CHECK(rep > 150u, "low COMPLEXITY: a 4-step motif repeats (%u / 200)", rep);
    }
}

/* ------------------------------------------------------------ playback --- */
static void reset_engine(void)
{
    host_init();
    bfm_p = BFM_FACTORY[0];
    bfm_p.arp.on = 1;
    bfm_p.arp.hold = 0;
    bfm_p.arp.length = 1;
    bfm_p.arp.rate = RATE_16;
    bfm_p.arp.window = 20;           /* 80 ms */
    bfm_p.arp.locks = 0;
    memset(&bfm_p.pat, 0, sizeof bfm_p.pat);
    bfm_p.pat.len = 16;
    bfm_set.sync = SYNC_INT;
    bfm_set.bpm = 120 - 40;
    ntr = 0;
}
#define MS(x) ((uint32_t)((x) * BFM_FS / 1000u))
static void run_ms(uint32_t ms) { host_render(0, MS(ms)); }

static uint32_t step_samples(void) { return (uint32_t)(((uint64_t)sq_spt_q16() * 6u) >> 16); }

static void test_playback(void)
{
    uint32_t i, k;
    printf("== playback: the capture window, the first note at once, never a note outside the pool\n");
    reset_engine();
    bfm_key_push(48, 100, 1);
    run_ms(1);
    CHECK(ntr >= 1 && tr[0].on && tr[0].note == 48u && tr[0].t < MS(2), "the first key sounds at once (%u samples)",
          ntr ? tr[0].t : 0);
    run_ms(9);
    bfm_key_push(51, 100, 1);
    run_ms(10);
    bfm_key_push(55, 100, 1);
    run_ms(10);
    bfm_key_push(58, 100, 1);
    run_ms(100);
    CHECK(bfm_p.pat.npool == 4 && bfm_p.pat.pool[0] == 48 && bfm_p.pat.pool[3] == 58,
          "four keys 0 / 10 / 20 / 30 ms apart, window 80 ms: the pattern is the chord's (%u notes)", bfm_p.pat.npool);
    CHECK(!sq.pending, "..taken at once, not at the loop boundary (the first loop grows into the chord)");
    {   /* the rest of the loops: only the four notes */
        uint32_t bad = 0;
        ntr = 0;
        run_ms(8000);
        for (i = 0; i < ntr; i++)
            if (tr[i].on && tr[i].note != 48 && tr[i].note != 51 && tr[i].note != 55 && tr[i].note != 58)
                bad++;
        CHECK(ntr > 50 && !bad, "8 s of playback: %u note events, %u outside C3 Eb3 G3 Bb3", ntr, bad);
    }
    {   /* the pattern data never changes while it plays, every loop is the same */
        bfm_pattern_t before = bfm_p.pat;
        uint32_t l0 = sq.loops, seq_a[64], na = 0, seq_b[64], nb = 0, loops;
        while (sq.step != 0)
            host_render(0, BFM_CTL);
        ntr = 0;
        l0 = sq.loops;
        while (sq.loops == l0)
            host_render(0, BFM_CTL);
        for (i = 0; i < ntr && na < 64; i++)
            if (tr[i].on)
                seq_a[na++] = tr[i].note;
        ntr = 0;
        l0 = sq.loops;
        for (loops = 0; loops < 30; loops++) {
            uint32_t lx = sq.loops;
            ntr = 0;
            while (sq.loops == lx)
                host_render(0, BFM_CTL);
            for (i = 0, nb = 0; i < ntr && nb < 64; i++)
                if (tr[i].on)
                    seq_b[nb++] = tr[i].note;
            if (nb != na || memcmp(seq_a, seq_b, na * sizeof *seq_a))
                break;
        }
        CHECK(loops == 30, "30 loops: the same notes in the same order every loop (%u notes a loop)", na);
        CHECK(!memcmp(&before, &bfm_p.pat, sizeof before), "..and the pattern's bytes unchanged after it");
    }
    {   /* timing: a step is a 16th at 120 BPM (5512.5 samples); gates as written */
        uint32_t ons[64], n = 0, ok = 1, sp = step_samples();
        ntr = 0;
        run_ms(4000);
        for (i = 0; i < ntr && n < 64; i++)
            if (tr[i].on)
                ons[n++] = tr[i].t;
        for (i = 1; i < n; i++) {
            uint32_t d = ons[i] - ons[i - 1], steps = (d + sp / 2) / sp;
            if (steps == 0 || (d > steps * sp ? d - steps * sp : steps * sp - d) > BFM_CTL + 1u)
                ok = 0;
        }
        CHECK(sp > 5500 && sp < 5525 && ok, "note starts fall on the 16th grid at 120 BPM (%u samples a step, "
              "within one block)", sp);
    }
    {   /* every note off comes at the gate the step asks for (or is a tie) */
        uint32_t bad = 0, checked = 0, sp = step_samples();
        ntr = 0;
        run_ms(4000);
        for (i = 0; i + 1 < ntr; i++) {
            if (tr[i].on) {
                uint32_t j, s;
                for (s = 0; s < bfm_p.pat.len && !((bfm_p.pat.st[s].flags & ST_ON) &&
                                                     bfm_p.pat.st[s].note == tr[i].note); s++)
                    ;
                for (j = i + 1; j < ntr && !(!tr[j].on && tr[j].note == tr[i].note); j++)
                    ;
                if (j < ntr && s < bfm_p.pat.len && !(bfm_p.pat.st[s].flags & ST_TIE)) {
                    uint32_t len = tr[j].t - tr[i].t;
                    checked++;
                    if (len > sp + BFM_CTL * 2u)
                        bad++;
                }
            }
        }
        CHECK(checked > 10 && bad == 0, "note lengths within a step unless tied (%u checked)", checked);
    }
    /* release everything: without HOLD the ARP stops */
    for (k = 0; k < 4u; k++)
        bfm_key_push((const uint8_t[]){48, 51, 55, 58}[k], 0, 0);
    run_ms(200);
    CHECK(!sq.playing && sq.cur_note < 0, "all keys up, HOLD off: the ARP stops, its note released");
}

static void test_switching(void)
{
    uint32_t i, sw_step = 99;
    bfm_pattern_t a;
    printf("== chord changes: at the loop boundary, the same keys change nothing, HOLD, the window\n");
    reset_engine();
    bfm_key_push(48, 100, 1);
    bfm_key_push(55, 100, 1);
    run_ms(300);
    a = bfm_p.pat;
    /* a third key well after the window: a new chord, switching at the next loop */
    while (sq.step != 5)
        host_render(0, BFM_CTL);
    bfm_key_push(60, 100, 1);
    run_ms(150);
    CHECK(sq.pending && !memcmp(&a, &bfm_p.pat, sizeof a), "a key added mid-loop: the new pattern waits (NEXT)");
    for (i = 0; i < 20000u && sq.pending; i++) {
        uint32_t st = sq.step;
        host_render(0, BFM_CTL);
        if (!sq.pending)
            sw_step = st;
    }
    CHECK(!sq.pending && sw_step == 0 && bfm_p.pat.npool == 3, "..and switches in exactly when step 1 plays "
          "(pool %u notes)", bfm_p.pat.npool);
    {
        uint32_t bad = 0;
        ntr = 0;
        run_ms(3000);
        for (i = 0; i < ntr; i++)
            if (tr[i].on && tr[i].note != 48 && tr[i].note != 55 && tr[i].note != 60)
                bad++;
        CHECK(!bad, "after the switch: only C3 G3 C4");
    }
    /* the same set again: nothing changes */
    a = bfm_p.pat;
    bfm_key_push(60, 0, 0);
    run_ms(20);
    bfm_key_push(60, 100, 1);
    run_ms(400);
    run_ms(4000);
    CHECK(!memcmp(&a, &bfm_p.pat, sizeof a) && !sq.pending, "a key let go and pressed again inside the window: "
          "the same pattern (no regeneration)");
    /* HOLD */
    bfm_p.arp.hold = 1;
    bfm_key_push(48, 0, 0);
    bfm_key_push(55, 0, 0);
    bfm_key_push(60, 0, 0);
    run_ms(500);
    CHECK(sq.playing, "HOLD: every key let go, the loop plays on");
    a = bfm_p.pat;
    bfm_key_push(62, 100, 1);
    bfm_key_push(65, 100, 1);
    run_ms(150);
    {
        const bfm_pattern_t *np = sq.pending ? &sq.pend : &bfm_p.pat;
        CHECK(np->npool == 2 && np->pool[0] == 62 && np->pool[1] == 65, "HOLD: a new gesture is a new set (D4 F4), "
              "not added to the old one (%u notes, %s)", np->npool, sq.pending ? "waiting" : "switched");
    }
    run_ms(5000);
    CHECK(!sq.pending && bfm_p.pat.npool == 2, "..switched in at the loop");
    bfm_key_push(62, 0, 0);
    bfm_key_push(65, 0, 0);
    bfm_p.arp.hold = 0;
    run_ms(100);
    CHECK(!sq.playing, "HOLD off with no key down: the ARP stops");
    /* a long window: keys pressed one by one build one chord */
    reset_engine();
    bfm_p.arp.window = 100;          /* 400 ms */
    bfm_key_push(48, 100, 1);
    run_ms(250);
    bfm_key_push(52, 100, 1);
    run_ms(250);
    bfm_key_push(55, 100, 1);
    run_ms(500);
    CHECK(bfm_p.pat.npool == 3 && !sq.pending, "WINDOW 400 ms: three keys 250 ms apart make one chord at once");
}

static void test_locks(void)
{
    bfm_pattern_t a, b;
    uint32_t i, ok;
    uint8_t ia[16], ib[16];
    uint32_t na = 0, nb = 0;
    printf("== REGENERATE, the LOCKs, FREEZE, UNDO, LENGTH\n");
    reset_engine();
    bfm_p.arp.hold = 1;
    bfm_p.arp.timbrevar = 80;
    bfm_key_push(48, 100, 1); bfm_key_push(51, 100, 1); bfm_key_push(55, 100, 1); bfm_key_push(58, 100, 1);
    run_ms(200);
    bfm_key_push(48, 0, 0); bfm_key_push(51, 0, 0); bfm_key_push(55, 0, 0); bfm_key_push(58, 0, 0);
    a = bfm_p.pat;
    rq_regen = 1;
    run_ms(5000);
    b = bfm_p.pat;
    CHECK(memcmp(&a.st, &b.st, sizeof a.st) && b.seed == bfm_seed_next(a.seed) && bfm_pat_valid(&b) &&
          !memcmp(a.pool, b.pool, sizeof a.pool), "REGENERATE: a new pattern of the same keys (the next seed)");
    rq_undo = 1;
    run_ms(10);
    CHECK(!memcmp(&a, &bfm_p.pat, sizeof a), "UNDO: the pattern before, byte for byte");
    /* RHYTHM LOCK */
    a = bfm_p.pat;
    bfm_p.arp.locks = LK_RHYTHM;
    rq_regen = 1;
    run_ms(5000);
    b = bfm_p.pat;
    for (i = 0, ok = 1; i < 16u; i++)
        ok &= a.st[i].flags == b.st[i].flags && a.st[i].gate == b.st[i].gate;
    CHECK(ok && memcmp(&a.st, &b.st, sizeof a.st), "RHYTHM LOCK: on / off, accents, ties and gates kept; notes and "
          "timbre new");
    /* PITCH LOCK */
    a = bfm_p.pat;
    bfm_p.arp.locks = LK_PITCH;
    rq_regen = 1;
    run_ms(5000);
    b = bfm_p.pat;
    for (i = 0; i < 16u; i++) {
        if (a.st[i].flags & ST_ON) ia[na++] = a.st[i].idx;
        if (b.st[i].flags & ST_ON) ib[nb++] = b.st[i].idx;
    }
    for (i = 0, ok = 1; i < nb; i++)
        ok &= ib[i] == ia[i % na];
    CHECK(ok && nb, "PITCH LOCK: the order of the notes kept over the new rhythm (%u -> %u hits)", na, nb);
    /* TIMBRE LOCK */
    bfm_p.arp.locks = LK_TIMBRE | LK_RHYTHM;
    a = bfm_p.pat;
    rq_regen = 1;
    run_ms(5000);
    b = bfm_p.pat;
    for (i = 0, ok = 1; i < 16u; i++)
        ok &= a.st[i].fm == b.st[i].fm && a.st[i].fb == b.st[i].fb && a.st[i].morph == b.st[i].morph;
    CHECK(ok, "TIMBRE LOCK: FM / FEEDBACK / MORPH per step kept");
    /* FREEZE: REGENERATE refused; a new chord is laid on the frozen structure */
    bfm_p.arp.locks = LK_FREEZE;
    a = bfm_p.pat;
    rq_regen = 1;
    run_ms(5000);
    CHECK(!memcmp(&a, &bfm_p.pat, sizeof a), "FREEZE: REGENERATE changes nothing");
    bfm_key_push(50, 100, 1); bfm_key_push(53, 100, 1); bfm_key_push(57, 100, 1); bfm_key_push(60, 100, 1);
    run_ms(5000);
    b = bfm_p.pat;
    for (i = 0, ok = 1; i < 16u; i++)
        ok &= a.st[i].flags == b.st[i].flags && a.st[i].gate == b.st[i].gate && a.st[i].fm == b.st[i].fm &&
              a.st[i].idx == b.st[i].idx && (!(b.st[i].flags & ST_ON) || b.st[i].note == b.pool[b.st[i].idx]);
    CHECK(ok && b.pool[0] == 50 && bfm_pat_valid(&b), "FREEZE: a new chord keeps rhythm, gates, timbre and the "
          "order (by index); the notes are the new keys");
    bfm_key_push(50, 0, 0); bfm_key_push(53, 0, 0); bfm_key_push(57, 0, 0); bfm_key_push(60, 0, 0);
    /* LENGTH 16 -> 8: a pattern made for 8, not the first half of the 16 */
    bfm_p.arp.locks = 0;
    {
        uint32_t trunc = 0, s;
        uint8_t pool[4] = {48, 51, 55, 58};
        bfm_arpset_t ar = bfm_p.arp;
        for (s = 1; s <= 100u; s++) {
            bfm_pattern_t p16, p8;
            bfm_generate(&p16, pool, 4, 16, &ar, s, 0, 0);
            bfm_generate(&p8, pool, 4, 8, &ar, s, 0, 0);
            trunc += !memcmp(p16.st, p8.st, 8 * sizeof(bfm_step_t));
        }
        CHECK(trunc < 10u, "an 8-step pattern is generated for 8 steps (same as a cut 16 in %u / 100 seeds)", trunc);
    }
    a = bfm_p.pat;
    bfm_p.arp.length = 0;
    rq_len = 1;
    run_ms(5000);
    CHECK(bfm_p.pat.len == 8 && bfm_pat_valid(&bfm_p.pat) && !memcmp(a.pool, bfm_p.pat.pool, sizeof a.pool),
          "LENGTH 8: a new 8-step pattern of the same keys");
    bfm_p.arp.length = 1;
    rq_len = 1;
    run_ms(5000);
    CHECK(bfm_p.pat.len == 16 && bfm_pat_valid(&bfm_p.pat), "LENGTH 16 again: 16 steps");
    {   /* the SEED knob: a pattern from exactly that seed */
        bfm_pattern_t want;
        bfm_generate(&want, bfm_p.pat.pool, bfm_p.pat.npool, 16, &bfm_p.arp, 0xBEEF, 0, &bfm_p.pat);
        rq_seed_v = 0xBEEF;
        rq_seed = 1;
        run_ms(5000);
        CHECK(!memcmp(&want, &bfm_p.pat, sizeof want), "SEED 0xBEEF: the pattern generate(seed 0xBEEF) gives");
    }
}

static void test_clock(void)
{
    uint32_t i, ons = 0, t0 = 0, t1 = 0;
    printf("== MIDI clock: EXT follows it, Start / Stop / Continue, a lost clock, AUTO falls back\n");
    reset_engine();
    bfm_set.sync = SYNC_EXT;
    bfm_p.arp.hold = 1;
    bfm_key_push(48, 100, 1);
    run_ms(200);
    CHECK(sq.cur_note < 0 || ntr <= 1, "EXT without a clock: nothing plays after the first key");
    ntr = 0;
    sq_midi(0xFu | 0xFAu << 8);                        /* Start */
    for (i = 0; i < 96u; i++) {                        /* 4 beats of clock at 100 BPM: a tick every 25 ms */
        sq_midi(0xFu | 0xF8u << 8);
        run_ms(25);
    }
    for (i = 0; i < ntr; i++)
        if (tr[i].on) {
            if (!ons) t0 = tr[i].t;
            t1 = tr[i].t;
            ons++;
        }
    CHECK(ons >= 8 && ons <= 16, "EXT: notes on the clock's 16ths (%u in 4 beats)", ons);
    CHECK(sq_bpm_x10() > 990 && sq_bpm_x10() < 1010, "the tempo learnt from the clock: %u.%u BPM", sq_bpm_x10() / 10,
          sq_bpm_x10() % 10);
    sq_midi(0xFu | 0xFCu << 8);                        /* Stop */
    ntr = 0;
    run_ms(400);
    for (i = 0, ons = 0; i < ntr; i++)
        ons += tr[i].on;
    CHECK(!ons && sq.cur_note < 0, "Stop: silent");
    sq_midi(0xFu | 0xFBu << 8);                        /* Continue */
    for (i = 0; i < 24u; i++) {
        sq_midi(0xFu | 0xF8u << 8);
        run_ms(25);
    }
    for (i = 0, ons = 0; i < ntr; i++)
        ons += tr[i].on;
    CHECK(ons >= 1, "Continue: plays on (%u notes in a beat)", ons);
    ntr = 0;
    run_ms(1000);                                      /* the clock stops without a Stop */
    CHECK(sq.ext_lost && sq.cur_note < 0, "EXT: no clock for 300 ms counts as stopped, the note released");
    /* AUTO: the same loss plays on at the tempo learnt */
    bfm_set.sync = SYNC_AUTO;
    sq_midi(0xFu | 0xFAu << 8);
    for (i = 0; i < 48u; i++) {
        sq_midi(0xFu | 0xF8u << 8);
        run_ms(25);
    }
    ntr = 0;
    run_ms(2000);
    for (i = 0, ons = 0; i < ntr; i++)
        ons += tr[i].on;
    CHECK(sq.ext_lost && ons >= 8, "AUTO: the clock gone, the internal clock goes on at ~100 BPM (%u notes in 2 s)", ons);
    (void)t0; (void)t1;
}

static void test_midi(void)
{
    printf("== MIDI in: notes, CCs to the four macros, Program Change, channel filter\n");
    reset_engine();
    bfm_p.arp.on = 0;
    sq_midi(0x9u | 0x90u << 8 | 60u << 16 | 100u << 24);
    run_ms(5);
    CHECK(ntr == 1 && tr[0].note == 60, "Note On (USB-MIDI packet) plays C4");
    sq_midi(0x8u | 0x80u << 8 | 60u << 16);
    run_ms(5);
    CHECK(ntr == 2 && !tr[1].on, "Note Off releases it");
    sq_midi(0xBu | 0xB0u << 8 | 1u << 16 | 99u << 24);
    sq_midi(0xBu | 0xB0u << 8 | 74u << 16 | 11u << 24);
    sq_midi(0xBu | 0xB0u << 8 | 71u << 16 | 22u << 24);
    sq_midi(0xBu | 0xB0u << 8 | 76u << 16 | 33u << 24);
    CHECK(bfm_p.morph == 99 && bfm_p.intensity == 11 && bfm_p.feedback == 22 && bfm_p.movement == 33,
          "CC 1 / 74 / 71 / 76: MORPH INTENSITY FEEDBACK MOVEMENT");
    bfm_set.cc[0] = 20;
    sq_midi(0xBu | 0xB0u << 8 | 20u << 16 | 5u << 24);
    CHECK(bfm_p.morph == 5, "the CC numbers are settings (MORPH on CC 20)");
    sq_midi(0xCu | 0xC0u << 8 | 7u << 16);
    CHECK(rq_pc == 8, "Program Change 7: preset 8 asked of the main loop");
    bfm_set.midi_ch = 2;
    ntr = 0;
    sq_midi(0x9u | 0x90u << 8 | 62u << 16 | 100u << 24);
    sq_midi(0x9u | 0x91u << 8 | 64u << 16 | 100u << 24);
    run_ms(5);
    CHECK(ntr == 1 && tr[0].note == 64, "MIDI CH 2: channel 1 ignored, channel 2 plays");
    bfm_set.midi_ch = 0;
    sq_midi(0xBu | 0xB0u << 8 | 123u << 16);
    run_ms(5);
    CHECK(!sq.playing, "All Notes Off");
}

int main(void)
{
    host_init();
    test_generator();
    test_playback();
    test_switching();
    test_locks();
    test_clock();
    test_midi();
    printf("arp: %d checks, %d failed\n", t_count, t_fail);
    return t_fail != 0;
}
