/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Hiro Ikezawa */
/* BRUTAL FM runtime, in the audio ISR: the keys and MIDI in, the clock (internal or MIDI clock), the COMPLEX
 * ARP's playback, and the requests the UI makes (REGENERATE, UNDO, LENGTH, preset loads). The main loop never
 * touches this state except through the one-byte request flags and the key ring, so nothing here needs a lock.
 *
 * Keys with the ARP on:
 *   - the first key of a gesture starts the ARP at once from that key alone (step 0 is always a hit, so the
 *     press is heard immediately); keys pressed within the capture WINDOW of the last press join the set
 *     (rolling: building a chord key by key works when the gaps are shorter than the window); when the window
 *     closes the pattern of the whole set replaces the first one in place, without waiting
 *   - after that, a change of the held set (a key added or released, or with HOLD a new gesture after all keys
 *     were let go) is captured the same way and switches at the next loop boundary
 *   - without HOLD, releasing every key stops the ARP; with HOLD it loops on
 *   - the same set again changes nothing (REGENERATE makes a new pattern of it)
 * Clock: 24 ticks a quarter note. INT: the internal tempo. EXT: MIDI clock only (Start resets to step 0,
 * Continue resumes, Stop silences; no clock for 300 ms counts as stopped). AUTO: MIDI clock while it comes,
 * the internal tempo (at the last tempo measured) when it stops: the ARP goes on playing. */

static const uint8_t RATE_TICKS[RATE_N] = {3, 4, 6, 8, 12, 24};
static const char *const RATE_NAME[RATE_N] = {"1/32", "1/16T", "1/16", "1/8T", "1/8", "1/4"};

#define KQ 64u                   /* key events from the main loop: note | vel << 8 | down << 16 */
static uint32_t kq[KQ];
static volatile uint32_t kq_w, kq_r;

/* requests from the main loop: it sets one to 1, the ISR clears it when done */
static volatile uint8_t rq_regen, rq_undo, rq_len, rq_load, rq_panic, rq_arp_off, rq_play;
static volatile uint16_t rq_pc;  /* MIDI Program Change for the main loop: program + 1 */
static volatile uint8_t rq_seed; /* a pattern from exactly this seed (the SEED knob) */
static volatile uint32_t rq_seed_v;
static bfm_patch_t bfm_stage;    /* a preset the main loop has prepared (rq_load) */

#define UNDO_N 4u
static struct {
    uint32_t now;                /* samples since boot */
    /* clock */
    uint32_t frac;               /* internal clock: Q16 samples into the tick */
    uint32_t tick;               /* ticks since the ARP started */
    uint32_t ext_last;           /* sample time of the last MIDI clock */
    uint32_t ext_period;         /* samples per MIDI clock tick, measured (smoothed) */
    uint8_t ext_run, ext_seen, ext_lost;
    /* keys */
    uint32_t phys[4];            /* keys held (128-bit set) */
    uint32_t cset[4];            /* the set being captured */
    uint32_t cap_until;
    uint8_t capturing, initial, playing;
    /* playback */
    uint8_t step;                /* the step that plays next is step; cur_step played last */
    int16_t cur_note;            /* the ARP's sounding note, -1 none */
    uint32_t off_at;             /* sample time of its note off, 0 = held (tie) */
    uint8_t pending;             /* pend switches in at the next loop boundary */
    bfm_pattern_t pend;
    bfm_pattern_t undo[UNDO_N];
    uint8_t nundo;
    uint8_t flash;               /* the step that just played (UI) */
    uint32_t loops;              /* completed loops (tests) */
} sq;

static uint32_t sq_spt_q16(void)                 /* samples per tick, Q16 */
{
    if (sq.ext_seen && sq.ext_period && bfm_set.sync != SYNC_INT)
        return sq.ext_period;
    return (BFM_FS * 60u * 256u / ((40u + bfm_set.bpm) * 24u)) << 8;   /* (32-bit: no 64-bit divide here) */
}

static uint32_t sq_bpm_x10(void)                 /* the tempo shown */
{
    uint32_t spt = sq_spt_q16();
    return spt >> 8 ? ((BFM_FS * 600u / 24u) << 8) / (spt >> 8) : 0u;
}

static int sq_clock_running(void)
{
    if (bfm_set.sync == SYNC_INT)
        return 1;
    if (bfm_set.sync == SYNC_EXT)
        return sq.ext_run && !sq.ext_lost;
    return !sq.ext_seen || sq.ext_lost || sq.ext_run;      /* AUTO: internal unless a clock says stop */
}

static int sq_ext_drives(void)
{
    return bfm_set.sync != SYNC_INT && sq.ext_seen && !sq.ext_lost;
}

/* ---------------------------------------------------------------- sets --- */
static void set_clear(uint32_t *s) { s[0] = s[1] = s[2] = s[3] = 0; }
static int set_empty(const uint32_t *s) { return !(s[0] | s[1] | s[2] | s[3]); }
static void set_add(uint32_t *s, uint32_t n) { s[(n >> 5) & 3u] |= 1u << (n & 31u); }
static void set_del(uint32_t *s, uint32_t n) { s[(n >> 5) & 3u] &= ~(1u << (n & 31u)); }
static uint32_t set_list(const uint32_t *s, uint8_t *out)   /* ascending, at most BFM_POOL */
{
    uint32_t n, k = 0;
    for (n = 0; n < 128u && k < BFM_POOL; n++)
        if (s[n >> 5] & (1u << (n & 31u)))
            out[k++] = (uint8_t)n;
    return k;
}

static uint32_t sq_len(void) { return bfm_p.arp.length ? 16u : 8u; }

static void sq_note_stop(void)
{
    if (sq.cur_note >= 0)
        bfm_note_off((uint32_t)sq.cur_note);
    sq.cur_note = -1;
    sq.off_at = 0;
}

static void sq_undo_push(void)
{
    uint32_t i;
    if (!bfm_p.pat.npool)
        return;
    if (sq.nundo == UNDO_N) {
        for (i = 1; i < UNDO_N; i++)
            sq.undo[i - 1u] = sq.undo[i];
        sq.nundo--;
    }
    sq.undo[sq.nundo++] = bfm_p.pat;
}

/* a new pattern from pool (seed: the next of the chain), the LOCKs applied, FREEZE remaps instead */
static void sq_make(bfm_pattern_t *out, const uint8_t *pool, uint32_t npool, uint32_t len, int new_seed)
{
    const bfm_pattern_t *prev = &bfm_p.pat;
    uint32_t seed = new_seed || !prev->seed ? bfm_seed_next(prev->seed ? prev->seed : 0x1234u) : prev->seed;
    if ((bfm_p.arp.locks & LK_FREEZE) && prev->npool && prev->len == len) {
        *out = *prev;
        bfm_pat_remap(out, pool, npool);
        return;
    }
    bfm_generate(out, pool, npool, len, &bfm_p.arp, seed, bfm_p.arp.locks, prev->npool ? prev : 0);
}

static void sq_install(const bfm_pattern_t *p)   /* the pattern plays from now */
{
    sq_undo_push();
    bfm_p.pat = *p;
    if (sq.step >= bfm_p.pat.len)
        sq.step = 0;
}

static void sq_tick(void);
static int sq_ext_drives(void);
static void sq_start(void)
{
    sq.playing = 1;
    sq.step = 0;
    sq.tick = 0;
    sq.frac = 0;
    if (!sq_ext_drives())
        sq_tick();                                       /* step 1 now: the key that started it sounds at once */
}

static void sq_stop(void)
{
    sq_note_stop();
    sq.playing = 0;
    sq.capturing = 0;
    sq.initial = 0;
    sq.pending = 0;
}

/* --------------------------------------------------------------- keys --- */
static uint32_t sq_window(void) { return (uint32_t)bfm_p.arp.window * 4u * (BFM_FS / 1000u); }   /* samples */

static void sq_key(uint32_t note, uint32_t vel, uint32_t down)
{
    note &= 127u;
    if (!bfm_p.arp.on) {
        if (down)
            bfm_note_on(note, vel ? vel : 100u);
        else
            bfm_note_off(note);
        if (down)
            set_add(sq.phys, note);
        else
            set_del(sq.phys, note);
        return;
    }
    if (down) {
        int fresh = set_empty(sq.phys);
        set_add(sq.phys, note);
        if (!sq.playing) {                               /* from idle: the key plays at once */
            uint8_t pool[1] = {(uint8_t)note};
            bfm_pattern_t p;
            set_clear(sq.cset);
            set_add(sq.cset, note);
            sq_make(&p, pool, 1, sq_len(), 1);
            sq_install(&p);
            sq_start();
            sq.initial = 1;
        } else if (fresh && sq.capturing == 0) {        /* a new gesture over a playing (HOLD) loop */
            set_clear(sq.cset);
            set_add(sq.cset, note);
        } else {
            uint32_t i;
            for (i = 0; i < 4u; i++)
                sq.cset[i] = sq.capturing && fresh ? sq.cset[i] : sq.phys[i];
            set_add(sq.cset, note);
        }
        sq.capturing = 1;
        sq.cap_until = sq.now + sq_window();
    } else {
        set_del(sq.phys, note);
        if (bfm_p.arp.hold)
            return;
        if (set_empty(sq.phys)) {
            sq_stop();
            return;
        }
        { uint32_t i; for (i = 0; i < 4u; i++) sq.cset[i] = sq.phys[i]; }
        sq.capturing = 1;
        sq.cap_until = sq.now + sq_window();
    }
}

/* the capture window closed: the set's pattern, at once (the first one) or at the loop boundary */
static void sq_capture_done(void)
{
    uint8_t pool[BFM_POOL];
    uint32_t n = set_list(sq.cset, pool), i, same;
    bfm_pattern_t p;
    sq.capturing = 0;
    if (!n)
        return;
    same = n == bfm_p.pat.npool;
    for (i = 0; same && i < n; i++)
        same = pool[i] == bfm_p.pat.pool[i];
    if (same && !sq.initial)
        return;
    sq_make(&p, pool, n, sq_len(), !same);
    if (sq.initial || !sq.playing) {
        if (same)
            p = bfm_p.pat;
        bfm_p.pat = p;                                    /* the first pattern grows into the whole chord */
        sq.initial = 0;
        sq.pending = 0;
    } else {
        sq.pend = p;
        sq.pending = 1;
    }
}

/* ---------------------------------------------------------- playback --- */
static void sq_step(uint32_t spstep)            /* the next step, now; spstep: samples per step */
{
    const bfm_pattern_t *p;
    const bfm_step_t *st;
    if (sq.step == 0u && sq.pending) {
        sq_install(&sq.pend);
        sq.pending = 0;
    }
    p = &bfm_p.pat;
    if (!p->len || !p->npool) {
        sq.step = 0;
        return;
    }
    if (sq.step >= p->len)
        sq.step = 0;
    st = &p->st[sq.step];
    sq.flash = sq.step;
    if (st->flags & ST_ON) {
        uint32_t vel = (st->flags & ST_ACC) ? 127u : 92u, g;
        sq_note_stop();
        bfm_note_on_t(st->note, vel, st->fm, st->fb, st->morph, (st->flags & ST_ACC) != 0u);
        sq.cur_note = st->note;
        g = spstep * st->gate / 100u;
        sq.off_at = (st->flags & ST_TIE) ? 0u : sq.now + (g ? g : 1u);
    }
    if (++sq.step >= p->len) {
        sq.step = 0;
        sq.loops++;
    }
}

/* one clock tick */
static void sq_tick(void)
{
    uint32_t tps = RATE_TICKS[bfm_p.arp.rate % RATE_N];
    if (sq.playing && sq.tick % tps == 0u)
        sq_step((uint32_t)(((uint64_t)sq_spt_q16() * tps) >> 16));
    sq.tick++;
}

/* --------------------------------------------------------------- MIDI --- */
static uint8_t bfm_midi_in_count;                /* incoming messages (UI activity dot) */

static void sq_midi(uint32_t pkt)                /* a USB-MIDI event packet (cable 0) */
{
    uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, ch = st & 15u;
    bfm_midi_in_count++;
    if (st >= 0xF8u) {
        if (bfm_set.sync == SYNC_INT)
            return;
        if (st == 0xF8u) {
            if (sq.ext_seen && !sq.ext_lost) {
                uint32_t d = sq.now - sq.ext_last;
                if (d > 100u && d < BFM_FS) {             /* 10 .. 1100 BPM: a period worth learning */
                    uint32_t q = d << 16;
                    sq.ext_period = sq.ext_period ? sq.ext_period - (sq.ext_period >> 3) + (q >> 3) : q;
                }
            }
            sq.ext_last = sq.now;
            sq.ext_seen = 1;
            if (sq.ext_lost) {
                sq.ext_lost = 0;
                sq.ext_run = 1;                           /* a clock that comes back without a Start runs */
            }
            if (sq.ext_run)
                sq_tick();
        } else if (st == 0xFAu) {
            sq.ext_run = 1;
            sq.ext_seen = 1;
            sq.ext_lost = 0;
            sq.ext_last = sq.now;
            sq.tick = 0;
            sq.step = 0;
        } else if (st == 0xFBu) {
            sq.ext_run = 1;
            sq.ext_seen = 1;
            sq.ext_lost = 0;
            sq.ext_last = sq.now;
        } else if (st == 0xFCu) {
            sq.ext_run = 0;
            sq_note_stop();
        }
        return;
    }
    if (st < 0x80u || st >= 0xF0u)
        return;
    if (bfm_set.midi_ch && ch != bfm_set.midi_ch - 1u)
        return;
    switch (st & 0xF0u) {
    case 0x90u:
        sq_key(d1, d2, d2 != 0u);
        break;
    case 0x80u:
        sq_key(d1, 0, 0);
        break;
    case 0xB0u:
        if (d1 == 120u || d1 == 123u) {
            sq_stop();
            bfm_all_off();
            set_clear(sq.phys);
        } else if (bfm_set.cc_on) {
            if (d1 == bfm_set.cc[0])
                bfm_p.morph = (uint8_t)d2;
            if (d1 == bfm_set.cc[1])
                bfm_p.intensity = (uint8_t)d2;
            if (d1 == bfm_set.cc[2])
                bfm_p.feedback = (uint8_t)d2;
            if (d1 == bfm_set.cc[3])
                bfm_p.movement = (uint8_t)d2;
        }
        break;
    case 0xC0u:
        if (bfm_set.pc_on)
            rq_pc = (uint16_t)(d1 + 1u);
        break;
    case 0xE0u: {
        int32_t b = (int32_t)(d1 | d2 << 7) - 8192;      /* +-bend semitones, Q12 octaves */
        bfm_bend = b * (int32_t)bfm_p.bend * 4096 / (12 * 8192);
        break;
    }
    }
}

/* --------------------------------------------------------- requests --- */
static void sq_requests(void)
{
    if (rq_panic) {
        sq_stop();
        bfm_panic();
        set_clear(sq.phys);
        rq_panic = 0;
    }
    if (rq_load) {
        int keep = sq.playing && bfm_stage.arp.on && bfm_stage.arp.hold;
        sq_note_stop();
        bfm_all_off();
        bfm_p = bfm_stage;
        sq.nundo = 0;
        sq.pending = 0;
        sq.capturing = 0;
        if (!keep && sq.playing && !(bfm_p.arp.on && !set_empty(sq.phys)))
            sq_stop();
        rq_load = 0;
    }
    if (rq_arp_off) {
        sq_stop();
        bfm_all_off();
        set_clear(sq.phys);
        rq_arp_off = 0;
    }
    if (rq_play) {                                       /* ARP turned on: a held (HOLD) pattern plays at once */
        if (bfm_p.arp.on && bfm_p.arp.hold && bfm_p.pat.npool && !sq.playing)
            sq_start();
        rq_play = 0;
    }
    if (rq_regen) {
        bfm_pattern_t p;
        if (bfm_p.pat.npool && !(bfm_p.arp.locks & LK_FREEZE)) {
            uint32_t locks = bfm_p.arp.locks;
            bfm_generate(&p, bfm_p.pat.pool, bfm_p.pat.npool, sq_len(), &bfm_p.arp, bfm_seed_next(bfm_p.pat.seed),
                         locks, &bfm_p.pat);
            if (sq.playing) {
                sq.pend = p;
                sq.pending = 1;
            } else {
                sq_install(&p);
            }
        }
        rq_regen = 0;
    }
    if (rq_seed) {
        bfm_pattern_t p;
        if (bfm_p.pat.npool && !(bfm_p.arp.locks & LK_FREEZE)) {
            bfm_generate(&p, bfm_p.pat.pool, bfm_p.pat.npool, sq_len(), &bfm_p.arp, rq_seed_v, bfm_p.arp.locks,
                         &bfm_p.pat);
            if (sq.playing) {
                sq.pend = p;
                sq.pending = 1;
            } else {
                sq_install(&p);
            }
        }
        rq_seed = 0;
    }
    if (rq_len) {
        bfm_pattern_t p;
        if (bfm_p.pat.npool && sq_len() != bfm_p.pat.len) {
            /* a new pattern made for the new length (never a cut-down 16): RHYTHM / TIMBRE LOCK cannot keep
             * steps that do not exist, FREEZE neither; PITCH LOCK keeps the order of the notes */
            bfm_generate(&p, bfm_p.pat.pool, bfm_p.pat.npool, sq_len(), &bfm_p.arp, bfm_seed_next(bfm_p.pat.seed),
                         bfm_p.arp.locks & LK_PITCH, &bfm_p.pat);
            if (sq.playing && sq.step < p.len) {
                sq.pend = p;
                sq.pending = 1;
            } else {
                sq_install(&p);
                sq.step = 0;
            }
        }
        rq_len = 0;
    }
    if (rq_undo) {
        if (sq.nundo) {
            sq.pending = 0;
            bfm_p.pat = sq.undo[--sq.nundo];
            if (sq.step >= bfm_p.pat.len)
                sq.step = 0;
        }
        rq_undo = 0;
    }
}

/* ------------------------------------------------------------ the block --- */
/* the whole audio side for n samples (n <= BFM_CTL): input, clock, ARP, then the voices */
static void bfm_block(int32_t *out, uint32_t n)
{
    uint32_t spt;
    sq_requests();
    while (kq_r != kq_w) {
        uint32_t e = kq[kq_r % KQ];
        kq_r++;
        sq_key(e & 127u, (e >> 8) & 127u, (e >> 16) & 1u);
    }
    if (sq.capturing && (int32_t)(sq.now - sq.cap_until) >= 0)
        sq_capture_done();
    if (sq.playing && (!bfm_p.arp.on || (!bfm_p.arp.hold && set_empty(sq.phys))))
        sq_stop();                                       /* ARP off, or HOLD off with no key down */
    /* MIDI clock lost: no tick for 300 ms */
    if (sq.ext_seen && !sq.ext_lost && sq.now - sq.ext_last > BFM_FS * 3u / 10u) {
        sq.ext_lost = 1;
        if (bfm_set.sync == SYNC_EXT)
            sq_note_stop();
    }
    /* the internal clock (also AUTO without a clock) */
    if (!sq_ext_drives() && sq_clock_running()) {
        spt = sq_spt_q16();
        sq.frac += n << 16;
        while (sq.frac >= spt) {
            sq.frac -= spt;
            sq_tick();
        }
    }
    if (sq.cur_note >= 0 && sq.off_at && (int32_t)(sq.now - sq.off_at) >= 0) {
        bfm_note_off((uint32_t)sq.cur_note);
        sq.cur_note = -1;
        sq.off_at = 0;
    }
    bfm_render_voices(out, n);
    sq.now += n;
}

static void bfm_seq_init(void)
{
    memset(&sq, 0, sizeof sq);
    sq.cur_note = -1;
    kq_w = kq_r = 0;
}

/* main loop -> ISR: a key */
static void bfm_key_push(uint32_t note, uint32_t vel, uint32_t down)
{
    if (kq_w - kq_r < KQ) {
        kq[kq_w % KQ] = (note & 127u) | (vel & 127u) << 8 | (down ? 1u : 0u) << 16;
        RING_PUBLISH();
        kq_w++;
    }
}
