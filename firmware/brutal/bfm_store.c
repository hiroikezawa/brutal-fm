/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Hiro Ikezawa */
/* Presets and settings in flash, over storage.c (A/B sectors, commit record last, CRC-32 of the payload).
 * A bank record carries its own magic, a format version and the patch size, so a later firmware can migrate
 * an older layout instead of misreading it. Whatever comes out of flash is sanitised before it reaches the
 * DSP: every byte clamped to its range, a pattern that breaks the ARP's rules (a note outside its pool) is
 * dropped. 32 user presets in 4 banks of 8. */

#define BANK_N 8u
#define BFM_USER 32u
#define BANK_MAGIC 0x4B4E4142u           /* "BANK" */
#define BANK_VERSION 1u
#define LAST_MAGIC 0x5453414Cu           /* "LAST" */
typedef struct {
    uint32_t magic;
    uint16_t version, size;
    bfm_patch_t p[BANK_N];
} bfm_bank_t;
static bfm_bank_t st_bank BFM_POOL_SECTION;

/* every byte of a patch into its range; returns the number of bytes changed */
static uint32_t bfm_patch_sanitize(bfm_patch_t *p)
{
    uint32_t i, k, n = 0;
    uint8_t *b;
#define CL(x, hi) do { if ((x) > (hi)) { (x) = (hi); n++; } } while (0)
    p->name[BFM_NAME - 1] = 0;
    for (i = 0; i < BFM_NAME - 1u && p->name[i]; i++)
        if (p->name[i] < 32 || p->name[i] > 126) {
            p->name[i] = '?';
            n++;
        }
    CL(p->category, 5u);
    CL(p->mode, MODE_N - 1u);
    CL(p->poly, 1u);
    CL(p->algo, BFM_NALGO - 1u);
    CL(p->lfo_shape, LFO_N - 1u);
    for (b = &p->morph; b <= &p->dly_mix; b++)
        if (b != &p->lfo_shape)
            CL(*b, 127u);
    CL(p->bend, 24u);
    for (k = 0; k < 2u; k++) {
        bfm_sound_t *s = &p->s[k];
        for (i = 0; i < BFM_NOP; i++)
            CL(s->ratio[i], BFM_NRATIO - 1u);
        for (b = s->fine; b < s->rsv; b++)
            if (b < s->ratio || b >= s->ratio + BFM_NOP)
                CL(*b, 127u);
    }
    CL(p->arp.on, 1u);
    CL(p->arp.hold, 1u);
    CL(p->arp.length, 1u);
    CL(p->arp.rate, RATE_N - 1u);
    CL(p->arp.density, 127u);
    CL(p->arp.complexity, 127u);
    CL(p->arp.gatevar, 127u);
    CL(p->arp.timbrevar, 127u);
    if (p->arp.gate < 1u || p->arp.gate > 100u) {
        p->arp.gate = 60;
        n++;
    }
    CL(p->arp.window, 127u);
    CL(p->arp.locks, 15u);
    if (p->pat.npool && !bfm_pat_valid(&p->pat)) {   /* never play a pattern that breaks the pool rule */
        memset(&p->pat, 0, sizeof p->pat);
        p->pat.len = p->arp.length ? 16 : 8;
        n++;
    }
    if (!p->pat.npool)
        p->pat.len = p->arp.length ? 16 : 8;
#undef CL
    return n;
}

#if BFM_FLASH
static int bank_read(uint32_t b)                 /* 0: st_bank holds bank b (or an empty one if none was saved) */
{
    int n = st_load(OBJ_BANK0 + b, &st_bank, sizeof st_bank);
    if (n == (int)sizeof st_bank && st_bank.magic == BANK_MAGIC && st_bank.version == BANK_VERSION &&
        st_bank.size == sizeof(bfm_patch_t))
        return 0;
    memset(&st_bank, 0, sizeof st_bank);
    st_bank.magic = BANK_MAGIC;
    st_bank.version = BANK_VERSION;
    st_bank.size = sizeof(bfm_patch_t);
    return n < 0 ? 1 : 2;                          /* 1 none saved, 2 another layout (ignored) */
}

/* user preset slot 0..31 -> *p; -1 when the slot is empty */
static int store_user_get(uint32_t slot, bfm_patch_t *p)
{
    if (slot >= BFM_USER)
        return -1;
    bank_read(slot / BANK_N);
    if (!st_bank.p[slot % BANK_N].name[0])
        return -1;
    *p = st_bank.p[slot % BANK_N];
    bfm_patch_sanitize(p);
    return 0;
}

static int store_user_name(uint32_t slot, char *name)   /* the slot's name, "" if empty */
{
    name[0] = 0;
    if (slot >= BFM_USER)
        return -1;
    bank_read(slot / BANK_N);
    memcpy(name, st_bank.p[slot % BANK_N].name, BFM_NAME);
    name[BFM_NAME - 1] = 0;
    return name[0] ? 0 : -1;
}

static int store_user_put(uint32_t slot, const bfm_patch_t *p)
{
    if (slot >= BFM_USER)
        return -1;
    bank_read(slot / BANK_N);
    st_bank.p[slot % BANK_N] = *p;
    if (!st_bank.p[slot % BANK_N].name[0])
        str_cpy(st_bank.p[slot % BANK_N].name, "USER", BFM_NAME);
    return st_save(OBJ_BANK0 + slot / BANK_N, &st_bank, sizeof st_bank);
}

static int store_settings_load(bfm_settings_t *s)
{
    bfm_settings_t t;
    if (st_load(OBJ_SETTINGS, &t, sizeof t) != (int)sizeof t || t.magic != BFM_SETTINGS_MAGIC)
        return -1;
    *s = t;
    return 0;
}
static int store_settings_save(const bfm_settings_t *s) { return st_save(OBJ_SETTINGS, s, sizeof *s); }

static struct { uint32_t magic; uint16_t version, size; uint16_t preset; uint16_t rsv; bfm_patch_t p; } st_last
    BFM_POOL_SECTION;
static int store_last_save(const bfm_patch_t *p, uint32_t preset)
{
    st_last.magic = LAST_MAGIC;
    st_last.version = BANK_VERSION;
    st_last.size = sizeof(bfm_patch_t);
    st_last.preset = (uint16_t)preset;
    st_last.p = *p;
    return st_save(OBJ_LAST, &st_last, sizeof st_last);
}
static int store_last_load(bfm_patch_t *p, uint32_t *preset)
{
    if (st_load(OBJ_LAST, &st_last, sizeof st_last) != (int)sizeof st_last || st_last.magic != LAST_MAGIC ||
        st_last.version != BANK_VERSION || st_last.size != sizeof(bfm_patch_t))
        return -1;
    *p = st_last.p;
    *preset = st_last.preset;
    bfm_patch_sanitize(p);
    return 0;
}
#else
static int store_user_get(uint32_t slot, bfm_patch_t *p) { (void)slot; (void)p; return -1; }
static int store_user_name(uint32_t slot, char *name) { (void)slot; name[0] = 0; return -1; }
static int store_user_put(uint32_t slot, const bfm_patch_t *p) { (void)slot; (void)p; return -1; }
static int store_settings_load(bfm_settings_t *s) { (void)s; return -1; }
static int store_settings_save(const bfm_settings_t *s) { (void)s; return -1; }
static int store_last_save(const bfm_patch_t *p, uint32_t preset) { (void)p; (void)preset; return -1; }
static int store_last_load(bfm_patch_t *p, uint32_t *preset) { (void)p; (void)preset; return -1; }
#endif

static void bfm_settings_default(bfm_settings_t *s)
{
    memset(s, 0, sizeof *s);
    s->magic = BFM_SETTINGS_MAGIC;
    s->sync = SYNC_AUTO;
    s->midi_ch = 0;
    s->cc_on = 1;
    s->pc_on = 1;
    s->cc[0] = 1;                /* MORPH: the mod wheel */
    s->cc[1] = 74;               /* INTENSITY: brightness */
    s->cc[2] = 71;               /* FEEDBACK: resonance */
    s->cc[3] = 76;               /* MOVEMENT: vibrato rate */
    s->quality = 1;
    s->bpm = 120 - 40;
    s->palette = 0;
}

static void bfm_settings_sanitize(bfm_settings_t *s)
{
    uint32_t i;
    if (s->sync >= SYNC_N) s->sync = SYNC_AUTO;
    if (s->midi_ch > 16u) s->midi_ch = 0;
    s->cc_on = s->cc_on != 0;
    s->pc_on = s->pc_on != 0;
    for (i = 0; i < 4u; i++)
        s->cc[i] &= 127u;
    s->quality = s->quality != 0;
    if (s->bpm > 215u) s->bpm = 80;
}
