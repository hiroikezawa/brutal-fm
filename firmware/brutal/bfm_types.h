/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Hiro Ikezawa */
/* BRUTAL FM: the data of the instrument. Everything here is plain bytes so that a preset is a memcpy, the
 * flash record is the struct itself, and the browser emulator and the host tests share it with the firmware.
 * Integer only: the FM-1 build has no float.
 *
 * A PATCH is one preset: two SOUNDS (A and B, MORPH crossfades their parameters), the performance macros,
 * modulation, effects, the COMPLEX ARP settings and the ARP pattern that was generated last (the steps
 * themselves, not only the seed, so a saved pattern comes back exactly). */
#ifndef BFM_TYPES_H
#define BFM_TYPES_H
#include <stdint.h>

#define BFM_NOP 4                /* operators per voice */
#define BFM_MAXV 8               /* voices at most (the VOICES setting; HQ renders at 2x and allows half) */
#define BFM_MAXSTEP 16
#define BFM_POOL 16              /* most distinct notes the ARP takes (the lowest 16 of a bigger set) */
#define BFM_NAME 12

enum { MODE_METAL, MODE_GROWL, MODE_CHAOS, MODE_NOISE, MODE_N };
enum { LFO_SINE, LFO_TRI, LFO_SAW, LFO_SQR, LFO_SH, LFO_CHAOS, LFO_N };
enum { SYNC_INT, SYNC_EXT, SYNC_AUTO, SYNC_N };
enum { RATE_32, RATE_16T, RATE_16, RATE_8T, RATE_8, RATE_4, RATE_N };

/* one sound; every byte 0..127 (ratio: 0..BFM_NRATIO-1) */
typedef struct {
    uint8_t ratio[BFM_NOP];      /* index into BFM_RATIO_LOG */
    uint8_t fine[BFM_NOP];       /* 64 = 0, +-128 cents in 2-cent steps */
    uint8_t fixed[BFM_NOP];      /* 0: ratio mode; 1..127 fixed frequency (1 Hz .. 9 kHz) */
    uint8_t level[BFM_NOP];      /* operator amplitude (modulation depth and carrier level both follow it) */
    uint8_t out[BFM_NOP];        /* carrier mix: how much of this operator reaches the output */
    uint8_t phase[BFM_NOP];      /* 0: free running; 1..127: reset at note-on to (v-1)/127 cycle */
    uint8_t atk[BFM_NOP], dec[BFM_NOP], sus[BFM_NOP], rel[BFM_NOP];
    uint8_t mat[BFM_NOP * BFM_NOP];   /* modulation amount [src * 4 + dst]; the diagonal is self feedback */
    uint8_t fold, sym, bias, fmix;    /* wavefolder: amount, symmetry (64 = even), bias (64 = 0), dry/wet */
    uint8_t character;           /* the mode's own control (METAL spread, GROWL body, CHAOS nonlinearity, NOISE) */
    uint8_t gain;                /* level trim (100 = 0 dB) */
    uint8_t rsv[2];
} bfm_sound_t;

/* one ARP step: the generator writes all of it; playback only reads */
#define ST_ON 1u                 /* a note starts here */
#define ST_ACC 2u                /* accent */
#define ST_TIE 4u                /* the note runs into the next step (no gap) */
typedef struct {
    uint8_t flags;
    uint8_t note;                /* MIDI note: always one of the pool's */
    uint8_t idx;                 /* its index in the pool (FREEZE remaps by it) */
    uint8_t gate;                /* 1..100 % of the step */
    int8_t fm, fb, morph;        /* timbre offsets added to INTENSITY / FEEDBACK / MORPH (-64..63) */
    uint8_t rsv;
} bfm_step_t;

typedef struct {
    uint8_t len;                 /* 8 or 16 */
    uint8_t npool;
    uint8_t pool[BFM_POOL];      /* the input notes, ascending, distinct */
    uint32_t seed;               /* the seed this pattern was generated from */
    bfm_step_t st[BFM_MAXSTEP];
} bfm_pattern_t;

#define LK_FREEZE 1u
#define LK_RHYTHM 2u
#define LK_PITCH 4u
#define LK_TIMBRE 8u
typedef struct {
    uint8_t on, hold, length, rate;          /* length: 0 = 8, 1 = 16 */
    uint8_t density, complexity, gatevar, timbrevar;
    uint8_t gate;                /* base gate (% of a step) */
    uint8_t window;              /* chord capture window, 0..127 -> 0..508 ms (4 ms steps); default 20 = 80 ms */
    uint8_t locks;               /* LK_* */
    uint8_t rsv;
} bfm_arpset_t;

typedef struct {
    char name[BFM_NAME];
    uint8_t category;            /* 0 METAL 1 GROWL 2 CHAOS 3 NOISE 4 DRONE 5 USER (display only) */
    uint8_t mode;                /* MODE_* */
    uint8_t poly;                /* 1 poly, 0 mono (legato) */
    uint8_t algo;                /* last ALGORITHM picked (display; the matrix is what plays) */
    uint8_t morph, intensity, feedback, movement;     /* the performance macros (64 = neutral for I / F / M) */
    uint8_t lfo_shape, lfo_rate, drift_rate;
    uint8_t mod_int, mod_fold, mod_pitch, mod_morph, mod_fb;   /* 64 = 0, bipolar */
    uint8_t vel_sens, bend, glide;
    uint8_t drive, crush, dly_time, dly_fb, dly_mix;
    uint8_t rsv[3];
    bfm_sound_t s[2];            /* A, B */
    bfm_arpset_t arp;
    bfm_pattern_t pat;
} bfm_patch_t;

/* global settings (not part of a preset) */
typedef struct {
    uint32_t magic;
    uint8_t sync, midi_ch, cc_on, pc_on;     /* midi_ch 0 = OMNI, 1..16 */
    uint8_t cc[4];               /* CC numbers of MORPH INTENSITY FEEDBACK MOVEMENT */
    uint8_t quality;             /* 0 ECO (1x), 1 HQ (2x oversampled) */
    uint8_t bpm;                 /* internal tempo - 40 (40..295) */
    uint8_t palette, last_preset;
    uint8_t voices;              /* polyphony 2..8 (HQ: half of it) */
    uint8_t rsv[3];
} bfm_settings_t;
#define BFM_SETTINGS_MAGIC 0x31534642u   /* "BFS1" */

#endif
