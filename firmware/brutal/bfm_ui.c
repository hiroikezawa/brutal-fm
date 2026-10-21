/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Hiro Ikezawa */
/* BRUTAL FM user interface: everything on the FM-1's own controls, no editor needed.
 *
 *   KNOB 1-4   the four parameters of the page          SELECT     tempo (BPM)
 *   PRESETS    browse and load (F01-F40, U01-U32)       ALGORITHM  write a routing template into the matrix
 *   MASTER     volume (the pot)                         keys       play (27 keys, F3..G5 + octave)
 *   HOME  PERFORM        ARP   ARP GEN > TIME > LOCKS > MORE      SEQ   the pattern, step by step
 *   EDIT  OP1-4 > OP4> .. OP1> (matrix) > FIXED > PHASE           ENV   ENV OP1-4
 *   LFO   MOD 1-3        FX    FOLD > DRIVE/DELAY > VOICE         GLO   SYNC/MIDI > CC > SYSTEM > TOOLS
 *   SAVE  the SAVE page; SAVE again writes the slot
 *   SCL   the sound being edited: A > B > A+B (MORPH crossfades A and B)
 *   PLAY  ARP on / off        REC  tap: REGENERATE, hold: UNDO        HOME held 1 s: all notes off
 *   OCT- / OCT+   octave; both: reset; both held 5 s: update mode (UBOOT)
 *   ARP held + FX / SCL / ENV / LFO / EDIT / GLO: HOLD / FREEZE / RHYTHM / PITCH / TIMBRE LOCK / UNDO
 * The platform (bfm_main.c on the FM-1, brutal_web.c in the browser, the host tests) calls ui_button,
 * ui_encoder, ui_note_key, ui_frame and reads ui_led[]; drawing goes through gfx.c's canvas and lcd_*. */

enum { B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC, B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };
#define NKEYS 27u
#define KEY_BASE 53u             /* F3 */
#define NPRESETS (BFM_NFACTORY + BFM_USER)

#ifndef BFM_VERSION
#define BFM_VERSION "v0.1.0"
#endif

/* ---------------------------------------------------------- parameters --- */
enum { W_PATCH, W_SND, W_SET, W_LOCK, W_SPEC, W_NONE };
enum { F_NUM, F_BIP, F_RATIO, F_FINE, F_FIXED, F_PHASE, F_ATK, F_DEC, F_ONOFF, F_MODE, F_LFO, F_SYNC, F_RATE, F_LEN,
       F_QUAL, F_POLY, F_CH, F_CC, F_WIN, F_PCT, F_SEED, F_BPM, F_MAT, F_STEP, F_SNOTE, F_GAIN, F_DLY, F_COLOR,
       F_ACT, F_BEND, F_DEG, F_VOICES };
enum { SP_SEED, SP_BPM, SP_STEP, SP_SNOTE, SP_SGATE, SP_SFM, SP_ACT_AB, SP_ACT_BA, SP_ACT_SWAP, SP_ACT_INIT,
       SP_SLOT, SP_CPOS, SP_CHR };
typedef struct {
    const char *name;
    uint8_t where;
    uint16_t off;                /* byte offset (W_PATCH, W_SND, W_SET), lock bit, SP_* */
    uint8_t lo, hi, fmt;
    uint8_t perop;               /* W_SND: add the page's operator to the offset */
} prm_t;
#define OFP(f) ((uint16_t)__builtin_offsetof(bfm_patch_t, f))
#define OFS(f) ((uint16_t)__builtin_offsetof(bfm_sound_t, f))
#define OFG(f) ((uint16_t)__builtin_offsetof(bfm_settings_t, f))
static const prm_t PRM[] = {
#define P_MORPH 0
    {"MORPH", W_PATCH, OFP(morph), 0, 127, F_NUM, 0},
    {"INTENS", W_PATCH, OFP(intensity), 0, 127, F_NUM, 0},
    {"FEEDBK", W_PATCH, OFP(feedback), 0, 127, F_NUM, 0},
    {"MOVE", W_PATCH, OFP(movement), 0, 127, F_NUM, 0},
#define P_DENS 4
    {"DENSITY", W_PATCH, OFP(arp.density), 0, 127, F_NUM, 0},
    {"COMPLEX", W_PATCH, OFP(arp.complexity), 0, 127, F_NUM, 0},
    {"G.VAR", W_PATCH, OFP(arp.gatevar), 0, 127, F_NUM, 0},
    {"T.VAR", W_PATCH, OFP(arp.timbrevar), 0, 127, F_NUM, 0},
#define P_LEN 8
    {"LENGTH", W_PATCH, OFP(arp.length), 0, 1, F_LEN, 0},
    {"RATE", W_PATCH, OFP(arp.rate), 0, RATE_N - 1, F_RATE, 0},
    {"GATE", W_PATCH, OFP(arp.gate), 1, 100, F_PCT, 0},
    {"WINDOW", W_PATCH, OFP(arp.window), 0, 127, F_WIN, 0},
#define P_HOLD 12
    {"HOLD", W_PATCH, OFP(arp.hold), 0, 1, F_ONOFF, 0},
    {"FREEZE", W_LOCK, LK_FREEZE, 0, 1, F_ONOFF, 0},
    {"R.LOCK", W_LOCK, LK_RHYTHM, 0, 1, F_ONOFF, 0},
    {"P.LOCK", W_LOCK, LK_PITCH, 0, 1, F_ONOFF, 0},
#define P_TLOCK 16
    {"T.LOCK", W_LOCK, LK_TIMBRE, 0, 1, F_ONOFF, 0},
    {"SEED", W_SPEC, SP_SEED, 0, 0, F_SEED, 0},
    {"ARP", W_PATCH, OFP(arp.on), 0, 1, F_ONOFF, 0},
    {"SYNC", W_SET, OFG(sync), 0, SYNC_N - 1, F_SYNC, 0},
#define P_STEP 20
    {"STEP", W_SPEC, SP_STEP, 0, 0, F_STEP, 0},
    {"NOTE", W_SPEC, SP_SNOTE, 0, 0, F_SNOTE, 0},
    {"S.GATE", W_SPEC, SP_SGATE, 0, 0, F_PCT, 0},
    {"S.FM", W_SPEC, SP_SFM, 0, 0, F_BIP, 0},
#define P_RATIO 24
    {"RATIO", W_SND, OFS(ratio), 0, BFM_NRATIO - 1, F_RATIO, 1},
    {"FINE", W_SND, OFS(fine), 0, 127, F_FINE, 1},
    {"LEVEL", W_SND, OFS(level), 0, 127, F_NUM, 1},
    {"OUT", W_SND, OFS(out), 0, 127, F_NUM, 1},
#define P_MAT 28                 /* the four destinations of the page's source operator */
    {"> OP1", W_SND, OFS(mat[0]), 0, 127, F_MAT, 0},
    {"> OP2", W_SND, OFS(mat[1]), 0, 127, F_MAT, 0},
    {"> OP3", W_SND, OFS(mat[2]), 0, 127, F_MAT, 0},
    {"> OP4", W_SND, OFS(mat[3]), 0, 127, F_MAT, 0},
#define P_FIX 32
    {"FIX 1", W_SND, OFS(fixed[0]), 0, 127, F_FIXED, 0},
    {"FIX 2", W_SND, OFS(fixed[1]), 0, 127, F_FIXED, 0},
    {"FIX 3", W_SND, OFS(fixed[2]), 0, 127, F_FIXED, 0},
    {"FIX 4", W_SND, OFS(fixed[3]), 0, 127, F_FIXED, 0},
#define P_PH 36
    {"PH 1", W_SND, OFS(phase[0]), 0, 127, F_PHASE, 0},
    {"PH 2", W_SND, OFS(phase[1]), 0, 127, F_PHASE, 0},
    {"PH 3", W_SND, OFS(phase[2]), 0, 127, F_PHASE, 0},
    {"PH 4", W_SND, OFS(phase[3]), 0, 127, F_PHASE, 0},
#define P_ENV 40
    {"ATK", W_SND, OFS(atk), 0, 127, F_ATK, 1},
    {"DEC", W_SND, OFS(dec), 0, 127, F_DEC, 1},
    {"SUS", W_SND, OFS(sus), 0, 127, F_NUM, 1},
    {"REL", W_SND, OFS(rel), 0, 127, F_DEC, 1},
#define P_MOD1 44
    {"SHAPE", W_PATCH, OFP(lfo_shape), 0, LFO_N - 1, F_LFO, 0},
    {"RATE", W_PATCH, OFP(lfo_rate), 0, 127, F_NUM, 0},
    {">INTENS", W_PATCH, OFP(mod_int), 0, 127, F_BIP, 0},
    {">FOLD", W_PATCH, OFP(mod_fold), 0, 127, F_BIP, 0},
    {"RATE", W_PATCH, OFP(drift_rate), 0, 127, F_NUM, 0},
    {">MORPH", W_PATCH, OFP(mod_morph), 0, 127, F_BIP, 0},
    {">FDBK", W_PATCH, OFP(mod_fb), 0, 127, F_BIP, 0},
    {"VIBRATO", W_PATCH, OFP(mod_pitch), 0, 127, F_BIP, 0},
    {"VELO", W_PATCH, OFP(vel_sens), 0, 127, F_NUM, 0},
    {"BEND", W_PATCH, OFP(bend), 0, 24, F_BEND, 0},
    {"GLIDE", W_PATCH, OFP(glide), 0, 127, F_NUM, 0},
    {"VOICE", W_PATCH, OFP(poly), 0, 1, F_POLY, 0},
#define P_FX 56
    {"FOLD", W_SND, OFS(fold), 0, 127, F_NUM, 0},
    {"SYMM", W_SND, OFS(sym), 0, 127, F_BIP, 0},
    {"BIAS", W_SND, OFS(bias), 0, 127, F_BIP, 0},
    {"MIX", W_SND, OFS(fmix), 0, 127, F_NUM, 0},
    {"DRIVE", W_PATCH, OFP(drive), 0, 127, F_NUM, 0},
    {"CRUSH", W_PATCH, OFP(crush), 0, 127, F_NUM, 0},
    {"D.TIME", W_PATCH, OFP(dly_time), 0, 127, F_DLY, 0},
    {"D.FDBK", W_PATCH, OFP(dly_fb), 0, 127, F_NUM, 0},
    {"D.MIX", W_PATCH, OFP(dly_mix), 0, 127, F_NUM, 0},
    {"MODE", W_PATCH, OFP(mode), 0, MODE_N - 1, F_MODE, 0},
    {"CHAR", W_SND, OFS(character), 0, 127, F_NUM, 0},
    {"GAIN", W_SND, OFS(gain), 0, 127, F_GAIN, 0},
#define P_GLO 68
    {"SYNC", W_SET, OFG(sync), 0, SYNC_N - 1, F_SYNC, 0},
    {"BPM", W_SPEC, SP_BPM, 0, 0, F_BPM, 0},
    {"MIDI CH", W_SET, OFG(midi_ch), 0, 16, F_CH, 0},
    {"QUALITY", W_SET, OFG(quality), 0, 1, F_QUAL, 0},
    {"MORPH", W_SET, OFG(cc[0]), 0, 127, F_CC, 0},
    {"INTENS", W_SET, OFG(cc[1]), 0, 127, F_CC, 0},
    {"FEEDBK", W_SET, OFG(cc[2]), 0, 127, F_CC, 0},
    {"MOVE", W_SET, OFG(cc[3]), 0, 127, F_CC, 0},
    {"CC IN", W_SET, OFG(cc_on), 0, 1, F_ONOFF, 0},
    {"PC IN", W_SET, OFG(pc_on), 0, 1, F_ONOFF, 0},
    {"COLOR", W_SET, OFG(palette), 0, UI_NPALETTES, F_COLOR, 0},
    {"VOICES", W_SET, OFG(voices), 2, BFM_MAXV, F_VOICES, 0},
#define P_TOOLS 80
    {"A>B", W_SPEC, SP_ACT_AB, 0, 0, F_ACT, 0},
    {"B>A", W_SPEC, SP_ACT_BA, 0, 0, F_ACT, 0},
    {"SWAP", W_SPEC, SP_ACT_SWAP, 0, 0, F_ACT, 0},
    {"INIT", W_SPEC, SP_ACT_INIT, 0, 0, F_ACT, 0},
#define P_SAVE 84
    {"SLOT", W_SPEC, SP_SLOT, 0, 0, F_NUM, 0},
    {"POS", W_SPEC, SP_CPOS, 0, 0, F_NUM, 0},
    {"CHAR", W_SPEC, SP_CHR, 0, 0, F_NUM, 0},
    {"", W_NONE, 0, 0, 0, F_NUM, 0},
};

/* -------------------------------------------------------------- pages --- */
enum { G_HOME, G_ARP, G_SEQ, G_EDIT, G_ENV, G_LFO, G_FX, G_GLO, G_SAVE, NG };
enum { V_PERF, V_ARP, V_SEQ, V_OP, V_MAT, V_ENV, V_MOD, V_FOLD, V_FX, V_GLO, V_SAVE };
typedef struct {
    const char *title;
    uint8_t group, view, op;     /* op: the operator of an OP / ENV / matrix page */
    uint8_t p[4];
} page_t;
static const page_t PAGES[] = {
    {"PERFORM", G_HOME, V_PERF, 0, {P_MORPH, P_MORPH + 1, P_MORPH + 2, P_MORPH + 3}},
    {"ARP GENERATE", G_ARP, V_ARP, 0, {P_DENS, P_DENS + 1, P_DENS + 2, P_DENS + 3}},
    {"ARP TIME", G_ARP, V_ARP, 0, {P_LEN, P_LEN + 1, P_LEN + 2, P_LEN + 3}},
    {"ARP LOCKS", G_ARP, V_ARP, 0, {P_HOLD, P_HOLD + 1, P_HOLD + 2, P_HOLD + 3}},
    {"ARP MORE", G_ARP, V_ARP, 0, {P_TLOCK, P_TLOCK + 1, P_TLOCK + 2, P_TLOCK + 3}},
    {"SEQ STEPS", G_SEQ, V_SEQ, 0, {P_STEP, P_STEP + 1, P_STEP + 2, P_STEP + 3}},
    {"OP1", G_EDIT, V_OP, 0, {P_RATIO, P_RATIO + 1, P_RATIO + 2, P_RATIO + 3}},
    {"OP2", G_EDIT, V_OP, 1, {P_RATIO, P_RATIO + 1, P_RATIO + 2, P_RATIO + 3}},
    {"OP3", G_EDIT, V_OP, 2, {P_RATIO, P_RATIO + 1, P_RATIO + 2, P_RATIO + 3}},
    {"OP4", G_EDIT, V_OP, 3, {P_RATIO, P_RATIO + 1, P_RATIO + 2, P_RATIO + 3}},
    {"OP4 MODULATES", G_EDIT, V_MAT, 3, {P_MAT, P_MAT + 1, P_MAT + 2, P_MAT + 3}},
    {"OP3 MODULATES", G_EDIT, V_MAT, 2, {P_MAT, P_MAT + 1, P_MAT + 2, P_MAT + 3}},
    {"OP2 MODULATES", G_EDIT, V_MAT, 1, {P_MAT, P_MAT + 1, P_MAT + 2, P_MAT + 3}},
    {"OP1 MODULATES", G_EDIT, V_MAT, 0, {P_MAT, P_MAT + 1, P_MAT + 2, P_MAT + 3}},
    {"FIXED FREQ", G_EDIT, V_OP, 0, {P_FIX, P_FIX + 1, P_FIX + 2, P_FIX + 3}},
    {"PHASE RESET", G_EDIT, V_OP, 0, {P_PH, P_PH + 1, P_PH + 2, P_PH + 3}},
    {"ENV OP1", G_ENV, V_ENV, 0, {P_ENV, P_ENV + 1, P_ENV + 2, P_ENV + 3}},
    {"ENV OP2", G_ENV, V_ENV, 1, {P_ENV, P_ENV + 1, P_ENV + 2, P_ENV + 3}},
    {"ENV OP3", G_ENV, V_ENV, 2, {P_ENV, P_ENV + 1, P_ENV + 2, P_ENV + 3}},
    {"ENV OP4", G_ENV, V_ENV, 3, {P_ENV, P_ENV + 1, P_ENV + 2, P_ENV + 3}},
    {"MOD LFO", G_LFO, V_MOD, 0, {P_MOD1, P_MOD1 + 1, P_MOD1 + 2, P_MOD1 + 3}},
    {"MOD DRIFT", G_LFO, V_MOD, 0, {P_MOD1 + 4, P_MOD1 + 5, P_MOD1 + 6, P_MOD1 + 7}},
    {"MOD VOICE", G_LFO, V_MOD, 0, {P_MOD1 + 8, P_MOD1 + 9, P_MOD1 + 10, P_MOD1 + 11}},
    {"WAVEFOLDER", G_FX, V_FOLD, 0, {P_FX, P_FX + 1, P_FX + 2, P_FX + 3}},
    {"DRIVE DELAY", G_FX, V_FX, 0, {P_FX + 4, P_FX + 5, P_FX + 6, P_FX + 7}},
    {"MODE", G_FX, V_FX, 0, {P_FX + 8, P_FX + 9, P_FX + 10, P_FX + 11}},
    {"SYNC MIDI", G_GLO, V_GLO, 0, {P_GLO, P_GLO + 1, P_GLO + 2, P_GLO + 3}},
    {"MIDI CC", G_GLO, V_GLO, 0, {P_GLO + 4, P_GLO + 5, P_GLO + 6, P_GLO + 7}},
    {"SYSTEM", G_GLO, V_GLO, 0, {P_GLO + 8, P_GLO + 9, P_GLO + 10, P_GLO + 11}},
    {"SOUND TOOLS", G_GLO, V_GLO, 0, {P_TOOLS, P_TOOLS + 1, P_TOOLS + 2, P_TOOLS + 3}},
    {"SAVE", G_SAVE, V_SAVE, 0, {P_SAVE, P_SAVE + 1, P_SAVE + 2, P_SAVE + 3}},
};
#define NPAGES (sizeof PAGES / sizeof PAGES[0])
static const uint8_t GROUP_BTN[NG] = {B_HOME, B_ARP, B_SEQ, B_EDIT, B_ENV, B_LFO, B_FX, B_GLO, B_SAVE};

/* ------------------------------------------------------------------ state --- */
static struct {
    uint8_t page;
    uint8_t target;              /* 0 A, 1 B, 2 A+B */
    int8_t oct;
    uint8_t dirty;               /* 1 cards, 2 bottom, 4 header, 8 everything */
    uint32_t popup_until;
    char popup[24], popup2[24];
    int16_t preset_cur, preset_sel;
    uint32_t browse_t;
    uint8_t browsing;
    uint8_t save_slot, cpos;
    char save_name[BFM_NAME];
    uint32_t held, combo;        /* buttons down; buttons used in a combo (no tap on release) */
    uint32_t down_t[NB];
    uint32_t keys;               /* note keys down */
    uint8_t key_note[NKEYS];     /* the note each key started (an octave change between down and up) */
    uint8_t step_cur;            /* SEQ page cursor */
    uint8_t modified;
    uint8_t shown_step, shown_play;
    uint32_t last_draw;
    uint16_t cpu_q8;
    uint8_t uboot;               /* the countdown (main loop) */
    int32_t act_acc[4];
    uint8_t sel_page[NG];        /* the last page of each group */
    uint8_t settings_dirty;
    uint32_t settings_t;
} ui;
static uint8_t ui_led[NB + NKEYS];   /* 0 off, 1 the glow, 3 mid, 2 lit (the platform maps it) */
enum { LED_OFF, LED_GLOW, LED_LIT, LED_MID };
static uint16_t ui_scope[128];       /* the output, for the PERFORM view (filled by the audio side) */
static volatile uint8_t ui_scope_w;

static uint32_t ui_now(void);        /* the platform's milliseconds */
static void ui_popup(const char *a, const char *b)
{
    str_cpy(ui.popup, a, sizeof ui.popup);
    str_cpy(ui.popup2, b ? b : "", sizeof ui.popup2);
    ui.popup_until = ui_now() + 900u;
    ui.dirty |= 2;
}

/* --------------------------------------------------------- param access --- */
static bfm_sound_t *ui_snd(uint32_t k) { return &bfm_p.s[k]; }
static const page_t *ui_pg(void) { return &PAGES[ui.page % NPAGES]; }

static int32_t prm_get(uint32_t id)
{
    const prm_t *d = &PRM[id];
    const page_t *pg = ui_pg();
    switch (d->where) {
    case W_PATCH: return ((const uint8_t *)&bfm_p)[d->off];
    case W_SND: {
        uint32_t off = d->off + (d->perop ? pg->op : 0u);
        const bfm_sound_t *sn = ui_snd(ui.target == 1u);
        if (d->fmt == F_MAT) {                            /* -127..127: the polarity bit gives the sign */
            uint32_t e = pg->op * BFM_NOP + (uint32_t)(id - P_MAT);
            int32_t m = sn->mat[e];
            return (sn->neg[e >> 3] & (1u << (e & 7u))) ? -m : m;
        }
        return ((const uint8_t *)sn)[off];
    }
    case W_SET: return ((const uint8_t *)&bfm_set)[d->off];
    case W_LOCK: return (bfm_p.arp.locks & d->off) != 0u;
    case W_SPEC:
        switch (d->off) {
        case SP_SEED: return (int32_t)(bfm_p.pat.seed & 0xFFFFu);
        case SP_BPM: return 40 + bfm_set.bpm;
        case SP_STEP: return ui.step_cur;
        case SP_SNOTE: {
            const bfm_step_t *s = &bfm_p.pat.st[ui.step_cur % BFM_MAXSTEP];
            return (s->flags & ST_ON) ? s->idx : -1;
        }
        case SP_SGATE: return bfm_p.pat.st[ui.step_cur % BFM_MAXSTEP].gate;
        case SP_SFM: return bfm_p.pat.st[ui.step_cur % BFM_MAXSTEP].fm + 64;
        case SP_SLOT: return ui.save_slot;
        case SP_CPOS: return ui.cpos;
        case SP_CHR: return ui.save_name[ui.cpos % (BFM_NAME - 1u)];
        }
        return 0;
    }
    return 0;
}

static void ui_sound_set(uint32_t off, uint32_t v)   /* the edit target(s) */
{
    if (ui.target != 1u)
        ((uint8_t *)ui_snd(0))[off] = (uint8_t)v;
    if (ui.target != 0u)
        ((uint8_t *)ui_snd(1))[off] = (uint8_t)v;
}

static void ui_tool(uint32_t which)
{
    switch (which) {
    case SP_ACT_AB: bfm_p.s[1] = bfm_p.s[0]; ui_popup("COPIED", "A > B"); break;
    case SP_ACT_BA: bfm_p.s[0] = bfm_p.s[1]; ui_popup("COPIED", "B > A"); break;
    case SP_ACT_SWAP: { bfm_sound_t t = bfm_p.s[0]; bfm_p.s[0] = bfm_p.s[1]; bfm_p.s[1] = t; ui_popup("SWAPPED", "A / B"); break; }
    case SP_ACT_INIT: {
        bfm_sound_t s = BFM_FACTORY[0].s[0];
        uint32_t k;
        memset(s.mat, 0, sizeof s.mat);
        for (k = 0; k < BFM_NOP; k++) {
            s.ratio[k] = 4;      /* 1 */
            s.fine[k] = 64; s.fixed[k] = 0; s.level[k] = 118; s.out[k] = 0; s.phase[k] = 1;
            s.atk[k] = 0; s.dec[k] = 70; s.sus[k] = 100; s.rel[k] = 50;
        }
        s.out[0] = 118;
        s.mat[1 * 4 + 0] = 60;
        s.fold = 0; s.sym = 64; s.bias = 64; s.fmix = 0; s.character = 0; s.gain = 100;
        if (ui.target != 1u) bfm_p.s[0] = s;
        if (ui.target != 0u) bfm_p.s[1] = s;
        ui_popup("INIT", ui.target == 2u ? "A+B" : ui.target ? "B" : "A");
        break;
    }
    }
    ui.modified = 1;
}

static void prm_set(uint32_t id, int32_t v)
{
    const prm_t *d = &PRM[id];
    const page_t *pg = ui_pg();
    if (d->fmt == F_MAT) {                                /* a matrix amount: -127..127 */
        uint32_t e = pg->op * BFM_NOP + (uint32_t)(id - P_MAT), k;
        v = v < -127 ? -127 : v > 127 ? 127 : v;
        for (k = 0; k < 2u; k++) {
            bfm_sound_t *sn = ui_snd(k);
            if ((k == 0u && ui.target == 1u) || (k == 1u && ui.target == 0u))
                continue;
            sn->mat[e] = (uint8_t)(v < 0 ? -v : v);
            if (v < 0)
                sn->neg[e >> 3] |= (uint8_t)(1u << (e & 7u));
            else
                sn->neg[e >> 3] &= (uint8_t)~(1u << (e & 7u));
        }
        ui.modified = 1;
        ui.dirty |= 3;
        return;
    }
    if (d->where != W_SPEC && d->where != W_NONE) {
        if (v < d->lo) v = d->lo;
        if (v > d->hi) v = d->hi;
    }
    switch (d->where) {
    case W_PATCH:
        if (((uint8_t *)&bfm_p)[d->off] == (uint8_t)v)
            return;
        ((uint8_t *)&bfm_p)[d->off] = (uint8_t)v;
        if (d->off == OFP(arp.length))
            rq_len = 1;
        if (d->off == OFP(arp.on)) {
            if (v)
                rq_play = 1;
            else
                rq_arp_off = 1;
        }
        if (d->off == OFP(poly))
            rq_panic = 1;
        break;
    case W_SND: {
        uint32_t off = d->off + (d->perop ? pg->op : 0u);
        if (d->fmt == F_MAT)
            off += pg->op * BFM_NOP;
        ui_sound_set(off, (uint32_t)v);
        break;
    }
    case W_SET:
        ((uint8_t *)&bfm_set)[d->off] = (uint8_t)v;
        if (d->off == OFG(palette))
            palette_set(v == 0 ? NPALETTES : (uint32_t)v - 1u);
        if (d->off == OFG(quality))
            rq_panic = 1;
        ui.settings_dirty = 1;
        ui.settings_t = ui_now();
        ui.dirty |= 8;
        break;
    case W_LOCK:
        if (v)
            bfm_p.arp.locks |= (uint8_t)d->off;
        else
            bfm_p.arp.locks &= (uint8_t)~d->off;
        break;
    case W_SPEC:
        switch (d->off) {
        case SP_BPM:
            bfm_set.bpm = (uint8_t)(v < 40 ? 0 : v > 255 ? 215 : v - 40);
            ui.settings_dirty = 1;
            ui.settings_t = ui_now();
            break;
        case SP_STEP:
            ui.step_cur = (uint8_t)(((v % (int32_t)bfm_p.pat.len) + bfm_p.pat.len) % bfm_p.pat.len);
            break;
        case SP_SNOTE: {          /* -1 rest, 0..n-1 a note of the pool: nothing else exists */
            bfm_step_t *s = &bfm_p.pat.st[ui.step_cur % BFM_MAXSTEP];
            int32_t n = bfm_p.pat.npool;
            if (!n)
                break;
            if (v < -1) v = -1;
            if (v >= n) v = n - 1;
            if (v < 0) {
                s->flags = 0;
            } else {
                s->idx = (uint8_t)v;
                s->note = bfm_p.pat.pool[v];
                if (!s->gate)
                    s->gate = bfm_p.arp.gate;
                RING_PUBLISH();
                s->flags |= ST_ON;
            }
            break;
        }
        case SP_SGATE: {
            bfm_step_t *s = &bfm_p.pat.st[ui.step_cur % BFM_MAXSTEP];
            s->gate = (uint8_t)(v < 1 ? 1 : v > 100 ? 100 : v);
            break;
        }
        case SP_SFM: {
            bfm_step_t *s = &bfm_p.pat.st[ui.step_cur % BFM_MAXSTEP];
            v -= 64;
            s->fm = (int8_t)(v < -64 ? -64 : v > 63 ? 63 : v);
            break;
        }
        case SP_SLOT: ui.save_slot = (uint8_t)((v % (int32_t)BFM_USER + BFM_USER) % BFM_USER); break;
        case SP_CPOS: ui.cpos = (uint8_t)((v % (BFM_NAME - 1) + (BFM_NAME - 1)) % (BFM_NAME - 1)); break;
        case SP_CHR: {
            static const char CS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-+.#/*";
            uint32_t n = sizeof CS - 1u, i;
            char *c = &ui.save_name[ui.cpos % (BFM_NAME - 1u)];
            for (i = 0; i < n && CS[i] != *c; i++)
                ;
            i = (uint32_t)(((int32_t)(i % n) + (v - *c > 0 ? 1 : -1) + (int32_t)n) % (int32_t)n);
            *c = CS[i];
            break;
        }
        }
        break;
    }
    if (d->where != W_SET && d->where != W_NONE && !(d->where == W_SPEC && d->off >= SP_SLOT))
        ui.modified = 1;
    ui.dirty |= 3;
}

/* the knob turned by steps */
static void prm_turn(uint32_t id, int32_t steps)
{
    const prm_t *d = &PRM[id];
    if (d->where == W_NONE)
        return;
    if (d->where == W_SPEC) {
        switch (d->off) {
        case SP_SEED:              /* a new seed: the pattern it generates (the LOCKs kept) */
            if (bfm_p.pat.npool) {
                rq_seed_v = bfm_p.pat.seed + (uint32_t)steps;
                rq_seed = 1;
            }
            ui.dirty |= 3;
            return;
        case SP_ACT_AB: case SP_ACT_BA: case SP_ACT_SWAP: case SP_ACT_INIT: {
            int32_t *a = &ui.act_acc[d->off - SP_ACT_AB];
            *a += steps;
            if (*a >= 4) {         /* a deliberate turn to the right */
                *a = 0;
                ui_tool(d->off);
            } else if (*a < 0) {
                *a = 0;
            }
            ui.dirty |= 3;
            return;
        }
        case SP_CHR:
            prm_set(id, ui.save_name[ui.cpos % (BFM_NAME - 1u)] + (steps > 0 ? 1 : -1));
            return;
        }
    }
    if (d->hi - d->lo > 40 && (steps > 1 || steps < -1))
        steps *= 2;                /* a fast turn covers the range quicker */
    prm_set(id, prm_get(id) + steps);
}

/* ---------------------------------------------------------- formatting --- */
static void u2s(char *b, uint32_t v)
{
    char t[12];
    uint32_t n = 0;
    do {
        t[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void i2s(char *b, int32_t v)
{
    if (v < 0) {
        *b++ = '-';
        v = -v;
    } else if (v > 0) {
        *b++ = '+';
    }
    u2s(b, (uint32_t)v);
}
static void str_cat(char *d, const char *s, uint32_t max)
{
    uint32_t i = 0;
    while (i < max - 1u && d[i])
        i++;
    while (i < max - 1u && *s)
        d[i++] = *s++;
    d[i] = 0;
}
static void note_name(char *b, uint32_t n)
{
    static const char *const NN[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char o[4];
    str_cpy(b, NN[n % 12u], 4);
    i2s(o, (int32_t)(n / 12u) - 1);
    str_cat(b, o[0] == '+' ? o + 1 : o, 8);
}
static void ms_str(char *b, uint32_t us10)     /* tenths of ms -> "0.3ms" / "12ms" / "1.2s" */
{
    char t[12];
    if (us10 < 100u) {
        u2s(b, us10 / 10u);
        str_cat(b, ".", 12);
        u2s(t, us10 % 10u);
        str_cat(b, t, 12);
        str_cat(b, "ms", 12);
    } else if (us10 < 10000u) {
        u2s(b, us10 / 10u);
        str_cat(b, "ms", 12);
    } else {
        u2s(b, us10 / 10000u);
        str_cat(b, ".", 12);
        u2s(t, us10 / 1000u % 10u);
        str_cat(b, t, 12);
        str_cat(b, "s", 12);
    }
}
static void prm_str(uint32_t id, char *b)
{
    const prm_t *d = &PRM[id];
    int32_t v = prm_get(id);
    char t[16];
    b[0] = 0;
    switch (d->fmt) {
    case F_NUM: u2s(b, (uint32_t)v); break;
    case F_BIP: i2s(b, v - 64); break;
    case F_RATIO: str_cpy(b, BFM_RATIO_NAME[v % BFM_NRATIO], 12); str_cat(b, "x", 12); break;
    case F_FINE: i2s(b, (v - 64) * 2); str_cat(b, "c", 12); break;
    case F_FIXED:
        if (!v) {
            str_cpy(b, "RATIO", 12);
        } else {
            uint32_t hz10 = BFM_FIXED_SHOW[v & 127];
            if (hz10 < 1000u) {
                u2s(b, hz10 / 10u);
                str_cat(b, ".", 12);
                u2s(t, hz10 % 10u);
                str_cat(b, t, 12);
            } else {
                u2s(b, hz10 / 10u);
            }
            str_cat(b, "Hz", 12);
        }
        break;
    case F_PHASE:
        if (!v) {
            str_cpy(b, "FREE", 12);
        } else {
            u2s(b, (uint32_t)(v - 1) * 360u / 127u);
            str_cat(b, "deg", 12);
        }
        break;
    case F_ATK: ms_str(b, BFM_ATK_SHOW[v & 127]); break;
    case F_DEC: ms_str(b, BFM_DEC_SHOW[v & 127]); break;
    case F_ONOFF: str_cpy(b, v ? "ON" : "OFF", 12); break;
    case F_MODE: str_cpy(b, (const char *[]){"METAL", "GROWL", "CHAOS", "NOISE"}[v % MODE_N], 12); break;
    case F_LFO: str_cpy(b, (const char *[]){"SINE", "TRI", "SAW", "SQUARE", "S&H", "CHAOS"}[v % LFO_N], 12); break;
    case F_SYNC: str_cpy(b, (const char *[]){"INT", "EXT", "AUTO"}[v % SYNC_N], 12); break;
    case F_RATE: str_cpy(b, RATE_NAME[v % RATE_N], 12); break;
    case F_LEN: str_cpy(b, v ? "16 STEP" : "8 STEP", 12); break;
    case F_QUAL: str_cpy(b, v ? "HQ 2x" : "ECO", 12); break;
    case F_POLY: str_cpy(b, v ? "POLY" : "MONO", 12); break;
    case F_CH: if (!v) str_cpy(b, "OMNI", 12); else u2s(b, (uint32_t)v); break;
    case F_CC: str_cpy(b, "CC", 12); u2s(t, (uint32_t)v); str_cat(b, t, 12); break;
    case F_WIN: u2s(b, (uint32_t)v * 4u); str_cat(b, "ms", 12); break;
    case F_PCT: u2s(b, (uint32_t)v); str_cat(b, "%", 12); break;
    case F_SEED: {
        static const char H[] = "0123456789ABCDEF";
        uint32_t k, s = bfm_p.pat.seed;
        for (k = 0; k < 4u; k++)
            b[k] = H[(s >> (12u - 4u * k)) & 15u];
        b[4] = 0;
        if (!bfm_p.pat.npool)
            str_cpy(b, "----", 12);
        break;
    }
    case F_BPM: u2s(b, (uint32_t)v); break;
    case F_MAT:
        if (id - P_MAT == ui_pg()->op)
            str_cpy(b, v ? "FB " : "FB", 12);
        if (!v) {
            str_cat(b, "-", 12);
        } else {
            if (v < 0)
                str_cat(b, "-", 12);
            u2s(t, (uint32_t)(v < 0 ? -v : v));
            str_cat(b, t, 12);
        }
        break;
    case F_STEP: u2s(b, (uint32_t)v + 1u); str_cat(b, "/", 12); u2s(t, bfm_p.pat.len); str_cat(b, t, 12); break;
    case F_SNOTE:
        if (!bfm_p.pat.npool) str_cpy(b, "NO KEYS", 12);
        else if (v < 0) str_cpy(b, "REST", 12);
        else note_name(b, bfm_p.pat.pool[v % BFM_POOL]);
        break;
    case F_GAIN: i2s(b, ((v - 100) * 375 + (v >= 100 ? 500 : -500)) / 1000); str_cat(b, "dB", 12); break;
    case F_DLY: u2s(b, (64u + (uint32_t)v * 127u) * 1000u / BFM_FS); str_cat(b, "ms", 12); break;
    case F_COLOR: str_cpy(b, v == 0 ? "BRUTAL" : UI_PALETTES[(v - 1) % UI_NPALETTES].name, 12); break;
    case F_ACT: str_cpy(b, "TURN >", 12); break;
    case F_BEND: u2s(b, (uint32_t)v); str_cat(b, " st", 12); break;
    case F_VOICES:
        u2s(b, (uint32_t)v);
        if (bfm_set.quality) {
            str_cat(b, ">", 12);
            u2s(t, bfm_maxv());
            str_cat(b, t, 12);
        }
        break;
    }
    if (d->where == W_SPEC && d->off == SP_SLOT) {
        str_cpy(b, "U", 12);
        if (v < 9) str_cat(b, "0", 12);
        u2s(t, (uint32_t)v + 1u);
        str_cat(b, t, 12);
    }
    if (d->where == W_SPEC && d->off == SP_CPOS) {
        u2s(b, (uint32_t)v + 1u);
    }
    if (d->where == W_SPEC && d->off == SP_CHR) {
        b[0] = v == ' ' ? '_' : (char)v;
        b[1] = 0;
    }
}

/* the bar of a card: 0..1000 of its range, bipolar from the centre */
static int32_t prm_bar(uint32_t id, int *bip)
{
    const prm_t *d = &PRM[id];
    int32_t v = prm_get(id);
    *bip = d->fmt == F_BIP || d->fmt == F_FINE || d->fmt == F_MAT || (d->where == W_SPEC && d->off == SP_SFM);
    if (d->fmt == F_MAT)
        return (v + 127) * 1000 / 254;
    if (d->where == W_SPEC) {
        switch (d->off) {
        case SP_BPM: return (v - 40) * 1000 / 215;
        case SP_STEP: return bfm_p.pat.len ? (v + 1) * 1000 / bfm_p.pat.len : 0;
        case SP_SGATE: return v * 10;
        case SP_SFM: return v * 1000 / 127;
        case SP_SNOTE: return bfm_p.pat.npool && v >= 0 ? (v + 1) * 1000 / bfm_p.pat.npool : 0;
        case SP_SLOT: return (v + 1) * 1000 / (int32_t)BFM_USER;
        case SP_ACT_AB: case SP_ACT_BA: case SP_ACT_SWAP: case SP_ACT_INIT: return ui.act_acc[d->off - SP_ACT_AB] * 250;
        default: return 0;
        }
    }
    if (d->where == W_NONE || d->hi == d->lo)
        return 0;
    return (v - d->lo) * 1000 / (d->hi - d->lo);
}

/* ------------------------------------------------------------- presets --- */
static void preset_label(char *b, int32_t i)
{
    char t[4];
    b[0] = i < BFM_NFACTORY ? 'F' : 'U';
    b[1] = 0;
    i = i < BFM_NFACTORY ? i + 1 : i - BFM_NFACTORY + 1;
    if (i < 10)
        str_cat(b, "0", 8);
    u2s(t, (uint32_t)i);
    str_cat(b, t, 8);
}

static int ui_preset_fetch(int32_t i, bfm_patch_t *p)   /* 0 ok */
{
    if (i < 0 || i >= (int32_t)NPRESETS)
        return -1;
    if (i < BFM_NFACTORY) {
        *p = BFM_FACTORY[i];
        return 0;
    }
    return store_user_get((uint32_t)(i - BFM_NFACTORY), p);
}

static void ui_preset_name(int32_t i, char *name)
{
    if (i < BFM_NFACTORY)
        str_cpy(name, BFM_FACTORY[i].name, BFM_NAME);
    else if (store_user_name((uint32_t)(i - BFM_NFACTORY), name))
        str_cpy(name, "(EMPTY)", BFM_NAME);
}

static int ui_preset_load(int32_t i)
{
    if (rq_load)
        return -1;                 /* the last one is still being taken */
    if (ui_preset_fetch(i, &bfm_stage))
        return -1;
    RING_PUBLISH();
    rq_load = 1;
    ui.preset_cur = (int16_t)i;
    ui.modified = 0;
    ui.step_cur = 0;
    str_cpy(ui.save_name, bfm_stage.name, BFM_NAME);
    ui.dirty |= 8;
    return 0;
}

static void ui_save(void)
{
    bfm_patch_t p = bfm_p;
    int rc;
    str_cpy(p.name, ui.save_name, BFM_NAME);
    {   /* trailing blanks off the name */
        int32_t k = BFM_NAME - 2;
        while (k > 0 && (p.name[k] == ' ' || !p.name[k]))
            p.name[k--] = 0;
    }
    if (!p.name[0] || p.name[0] == ' ')
        str_cpy(p.name, "USER", BFM_NAME);
    p.category = 5;
    rc = store_user_put(ui.save_slot, &p);
    if (!rc) {
        char t[8];
        str_cpy(bfm_p.name, p.name, BFM_NAME);
        ui.preset_cur = (int16_t)(BFM_NFACTORY + ui.save_slot);
        ui.modified = 0;
        preset_label(t, ui.preset_cur);
        ui_popup("SAVED", t);
        store_last_save(&bfm_p, (uint32_t)ui.preset_cur);
    } else {
        ui_popup("SAVE FAILED", rc == -7 ? "VERIFY" : "FLASH");
    }
}

/* --------------------------------------------------------------- input --- */
static void ui_page_to(uint32_t p)
{
    ui.page = (uint8_t)(p % NPAGES);
    ui.sel_page[PAGES[ui.page].group] = ui.page;
    ui.dirty |= 8;
}

static void ui_group(uint32_t g)                 /* a page button tapped: its group, or the next page in it */
{
    uint32_t i, p = ui.page;
    if (PAGES[p].group == g) {
        for (i = 1; i <= NPAGES; i++)
            if (PAGES[(p + i) % NPAGES].group == g) {
                ui_page_to((p + i) % NPAGES);
                return;
            }
        return;
    }
    p = ui.sel_page[g];
    if (PAGES[p].group != g)
        for (p = 0; p < NPAGES && PAGES[p].group != g; p++)
            ;
    ui_page_to(p);
}

static void ui_toggle_lock(uint32_t bit, const char *name)
{
    bfm_p.arp.locks ^= (uint8_t)bit;
    ui_popup(name, (bfm_p.arp.locks & bit) ? "ON" : "OFF");
    ui.dirty |= 3;
}

static void ui_button(uint32_t b, uint32_t down)
{
    uint32_t now = ui_now(), bit = 1u << b;
    if (b >= NB)
        return;
    if (down) {
        ui.held |= bit;
        ui.down_t[b] = now;
        ui.combo &= ~bit;
        if (b == B_OCTDN || b == B_OCTUP) {          /* octave on the press; both: reset */
            uint32_t both = (1u << B_OCTDN) | (1u << B_OCTUP);
            if ((ui.held & both) == both) {
                ui.oct = 0;
                ui.combo |= both;
            } else if (b == B_OCTDN && ui.oct > -2) {
                ui.oct--;
            } else if (b == B_OCTUP && ui.oct < 2) {
                ui.oct++;
            }
            {
                char t[8];
                i2s(t, ui.oct);
                ui_popup("OCTAVE", ui.oct ? t : "0");
            }
            return;
        }
        if ((ui.held & (1u << B_ARP)) && b != B_ARP) {   /* ARP held + a top-row button */
            ui.combo |= (1u << B_ARP) | bit;
            switch (b) {
            case B_FX: bfm_p.arp.hold ^= 1u; ui_popup("HOLD", bfm_p.arp.hold ? "ON" : "OFF"); break;
            case B_SCL: ui_toggle_lock(LK_FREEZE, "FREEZE"); break;
            case B_ENV: ui_toggle_lock(LK_RHYTHM, "RHYTHM LOCK"); break;
            case B_LFO: ui_toggle_lock(LK_PITCH, "PITCH LOCK"); break;
            case B_EDIT: ui_toggle_lock(LK_TIMBRE, "TIMBRE LOCK"); break;
            case B_GLO: rq_undo = 1; ui_popup("UNDO", ""); break;
            default: ui.combo &= ~bit; break;
            }
            ui.modified = 1;
            return;
        }
        return;
    }
    ui.held &= ~bit;
    if (ui.combo & bit) {
        ui.combo &= ~bit;
        return;
    }
    if (b == B_OCTDN || b == B_OCTUP)
        return;
    switch (b) {
    case B_SCL:
        ui.target = (uint8_t)((ui.target + 1u) % 3u);
        ui_popup("EDITING", ui.target == 2u ? "A + B" : ui.target ? "SOUND B" : "SOUND A");
        ui.dirty |= 8;
        break;
    case B_PLAY:
        prm_set(P_TLOCK + 2, !bfm_p.arp.on);
        ui_popup("ARP", bfm_p.arp.on ? "ON" : "OFF");
        break;
    case B_REC:
        if (now - ui.down_t[B_REC] >= 600u) {
            rq_undo = 1;
            ui_popup("UNDO", "");
        } else if (bfm_p.arp.locks & LK_FREEZE) {
            ui_popup("FROZEN", "FREEZE IS ON");
        } else if (!bfm_p.pat.npool) {
            ui_popup("NO PATTERN", "HOLD KEYS FIRST");
        } else {
            rq_regen = 1;
            ui_popup("REGENERATE", sq.playing ? "AT THE LOOP" : "");
        }
        ui.modified = 1;
        break;
    case B_HOME:
        if (now - ui.down_t[B_HOME] >= 1000u) {
            rq_panic = 1;
            ui_popup("ALL NOTES OFF", "");
            break;
        }
        ui_group(G_HOME);
        break;
    case B_SAVE:
        if (PAGES[ui.page].group == G_SAVE)
            ui_save();
        else
            ui_group(G_SAVE);
        break;
    default: {
        uint32_t g;
        for (g = 0; g < NG; g++)
            if (GROUP_BTN[g] == b)
                ui_group(g);
        break;
    }
    }
}

static void ui_encoder(uint32_t e, int32_t steps)
{
    if (!steps || e >= NE)
        return;
    switch (e) {
    case EN_SELECT: {
        char t[8];
        prm_set(P_GLO + 1, 40 + bfm_set.bpm + steps);
        u2s(t, 40u + bfm_set.bpm);
        ui_popup("TEMPO", t);
        break;
    }
    case EN_PRESET: {
        char name[BFM_NAME], lab[8];
        int32_t s = (ui.browsing ? ui.preset_sel : ui.preset_cur) + steps;
        s = (s % (int32_t)NPRESETS + (int32_t)NPRESETS) % (int32_t)NPRESETS;
        ui.preset_sel = (int16_t)s;
        ui.browsing = 1;
        ui.browse_t = ui_now();
        ui_preset_name(s, name);
        preset_label(lab, s);
        str_cat(lab, " ", 8);
        ui_popup(lab, name);
        break;
    }
    case EN_ALGO: {
        uint32_t a = (uint32_t)(((int32_t)bfm_p.algo + steps) % BFM_NALGO + BFM_NALGO) % BFM_NALGO, k;
        bfm_p.algo = (uint8_t)a;
        for (k = 0; k < 16u; k++)
            ui_sound_set(__builtin_offsetof(bfm_sound_t, mat) + k, BFM_ALGO_MAT[a][k]);
        for (k = 0; k < 4u; k++)
            ui_sound_set(__builtin_offsetof(bfm_sound_t, out) + k, BFM_ALGO_OUT[a][k]);
        for (k = 0; k < 2u; k++)                          /* a template is all positive */
            ui_sound_set(__builtin_offsetof(bfm_sound_t, neg) + k, 0);
        ui_popup("ALGORITHM", BFM_ALGO_NAME[a]);
        ui.modified = 1;
        ui.dirty |= 3;
        break;
    }
    default:
        prm_turn(ui_pg()->p[e - EN_K1], steps);
        break;
    }
}

static void ui_note_key(uint32_t k, uint32_t down)
{
    uint32_t n;
    if (k >= NKEYS)
        return;
    if (down) {
        int32_t v = (int32_t)KEY_BASE + (int32_t)k + 12 * ui.oct;
        n = (uint32_t)(v < 0 ? 0 : v > 127 ? 127 : v);
        ui.key_note[k] = (uint8_t)n;
        ui.keys |= 1u << k;
        bfm_key_push(n, 100, 1);
    } else {
        if (!(ui.keys & (1u << k)))
            return;
        ui.keys &= ~(1u << k);
        bfm_key_push(ui.key_note[k], 0, 0);
    }
}

/* ---------------------------------------------------------------- LEDs --- */
static void ui_leds(uint32_t now)
{
    /* every button and key glows, so a black FM-1 can be found on a dark stage (as Felucca, #35); a state that
     * is on but not the focus sits at the mid level over the glow, the focus and what sounds are lit */
    uint32_t i, g = PAGES[ui.page].group, on;
    for (i = 0; i < NB + NKEYS; i++)
        ui_led[i] = LED_GLOW;
    for (i = 0; i < NG; i++)
        if (i == g)
            ui_led[GROUP_BTN[i]] = LED_LIT;
    if (ui.target)
        ui_led[B_SCL] = ui.target == 1u ? LED_LIT : LED_MID;
    if (bfm_p.arp.on)
        ui_led[B_PLAY] = sq.playing ? LED_LIT : LED_MID;
    if (sq.pending)
        ui_led[B_REC] = (now / 120u) & 1u ? LED_LIT : LED_GLOW;
    else if (bfm_p.pat.npool && bfm_p.arp.on)
        ui_led[B_REC] = LED_MID;
    if (ui.held & (1u << B_ARP)) {                    /* ARP held: the toggles' states on their buttons */
        ui_led[B_FX] = bfm_p.arp.hold ? LED_LIT : LED_MID;
        ui_led[B_SCL] = bfm_p.arp.locks & LK_FREEZE ? LED_LIT : LED_MID;
        ui_led[B_ENV] = bfm_p.arp.locks & LK_RHYTHM ? LED_LIT : LED_MID;
        ui_led[B_LFO] = bfm_p.arp.locks & LK_PITCH ? LED_LIT : LED_MID;
        ui_led[B_EDIT] = bfm_p.arp.locks & LK_TIMBRE ? LED_LIT : LED_MID;
        ui_led[B_GLO] = sq.nundo ? LED_MID : LED_GLOW;
    }
    if (ui.oct < 0)
        ui_led[B_OCTDN] = LED_LIT;
    if (ui.oct > 0)
        ui_led[B_OCTUP] = LED_LIT;
    /* keys: the pool at the mid level, the ARP's sounding note and the keys held lit, the rest glow */
    for (i = 0; i < NKEYS; i++) {
        int32_t n = (int32_t)KEY_BASE + (int32_t)i + 12 * ui.oct;
        uint32_t k;
        on = LED_GLOW;
        if (bfm_p.arp.on)
            for (k = 0; k < bfm_p.pat.npool; k++)
                if (bfm_p.pat.pool[k] == n)
                    on = LED_MID;
        if (sq.cur_note == n || (ui.keys & (1u << i)))
            on = LED_LIT;
        ui_led[NB + i] = (uint8_t)on;
    }
}

/* ------------------------------------------------------------- drawing --- */
#define HDR_H 22
#define CARD_Y 24
#define CARD_H 92
#define BOT_Y 120
#define BOT_H 120

static void draw_header(void)
{
    char t[16], b[12];
    const page_t *pg = ui_pg();
    int32_t x;
    cv_begin(240, HDR_H, T_BG);
    cv_text(4, 3, &AF_M, pg->title, T_TEXT);
    x = 236;
    /* tempo */
    u2s(t, sq_bpm_x10() / 10u);
    if (bfm_set.sync != SYNC_INT && sq.ext_seen && !sq.ext_lost)
        str_cat(t, " EXT", 16);
    x = cv_text_r(x, 5, &AF_S, t, T_MID, T_BG) - 8;
    /* the edit target */
    str_cpy(b, ui.target == 2u ? "A+B" : ui.target ? "B" : "A", 12);
    {
        int32_t w = text_w(&AF_S, b) + 10;
        x -= w;
        cv_rrect(x, 3, w, 16, 4, ui.target ? T_ACCENT : T_SEL, T_BG);
        cv_text_in(x, 4, w, &AF_S, b, T_INK, ui.target ? T_ACCENT : T_SEL);
        x -= 6;
    }
    if (bfm_p.arp.on) {
        str_cpy(b, bfm_p.arp.hold ? "HOLD" : "ARP", 12);
        x = cv_text_r(x, 5, &AF_S, b, sq.playing ? T_THEME : T_DIM, T_BG) - 6;
    }
    cv_rect(0, HDR_H - 1, 240, 1, T_LINE);
    cv_blit(0, 0);
}

static void draw_cards(void)
{
    const page_t *pg = ui_pg();
    uint32_t i;
    cv_begin(240, CARD_H, T_BG);
    for (i = 0; i < 4u; i++) {
        uint32_t id = pg->p[i];
        int32_t x = (int32_t)i * 60 + 2, w = 56, bar, bip;
        char v[16];
        if (PRM[id].where == W_NONE)
            continue;
        cv_rrect(x, 0, w, CARD_H - 2, 6, T_SURF, T_BG);
        cv_text_fit(x + 4, 6, &AF_S, PRM[id].name, T_MID, T_SURF, w - 8);
        prm_str(id, v);
        cv_text_fit(x + 4, 30, &AF_M, v, T_TEXT, T_SURF, w - 6);
        bar = prm_bar(id, &bip);
        bar = bar < 0 ? 0 : bar > 1000 ? 1000 : bar;
        cv_rect(x + 5, CARD_H - 18, w - 10, 6, T_RAISE);
        if (bip) {
            int32_t c = x + 5 + (w - 10) / 2, e = x + 5 + (w - 10) * bar / 1000;
            if (e >= c)
                cv_rect(c, CARD_H - 18, e - c + 1, 6, T_THEME);
            else
                cv_rect(e, CARD_H - 18, c - e, 6, T_THEME);
        } else if (bar) {
            cv_rect(x + 5, CARD_H - 18, (w - 10) * bar / 1000 + 1, 6, T_THEME);
        }
        /* the knob number */
        u2s(v, i + 1u);
        cv_text(x + w - 12, CARD_H - 34, &AF_S, v, T_DIM);
    }
    cv_blit(0, CARD_Y);
}

/* the ARP pattern: 8 or 16 cells, each a note name, a gate bar, accent / tie marks, the playhead */
static void draw_pattern(int32_t y0, int32_t h, int cursor)
{
    const bfm_pattern_t *p = &bfm_p.pat;
    uint32_t i, len = p->len ? p->len : 8u, cols = len > 8u ? 8u : len, rows = (len + cols - 1u) / cols;
    int32_t cw = 240 / (int32_t)cols, ch = h / (int32_t)rows;
    for (i = 0; i < len; i++) {
        const bfm_step_t *st = &p->st[i];
        int32_t x = (int32_t)(i % cols) * cw + 2, y = y0 + (int32_t)(i / cols) * ch + 2, w = cw - 4, hh = ch - 4;
        int play = sq.playing && sq.flash == i;
        uint16_t bg = play ? T_SEL : T_SURF, fg = play ? T_INK : T_TEXT;
        char nm[8];
        cv_rrect(x, y, w, hh, 4, bg, T_BG);
        if (cursor && i == ui.step_cur)
            cv_frame(x, y, w, hh, T_ACCENT);
        if (!p->npool) {
            cv_text_in(x, y + 6, w, &AF_S, "-", T_DIM, bg);
            continue;
        }
        if (st->flags & ST_ON) {
            note_name(nm, st->note);
            cv_text_in(x, y + 4, w, &AF_S, nm, fg, bg);
            cv_rect(x + 3, y + hh - 7, (w - 6) * st->gate / 100 + 1, 3, play ? T_INK : T_THEME);
            if (st->flags & ST_ACC)
                cv_rect(x + w - 7, y + 3, 4, 4, T_ACCENT);
            if (st->flags & ST_TIE)
                cv_rect(x + w - 4, y + hh - 7, 4, 3, T_ACCENT);
            {   /* the timbre offset: a tick left of the gate bar */
                int32_t fm = st->fm, hgt = fm < 0 ? -fm : fm;
                hgt = hgt * (hh - 24) / 64;
                if (hgt)
                    cv_rect(x + 2, y + hh - 10 - hgt, 2, hgt, fm < 0 ? T_DIM : T_ACCENT);
            }
        } else {
            cv_text_in(x, y + 4, w, &AF_S, ".", T_DIM, bg);
        }
    }
}

static void draw_footer(int32_t y)
{
    char t[24], lab[8];
    preset_label(lab, ui.preset_cur);
    str_cpy(t, lab, sizeof t);
    str_cat(t, " ", sizeof t);
    str_cat(t, bfm_p.name, sizeof t);
    if (ui.modified)
        str_cat(t, "*", sizeof t);
    cv_text(4, y, &AF_S, t, T_MID);
    cv_text_r(236, y, &AF_S, (const char *[]){"METAL", "GROWL", "CHAOS", "NOISE"}[bfm_p.mode % MODE_N], T_THEME,
              T_BG);
}

static void draw_bottom(uint32_t now)
{
    const page_t *pg = ui_pg();
    uint32_t i;
    cv_begin(240, BOT_H, T_BG);
    if (now < ui.popup_until) {
        cv_rrect(10, 10, 220, 80, 8, T_SURF, T_BG);
        cv_text_in(10, 22, 220, &AF_S, ui.popup, T_MID, T_SURF);
        cv_text_in(10, 44, 220, &AF_L, ui.popup2[0] ? ui.popup2 : ui.popup, T_TEXT, T_SURF);
        draw_footer(BOT_H - 18);
        cv_blit(0, BOT_Y);
        return;
    }
    switch (pg->view) {
    case V_PERF: {
        int32_t m = bfm_p.morph, x;
        cv_text(6, 2, &AF_S, "A", T_MID);
        cv_text_r(234, 2, &AF_S, "B", T_MID, T_BG);
        cv_rect(20, 8, 200, 4, T_RAISE);
        x = 20 + m * 196 / 127;
        cv_rect(x, 4, 4, 12, T_ACCENT);
        /* the scope: 128 samples over 220 px */
        for (i = 1; i < 120u; i++) {
            int32_t a = (int16_t)ui_scope[(ui_scope_w + i) & 127u], b = (int16_t)ui_scope[(ui_scope_w + i + 1u) & 127u];
            cv_line(10 + (int32_t)i * 220 / 120 - 2, 52 - a * 32 / 32768, 10 + (int32_t)(i + 1u) * 220 / 120 - 2,
                    52 - b * 32 / 32768, T_THEME);
        }
        break;
    }
    case V_ARP:
    case V_SEQ:
        draw_pattern(0, 82, pg->view == V_SEQ);
        {
            char t[48], nm[8];
            uint32_t k;
            t[0] = 0;
            for (k = 0; k < bfm_p.pat.npool && k < 8u; k++) {
                note_name(nm, bfm_p.pat.pool[k]);
                str_cat(t, nm, sizeof t);
                str_cat(t, " ", sizeof t);
            }
            if (bfm_p.pat.npool > 8u)
                str_cat(t, "..", sizeof t);
            if (!bfm_p.pat.npool)
                str_cpy(t, bfm_p.arp.on ? "PLAY KEYS" : "PLAY: ARP ON", sizeof t);
            cv_text(4, 84, &AF_S, t, T_MID);
            t[0] = 0;
            if (bfm_p.arp.locks & LK_FREEZE) str_cat(t, "FRZ ", sizeof t);
            if (bfm_p.arp.locks & LK_RHYTHM) str_cat(t, "R ", sizeof t);
            if (bfm_p.arp.locks & LK_PITCH) str_cat(t, "P ", sizeof t);
            if (bfm_p.arp.locks & LK_TIMBRE) str_cat(t, "T ", sizeof t);
            if (sq.pending) str_cat(t, "NEXT", sizeof t);
            cv_text_r(236, 84, &AF_S, t, T_ACCENT, T_BG);
        }
        break;
    case V_OP:
    case V_MAT: {
        /* the matrix: rows = source, columns = destination; the page's row highlighted */
        const bfm_sound_t *s = ui_snd(ui.target == 1u);
        uint32_t r, c;
        for (r = 0; r < 4u; r++) {
            char t[4] = {'O', 'P', (char)('4' - r), 0};
            uint32_t src = 3u - r;
            cv_text(8, 4 + (int32_t)r * 20, &AF_S, t, pg->view == V_MAT && pg->op == src ? T_ACCENT : T_MID);
            for (c = 0; c < 4u; c++) {
                uint32_t e = src * 4u + c, v = s->mat[e], neg = (s->neg[e >> 3] >> (e & 7u)) & 1u;
                int32_t x = 48 + (int32_t)c * 30, y = 2 + (int32_t)r * 20, sz = (int32_t)v * 16 / 127;
                cv_rect(x, y, 18, 18, src == c ? T_TINT : T_SURF);
                if (sz)
                    cv_rect(x + 9 - sz / 2, y + 9 - sz / 2, sz + 1, sz + 1, neg ? T_MID : src == c ? T_ACCENT : T_THEME);
                if (sz && neg)
                    cv_rect(x + 4, y + 8, 10, 2, T_BG);       /* a minus through a negative amount */
            }
            {   /* ratio, out */
                char v[12];
                if (s->fixed[src])
                    str_cpy(v, "FIX", 12);
                else {
                    str_cpy(v, BFM_RATIO_NAME[s->ratio[src] % BFM_NRATIO], 12);
                    str_cat(v, "x", 12);
                }
                cv_text(170, 4 + (int32_t)r * 20, &AF_S, v, s->out[src] ? T_TEXT : T_MID);
                if (s->out[src])
                    cv_rect(222, 8 + (int32_t)r * 20, 10, 6, T_THEME);
            }
        }
        cv_text(48, 82, &AF_S, "1   2   3   4", T_DIM);
        cv_text_r(236, 82, &AF_S, BFM_ALGO_NAME[bfm_p.algo % BFM_NALGO], T_MID, T_BG);
        break;
    }
    case V_ENV: {
        /* the envelope: A D S R as a shape */
        const bfm_sound_t *s = ui_snd(ui.target == 1u);
        uint32_t op = pg->op;
        int32_t a = 6 + s->atk[op] * 50 / 127, d = 6 + s->dec[op] * 50 / 127, r = 6 + s->rel[op] * 50 / 127;
        int32_t sus = 80 - s->sus[op] * 70 / 127, x0 = 10;
        cv_line(x0, 80, x0 + a, 10, T_THEME);
        cv_line(x0 + a, 10, x0 + a + d, sus, T_THEME);
        cv_line(x0 + a + d, sus, x0 + a + d + 50, sus, T_THEME);
        cv_line(x0 + a + d + 50, sus, x0 + a + d + 50 + r, 80, T_THEME);
        cv_rect(0, 81, 240, 1, T_LINE);
        break;
    }
    case V_FOLD: {
        /* the folder's transfer curve over -1..1 */
        const bfm_sound_t *s = ui_snd(ui.target == 1u);
        int32_t px = -1, py = 0, fg = 256 + s->fold * 14, sy = 256 + (64 - s->sym) * 3, bias = (s->bias - 64) * 300;
        int32_t mix = s->fmix * 258;
        for (i = 0; i <= 200u; i += 2u) {
            int32_t x = ((int32_t)i - 100) * 328, u = ((x * fg) >> 8) + bias, y;
            if (u < 0)
                u = (u * sy) >> 8;
            y = bfm_sin((uint32_t)u << 15);
            y = x + (int32_t)(((int64_t)(y - x) * mix) >> 15);
            y = 42 - y * 38 / 32768;
            if (px >= 0)
                cv_line(px, py, 20 + (int32_t)i, y, T_THEME);
            px = 20 + (int32_t)i;
            py = y;
        }
        cv_rect(20, 42, 200, 1, T_LINE);
        break;
    }
    case V_GLO: {
        char t[40], v[12];
        str_cpy(t, "MIDI ", sizeof t);
        str_cat(t, bfm_set.sync == SYNC_INT ? "INT CLOCK" : sq.ext_seen && !sq.ext_lost ? (sq.ext_run ? "EXT RUN" : "EXT STOP")
                                                       : "NO CLOCK", sizeof t);
        cv_text(6, 4, &AF_S, t, T_TEXT);
        str_cpy(t, "VOICES ", sizeof t);
        u2s(v, bfm_voices_on());
        str_cat(t, v, sizeof t);
        str_cat(t, "/", sizeof t);
        u2s(v, bfm_maxv());
        str_cat(t, v, sizeof t);
        str_cat(t, "  CPU ", sizeof t);
        u2s(v, ui.cpu_q8 * 100u / 256u);
        str_cat(t, v, sizeof t);
        str_cat(t, "%", sizeof t);
        cv_text(6, 24, &AF_S, t, T_TEXT);
        str_cpy(t, "BRUTAL FM ", sizeof t);
        str_cat(t, BFM_VERSION, sizeof t);
        cv_text(6, 44, &AF_S, t, T_MID);
        cv_text(6, 62, &AF_S, "OCT- OCT+ HELD 5S: UPDATE MODE", T_DIM);
        break;
    }
    case V_SAVE: {
        char t[24], nm[BFM_NAME], lab[8];
        uint32_t k;
        preset_label(lab, BFM_NFACTORY + ui.save_slot);
        ui_preset_name(BFM_NFACTORY + ui.save_slot, nm);
        str_cpy(t, lab, sizeof t);
        str_cat(t, " NOW: ", sizeof t);
        str_cat(t, nm, sizeof t);
        cv_text(6, 4, &AF_S, t, T_MID);
        for (k = 0; k < BFM_NAME - 1u; k++) {
            char c[2] = {ui.save_name[k] ? ui.save_name[k] : ' ', 0};
            int32_t x = 6 + (int32_t)k * 19;
            cv_rect(x, 26, 17, 26, k == ui.cpos ? T_SEL : T_SURF);
            cv_text_in(x, 30, 17, &AF_M, c[0] == ' ' ? "_" : c, k == ui.cpos ? T_INK : T_TEXT,
                       k == ui.cpos ? T_SEL : T_SURF);
        }
        cv_text(6, 62, &AF_S, "SAVE AGAIN: WRITE THE SLOT", T_ACCENT);
        break;
    }
    case V_MOD:
    case V_FX:
    default: {
        /* the modulation and effect amounts as a row of meters */
        static const uint8_t SHOW[8] = {P_MOD1 + 2, P_MOD1 + 3, P_MOD1 + 5, P_MOD1 + 6, P_MOD1 + 7, P_FX + 4, P_FX + 5,
                                        P_FX + 8};
        static const char *const SN[8] = {"INT", "FOLD", "MRPH", "FB", "PTCH", "DRV", "CRSH", "DLY"};
        for (i = 0; i < 8u; i++) {
            int bip;
            int32_t b = prm_bar(SHOW[i], &bip), x = 8 + (int32_t)i * 29, h;
            cv_rect(x, 6, 20, 64, T_SURF);
            if (bip) {
                h = (b - 500) * 32 / 500;
                if (h >= 0)
                    cv_rect(x, 38 - h, 20, h + 1, T_THEME);
                else
                    cv_rect(x, 38, 20, -h, T_THEME);
            } else {
                h = b * 64 / 1000;
                cv_rect(x, 70 - h, 20, h, T_THEME);
            }
            cv_text_in(x - 4, 74, 28, &AF_S, SN[i], T_MID, T_BG);
        }
        break;
    }
    }
    draw_footer(BOT_H - 18);
    cv_blit(0, BOT_Y);
}

/* ------------------------------------------------------------ the frame --- */
static void ui_init(void)
{
    memset(&ui, 0, sizeof ui);
    ui.dirty = 8;
    ui.preset_cur = 0;
    str_cpy(ui.save_name, bfm_p.name, BFM_NAME);
    palette_set(bfm_set.palette == 0 ? NPALETTES : (uint32_t)bfm_set.palette - 1u);
}

/* ~60 times a second from the main loop: pending loads, the LEDs, what changed on the screen */
static void ui_frame(void)
{
    uint32_t now = ui_now();
    static uint32_t popup_was, last_bot;
    if (ui.browsing && now - ui.browse_t >= 200u) {   /* the preset browsed to loads when the knob rests */
        ui.browsing = 0;
        if (ui.preset_sel != ui.preset_cur && ui_preset_load(ui.preset_sel))
            ui_popup("EMPTY SLOT", "");
    }
    if (rq_pc) {                                      /* MIDI Program Change: factory 0..39, user 40..71 */
        uint32_t pc = rq_pc - 1u;
        rq_pc = 0;
        if (pc < NPRESETS)
            ui_preset_load((int32_t)pc);
    }
    if (ui.settings_dirty && now - ui.settings_t > 1500u && !sq.playing) {
        ui.settings_dirty = 0;
        store_settings_save(&bfm_set);
    }
    ui_leds(now);
    if (sq.flash != ui.shown_step || sq.playing != ui.shown_play) {
        ui.shown_step = sq.flash;
        ui.shown_play = sq.playing;
        if (PAGES[ui.page].view == V_ARP || PAGES[ui.page].view == V_SEQ)
            ui.dirty |= 2;
        ui.dirty |= 4;
    }
    if ((now < ui.popup_until) != (popup_was != 0)) {
        popup_was = now < ui.popup_until;
        ui.dirty |= 2;
    }
    if (PAGES[ui.page].view == V_PERF && now - last_bot >= 33u)
        ui.dirty |= 2;                                /* the scope, 30 frames a second */
    if (ui.dirty & 8) {
        lcd_fill(0, 0, 240, 240, T_BG);
        ui.dirty = 7;
    }
    if (ui.dirty & 4)
        draw_header();
    if (ui.dirty & 1)
        draw_cards();
    if (ui.dirty & 2) {
        draw_bottom(now);
        last_bot = now;
    }
    ui.dirty = 0;
}
