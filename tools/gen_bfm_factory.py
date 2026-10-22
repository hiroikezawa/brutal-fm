#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Hiro Ikezawa
"""The ALGORITHM templates and the 40 factory presets of BRUTAL FM, as C initialisers.

  tools/gen_bfm_factory.py OUT.h

One source for both: the firmware, the browser emulator and the host tests read the generated header.
Operators are 0..3 (OP1..OP4); a matrix entry (src, dst) is "src modulates dst", (k, k) is self feedback.
A preset is sound A plus a few differences for sound B (MORPH moves between them), the macros, modulation,
effects and the ARP settings. Every byte is 0..127 (64 = centre for the bipolar ones)."""
import copy
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from gen_bfm_tables import RATIOS  # noqa: E402

RIDX = {name: i for i, (_, name) in enumerate(RATIOS)}
MODES = {"METAL": 0, "GROWL": 1, "CHAOS": 2, "NOISE": 3}
CATS = ["METAL", "GROWL", "CHAOS", "NOISE", "DRONE"]
LFO = {"SINE": 0, "TRI": 1, "SAW": 2, "SQR": 3, "SH": 4, "CHAOS": 5}

ALGOS = [  # name, edges {(src, dst): amount}, carrier outs {op: level}
    ("STACK", {(3, 2): 70, (2, 1): 70, (1, 0): 70, (3, 3): 30}, {0: 118}),
    ("PAIRS", {(1, 0): 75, (3, 2): 75, (3, 3): 30}, {0: 114, 2: 114}),
    ("FAN", {(3, 0): 70, (3, 1): 70, (3, 2): 70, (3, 3): 35}, {0: 110, 1: 110, 2: 110}),
    ("TRIPLE", {(1, 0): 60, (2, 0): 60, (3, 0): 60, (3, 3): 20}, {0: 118}),
    ("X-FM", {(1, 0): 65, (0, 1): 55, (3, 2): 65, (2, 3): 55}, {0: 114, 2: 114}),
    ("RING", {(3, 2): 60, (2, 1): 60, (1, 0): 60, (0, 3): 50}, {0: 118}),
    ("MESH", {**{(s, d): 40 for s in range(4) for d in range(4) if s != d}, **{(k, k): 20 for k in range(4)}},
     {0: 114, 1: 100}),
    ("LOOP", {(3, 2): 65, (2, 1): 65, (1, 0): 65, (0, 3): 45, (3, 3): 45}, {0: 116, 1: 100}),
]
ANAME = {a[0]: i for i, a in enumerate(ALGOS)}


def sound(algo, ratios=("1", "1", "1", "1"), **kw):
    s = {"ratio": [RIDX[r] for r in ratios], "fine": [64] * 4, "fixed": [0] * 4, "level": [118] * 4,
         "out": [0] * 4, "phase": [1] * 4, "atk": [0] * 4, "dec": [70] * 4, "sus": [100] * 4, "rel": [50] * 4,
         "mat": [0] * 16, "fold": 0, "sym": 64, "bias": 64, "fmix": 0, "character": 0, "gain": 100}
    _, edges, outs = ALGOS[ANAME[algo]]
    for (a, b), v in edges.items():
        s["mat"][a * 4 + b] = v
    for k, v in outs.items():
        s["out"][k] = v
    return apply(s, kw)


def apply(s, kw):
    s = copy.deepcopy(s)
    for k, v in kw.items():
        if k == "ratios":
            s["ratio"] = [RIDX[r] for r in v]
        elif k == "edges":                     # {(src, dst): amount}; a negative amount sets the polarity bit
            for (a, b), x in v.items():
                s["mat"][a * 4 + b] = abs(x)
                s.setdefault("neg", [])
                if x < 0 and a * 4 + b not in s["neg"]:
                    s["neg"].append(a * 4 + b)
                if x >= 0 and a * 4 + b in s["neg"]:
                    s["neg"].remove(a * 4 + b)
        elif k in ("atk", "dec", "sus", "rel", "level", "out", "fine", "fixed", "phase") and isinstance(v, dict):
            for op, x in v.items():
                s[k][op] = x
        elif k in ("atk", "dec", "sus", "rel", "level", "out", "fine", "fixed", "phase") and isinstance(v, int):
            s[k] = [v] * 4
        else:
            s[k] = v
    return s


def patch(name, cat, mode, a, b=None, **kw):
    p = {"name": name, "category": CATS.index(cat), "mode": MODES[mode], "poly": 1, "algo": 0,
         "morph": 0, "intensity": 64, "feedback": 64, "movement": 64, "lfo_shape": 0, "lfo_rate": 50,
         "drift_rate": 30, "mod_int": 64, "mod_fold": 64, "mod_pitch": 64, "mod_morph": 64, "mod_fb": 64,
         "vel_sens": 40, "bend": 2, "glide": 0, "drive": 0, "crush": 0, "dly_time": 60, "dly_fb": 40,
         "dly_mix": 0, "a": a, "b": apply(a, b or {}),
         "arp": {"on": 0, "hold": 1, "length": 0, "rate": 2, "density": 127, "complexity": 50, "gatevar": 30,
                 "timbrevar": 40, "gate": 60, "window": 20, "locks": 0}}
    for k, v in kw.items():
        if k == "arp":
            p["arp"].update(v)
        elif k == "lfo_shape":
            p[k] = LFO[v]
        else:
            p[k] = v
    return p


PERC = dict(atk=0, sus=0)        # percussive: everything decays
P = []
# ------------------------------------------------------------------------------------------------- METAL
P.append(patch("RUST BELL", "METAL", "METAL",
               sound("PAIRS", ("1", "3.5", "1", "1.41"), **PERC, dec={0: 92, 1: 70, 2: 88, 3: 60}, rel=80,
                     character=20),
               {"ratios": ("1", "5.4", "1", "2.72"), "fold": 30, "fmix": 70, "character": 50}, algo=1,
               dly_mix=40, dly_time=80, dly_fb=60))
P.append(patch("ANVIL HIT", "METAL", "METAL",
               sound("STACK", ("1", "2.72", "6.28", "9.42"), **PERC, dec={0: 70, 1: 55, 2: 45, 3: 35}, rel=45,
                     fold=40, fmix=60, character=30, edges={(3, 2): 80, (2, 1): 85, (1, 0): 90}),
               {"edges": {(3, 3): 70}, "fold": 80}, intensity=80, algo=0, drive=30))
P.append(patch("SHEET STEEL", "METAL", "METAL",
               sound("FAN", ("1", "1.41", "2.24", "9.42"), dec=90, sus=60, rel=70, character=60),
               {"ratios": ("1", "1.73", "3.14", "11.2"), "character": 100, "fold": 50, "fmix": 80},
               algo=2, movement=80, lfo_shape="TRI", lfo_rate=30, mod_int=90))
P.append(patch("GAMELAN X", "METAL", "METAL",
               sound("PAIRS", ("1", "4.2", "2", "7"), **PERC, dec={0: 95, 1: 80, 2: 90, 3: 70}, rel=90,
                     character=15, edges={(1, 0): 60, (3, 2): 55}),
               {"ratios": ("1", "4.5", "2", "7.5"), "character": 40}, algo=1, dly_mix=50, dly_time=96,
               dly_fb=70, arp={"on": 1, "density": 60, "complexity": 70, "timbrevar": 70}))
P.append(patch("SAW BLADE", "METAL", "METAL",
               sound("X-FM", ("1", "1.06", "2", "3.14"), atk=10, dec=80, sus=110, rel=40, fold=50, fmix=90,
                     character=25),
               {"fold": 95, "sym": 30, "edges": {(0, 1): 85, (2, 3): 80}}, algo=4, poly=0, glide=30,
               lfo_rate=70, mod_fold=96, movement=70))
P.append(patch("CHAIN DRAG", "METAL", "METAL",
               sound("RING", ("1", "2.72", "5.4", "0.5"), dec=85, sus=70, rel=60, character=40),
               {"ratios": ("1", "3.14", "6.28", "0.71"), "character": 90}, algo=5, lfo_shape="TRI",
               lfo_rate=28, mod_int=88, movement=70))   # (v0.1.3: a fast S&H on the FM depth was a fizz)
P.append(patch("GONG PIT", "METAL", "METAL",
               sound("TRIPLE", ("0.5", "1.41", "2.72", "3.14"), atk={0: 6}, sus=0, dec=110, rel=110,
                     character=35),
               {"ratios": ("0.5", "1.73", "2.24", "4.2"), "character": 70}, algo=3, morph=0, movement=50,
               lfo_rate=20, mod_morph=100, drift_rate=20, dly_mix=30, dly_time=120, dly_fb=80))
P.append(patch("TIN SNARE", "METAL", "METAL",
               sound("FAN", ("1", "1.41", "2.24", "1"), **PERC, dec={0: 45, 1: 40, 2: 42, 3: 30}, rel=30,
                     fixed={3: 118}, character=80, fold=60, fmix=70, edges={(3, 3): 90}),
               {"fixed": {3: 125}, "fold": 100}, algo=2, intensity=90,
               arp={"on": 1, "density": 70, "complexity": 40, "rate": 2}))
# ------------------------------------------------------------------------------------------------- GROWL
P.append(patch("IRON BASS", "GROWL", "GROWL",
               sound("STACK", ("1", "1", "2", "1"), dec={1: 60, 2: 55, 3: 50}, sus={1: 70, 2: 60, 3: 50},
                     rel=35, fold=40, fmix=70, character=60),
               {"fold": 90, "edges": {(3, 3): 70}, "character": 80}, algo=0, poly=0, glide=20, drive=30))
P.append(patch("WOBBLE GUT", "GROWL", "GROWL",
               sound("PAIRS", ("1", "1", "0.5", "2"), sus=110, rel=35, fold=30, fmix=60, character=50),
               {"fold": 70, "edges": {(1, 0): 100}}, algo=1, poly=0, lfo_shape="SINE", lfo_rate=62,
               mod_int=120, mod_fold=100, movement=90, drive=20))
P.append(patch("REESE FM", "GROWL", "GROWL",
               sound("X-FM", ("1", "1.06", "0.5", "1.5"), sus=115, rel=40, character=55, fine={1: 70},
                     edges={(1, 0): 55, (0, 1): 40}),
               {"edges": {(1, 0): 85, (0, 1): 70}, "fold": 50, "fmix": 60}, algo=4, poly=0, glide=15,
               drift_rate=40, mod_morph=100, movement=70))
P.append(patch("SUB GRINDER", "GROWL", "GROWL",
               sound("TRIPLE", ("1", "0.5", "2", "3"), sus=110, rel=30, character=80, edges={(3, 3): 60}),
               {"edges": {(3, 3): 100, (2, 0): 90}}, algo=3, poly=0, feedback=80, drive=60))
P.append(patch("ACID GROWL", "GROWL", "GROWL",
               sound("STACK", ("1", "1", "2", "3"), dec={1: 40, 2: 35, 3: 30}, sus={1: 20, 2: 10, 3: 0},
                     rel=30, fold=30, fmix=50, character=45),
               {"fold": 80, "dec": {1: 55, 2: 50}}, algo=0, poly=0, glide=10, drive=25,
               arp={"on": 1, "density": 100, "complexity": 60, "gatevar": 60, "timbrevar": 90, "gate": 50}))
P.append(patch("DIESEL", "GROWL", "GROWL",
               sound("LOOP", ("1", "0.5", "2", "3"), sus=110, rel=35, character=70),
               {"edges": {(0, 3): 85, (3, 3): 80}, "fold": 60, "fmix": 70}, algo=7, poly=0, drive=50,
               lfo_shape="SQR", lfo_rate=55, mod_int=84))
P.append(patch("BIT BASS", "GROWL", "GROWL",
               sound("FAN", ("1", "0.5", "1", "2"), sus=105, rel=30, character=55),
               {"edges": {(3, 0): 100}}, algo=2, poly=0, crush=40, drive=20))
P.append(patch("SLUDGE", "GROWL", "GROWL",
               sound("MESH", ("1", "0.5", "1.5", "2"), atk=20, sus=110, rel=60, fold=40, fmix=60, character=70,
                     edges={(s, d): 28 for s in range(4) for d in range(4) if s != d}),
               {"fold": 100, "sym": 20}, algo=6, poly=0, lfo_rate=25, mod_fold=110, movement=80))
# ------------------------------------------------------------------------------------------------- CHAOS
P.append(patch("FDBK HELL", "CHAOS", "CHAOS",
               sound("LOOP", ("1", "1.5", "2", "3.5"), sus=110, rel=50, character=50,
                     edges={(3, 3): 70, (0, 3): 60}),
               {"edges": {(3, 3): 110, (0, 3): 100}, "character": 100}, algo=7, feedback=90, morph=20,
               drift_rate=50, mod_morph=110, movement=80))
P.append(patch("ATTRACTOR", "CHAOS", "CHAOS",
               sound("X-FM", ("1", "1.41", "0.71", "2.24"), sus=110, rel=60, character=60),
               {"character": 110, "ratios": ("1", "1.73", "0.75", "3.14")}, algo=4, lfo_shape="CHAOS",
               lfo_rate=70, mod_int=110, mod_fold=90, movement=90, fold=0))
P.append(patch("CROSSFIRE", "CHAOS", "CHAOS",
               sound("X-FM", ("1", "2", "1.5", "3"), sus=110, rel=40, character=80,
                     edges={(1, 0): 80, (0, 1): -80, (3, 2): 80, (2, 3): -80}),
               {"edges": {(0, 2): 60, (2, 0): -60}, "fold": 60, "fmix": 80}, algo=4, poly=0, feedback=80))
P.append(patch("RUNAWAY", "CHAOS", "CHAOS",
               sound("RING", ("1", "1.06", "2", "2.5"), sus=110, rel=50, character=40),
               {"edges": {(0, 3): 110}, "character": 110}, algo=5, feedback=96, mod_fb=110, drift_rate=60,
               movement=80))
P.append(patch("DEAD RADIO", "CHAOS", "CHAOS",
               sound("MESH", ("1", "3.14", "6.28", "0.5"), sus=110, rel=60, character=60),
               {"character": 120, "ratios": ("1", "4.2", "9.42", "0.25")}, algo=6, lfo_shape="SH",
               lfo_rate=90, mod_int=100, mod_morph=110, drift_rate=70, movement=100, crush=30))
P.append(patch("ORBIT DECAY", "CHAOS", "CHAOS",
               sound("PAIRS", ("1", "2.5", "1", "3.5"), sus=0, dec=105, rel=90, character=50,
                     edges={(0, 1): 50, (2, 3): 50}),
               {"character": 100, "edges": {(0, 1): 90, (2, 3): 90}}, algo=1, mod_morph=100, drift_rate=40,
               movement=80, dly_mix=40, dly_time=100, dly_fb=70))
P.append(patch("MUTANT LEAD", "CHAOS", "CHAOS",
               sound("STACK", ("1", "2", "3", "1.5"), atk=8, sus=110, rel=45, character=40, fold=30, fmix=60),
               {"character": 90, "fold": 70}, algo=0, poly=0, glide=25, lfo_rate=66, mod_pitch=80,
               mod_int=90, movement=70))
P.append(patch("CHAOS ARP", "CHAOS", "CHAOS",
               sound("LOOP", ("1", "2", "3.5", "5"), **PERC, dec=70, rel=50, character=70, fold=40, fmix=70),
               {"character": 120, "fold": 90}, algo=7, intensity=72,
               arp={"on": 1, "density": 90, "complexity": 100, "gatevar": 80, "timbrevar": 110, "gate": 45}))
# ------------------------------------------------------------------------------------------------- NOISE
P.append(patch("STATIC HIT", "NOISE", "NOISE",
               sound("STACK", ("1", "3.14", "9.42", "16"), **PERC, dec=60, rel=50, character=80),
               {"character": 120}, algo=0, intensity=90, drive=40))
P.append(patch("CLICK STORM", "NOISE", "NOISE",
               sound("FAN", ("1", "7", "13", "16"), **PERC, dec={0: 48, 1: 40, 2: 40, 3: 30}, rel=30, level=127,
                     character=100),
               {"character": 127, "ratios": ("0.5", "9", "15", "16")}, algo=2, crush=60,
               arp={"on": 1, "density": 95, "complexity": 110, "gatevar": 20, "timbrevar": 120, "rate": 0,
                    "gate": 25}))
P.append(patch("DATA STORM", "NOISE", "NOISE",
               sound("MESH", ("1", "5", "11", "13"), sus=110, rel=50, character=90),
               {"character": 127}, algo=6, lfo_shape="SH", lfo_rate=100, mod_int=110, movement=100,
               crush=50))
P.append(patch("WHITE METAL", "NOISE", "NOISE",
               sound("FAN", ("1", "1.41", "2.72", "16"), sus=60, dec=80, rel=60, character=70,
                     fixed={3: 120}),
               {"character": 110, "fold": 60, "fmix": 70}, algo=2, mod_morph=96, drift_rate=40, movement=70))
P.append(patch("CRUSH HAT", "NOISE", "NOISE",
               sound("STACK", ("1", "1", "1", "1"), **PERC, dec={0: 35, 1: 30, 2: 30, 3: 30}, rel=25,
                     fixed={0: 115, 1: 121, 2: 110, 3: 126}, character=110),
               {"dec": {0: 50}}, algo=0, crush=70, intensity=100,
               arp={"on": 1, "density": 110, "complexity": 30, "rate": 2, "gate": 20, "gatevar": 40}))
P.append(patch("TAPE SHRED", "NOISE", "NOISE",
               sound("RING", ("1", "1.5", "3", "0.25"), sus=110, rel=60, character=60, fold=30, fmix=50),
               {"character": 120, "fold": 80}, algo=5, lfo_shape="SAW", lfo_rate=40, mod_pitch=40,
               mod_fold=110, movement=90, crush=30))
P.append(patch("NOISE SWEEP", "NOISE", "NOISE",
               sound("TRIPLE", ("1", "2", "4", "8"), atk=40, sus=110, rel=80, character=70, fold=50, fmix=80),
               {"character": 120, "fold": 110}, algo=3, lfo_shape="TRI", lfo_rate=20, mod_fold=120,
               mod_int=100, movement=90))
P.append(patch("ERROR TONE", "NOISE", "NOISE",
               sound("PAIRS", ("1", "16", "2", "11"), sus=110, rel=30, character=50, phase=1),
               {"character": 115, "ratios": ("1", "15", "2", "13")}, algo=1, poly=0, lfo_shape="SQR",
               lfo_rate=85, mod_morph=64, mod_int=110, movement=80, crush=40))
# ------------------------------------------------------------------------------------------------- DRONE
P.append(patch("IRON DRONE", "DRONE", "GROWL",
               sound("STACK", ("1", "0.5", "1.5", "2"), atk=95, sus=115, rel=110, character=80, fold=30, fmix=50),
               {"fold": 80, "edges": {(3, 3): 80}}, algo=0, lfo_rate=10, drift_rate=10, mod_morph=110,
               mod_fold=90, movement=90, dly_mix=40, dly_time=127, dly_fb=80))
P.append(patch("VOID HUM", "DRONE", "CHAOS",
               sound("X-FM", ("0.5", "0.71", "1", "1.41"), atk=100, sus=115, rel=115, character=40),
               {"character": 100, "edges": {(0, 1): -80, (2, 3): -80}}, algo=4, drift_rate=8, mod_morph=120,
               mod_fb=100, movement=100))
P.append(patch("FURNACE", "DRONE", "NOISE",
               sound("MESH", ("0.5", "1", "1.5", "3.14"), atk=90, sus=115, rel=110, character=40,
                     edges={(s, d): 25 for s in range(4) for d in range(4) if s != d}),
               {"character": 90}, algo=6, drift_rate=15, lfo_rate=12, mod_morph=110, mod_int=96,
               movement=90, dly_mix=50, dly_time=127, dly_fb=90))
P.append(patch("CATHEDRAL X", "DRONE", "METAL",
               sound("FAN", ("1", "2", "3", "4.2"), atk=100, sus=115, rel=120, character=30),
               {"character": 80, "ratios": ("1", "2.24", "3.14", "5.4")}, algo=2, drift_rate=10,
               mod_morph=120, movement=90, dly_mix=60, dly_time=127, dly_fb=95))
P.append(patch("SLOW MELT", "DRONE", "CHAOS",
               sound("LOOP", ("1", "1.5", "2", "0.5"), atk=105, sus=115, rel=115, character=30, fold=20, fmix=40),
               {"character": 90, "fold": 70}, algo=7, lfo_shape="SINE", lfo_rate=5, mod_morph=127,
               mod_fold=100, movement=100))
P.append(patch("TURBINE", "DRONE", "METAL",
               sound("RING", ("1", "1", "1", "1"), atk=80, sus=115, rel=100, fixed={1: 70, 2: 85, 3: 40},
                     character=50),
               {"fixed": {1: 74, 2: 90, 3: 46}}, algo=5, lfo_rate=15, mod_morph=110, movement=90))
P.append(patch("DEAD SIGNAL", "DRONE", "NOISE",
               sound("PAIRS", ("0.5", "11", "1", "13"), atk=90, sus=115, rel=110, character=60),
               {"character": 110}, algo=1, lfo_shape="SH", lfo_rate=30, mod_int=90, drift_rate=20,
               mod_morph=100, movement=80, crush=20, dly_mix=40, dly_time=110, dly_fb=80))
P.append(patch("OCEAN RUST", "DRONE", "GROWL",
               sound("TRIPLE", ("0.5", "1", "1.41", "2.72"), atk=95, sus=115, rel=115, character=60, fold=30,
                     fmix=40),
               {"fold": 70, "character": 90}, algo=3, lfo_rate=8, drift_rate=12, mod_morph=110, mod_fold=96,
               movement=90, dly_mix=50, dly_time=127, dly_fb=85))

assert len(P) == 40, len(P)

# BRUTAL is the floor, not a category: no factory sound is clean. Every preset gets at least this much drive,
# folding, character and FM depth (a preset already above a floor keeps its own value); B, the far end of MORPH,
# goes further than A. Grit comes from saturation and folding, not from noise: no floor on self-feedback (OP4
# driven near chaos turns to hiss) and no sample crusher (its aliasing is a fizz over every note), v0.1.3.
# CHARACTER only where it is a tone (METAL: inharmonic spread, GROWL: body); in CHAOS and NOISE it is the
# amount of noise and chaos, which those presets set themselves. FM depth is raised a little: a very deep
# index is broadband, a hiss of its own.
FLOOR = {"intensity": 76, "drive": 70}
FLOOR_A = {"fold": 55, "fmix": 75, "character": 65}
FLOOR_B = {"fold": 90, "fmix": 95, "character": 100}
TONE_MODES = (MODES["METAL"], MODES["GROWL"])


def brutalize(p):
    for k, v in FLOOR.items():
        p[k] = max(p[k], v)
    for side, fl in (("a", FLOOR_A), ("b", FLOOR_B)):
        s = p[side]
        for k, v in fl.items():
            if k != "character" or p["mode"] in TONE_MODES:
                s[k] = max(s[k], v)


for p in P:
    brutalize(p)

# the ones the floor leaves too clean (a low note or a bell barely reaches the folder): pushed by hand.
# Modulators held up (no decay to a pure sine), the folder further (the master drive stays at the floor: past
# it the saturation pins the level and no GAIN can bring it down).
HARDER = ("RUST BELL", "IRON BASS", "ACID GROWL", "BIT BASS", "SLUDGE")
for p in P:
    if p["name"] in HARDER:
        for side in ("a", "b"):
            s = p[side]
            s["fold"] = max(s["fold"], 85 if side == "a" else 115)
            if p["mode"] in TONE_MODES:
                s["character"] = max(s["character"], 95 if side == "a" else 120)
            for op in (1, 2, 3):
                s["sus"][op] = max(s["sus"][op], 90)
# two basses whose folded GROWL body already saturates: the master drive on top pinned their level above the
# others whatever their GAIN; their grit is the folder's
for p in P:
    if p["name"] in ("IRON BASS", "ACID GROWL"):
        p["drive"] = 45

# hiss budget (v0.1.4): grit yes, fizz no. tools/tune_hiss.py finds per preset the smallest TAME step (0 .. 30)
# at which its render keeps <= -20 dB above 16 kHz and a spectral flatness <= 0.30 (tests/render.c), and writes
# tools/factory_tame.txt. Steps 1..10 take the noise sources away (feedback, self-feedback and the edges that close
# a loop, the CHAOS amount,
# the LFO's FM depth, half the FM depth above neutral); 11..20 then soften the folder; 21..30 bring the FM depth
# and the METAL spread to neutral and halve the folder's mix. NOISE-mode presets are noise by
# design and are left alone.
TAME_FILE = Path(__file__).resolve().parent / "factory_tame.txt"


def toward(v, centre, f):
    return int(round(centre + (v - centre) * f))


def tame(p, t):
    f = max(0.0, 1.0 - min(t, 10) / 10.0)          # 1..10: the noise sources, 1 .. 0
    g = 1.0 - min(10, max(0, t - 10)) / 10.0 * 0.6  # 11..20: the folder, 1 .. 0.4
    h = 1.0 - min(10, max(0, t - 20)) / 10.0        # 21..30: the FM depth and METAL spread above neutral, 1 .. 0
    for k in ("feedback", "mod_fb", "mod_int"):
        if p[k] > 64:
            p[k] = toward(p[k], 64, f)
    if p["intensity"] > 64:
        p["intensity"] = toward(p["intensity"], 64, (0.5 + 0.5 * f) * h)
    for side in ("a", "b"):
        s = p[side]
        for a in range(4):
            for b in range(4):
                if a <= b:                          # self-feedback and the edges that close a loop (src < dst)
                    s["mat"][a * 4 + b] = int(round(s["mat"][a * 4 + b] * f))
        if p["mode"] == MODES["CHAOS"]:
            s["character"] = int(round(s["character"] * f))
        elif p["mode"] == MODES["METAL"]:
            s["character"] = int(round(s["character"] * h))
        s["fold"] = int(round(s["fold"] * g))
        s["fmix"] = int(round(s["fmix"] * (0.5 + 0.5 * h)))


TAMES = {}
if TAME_FILE.exists():
    for line in TAME_FILE.read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            n, t = line.split("|")
            TAMES[n] = int(t)
for p in P:
    if TAMES.get(p["name"]):
        tame(p, TAMES[p["name"]])

# the ARP as it starts (v0.1.4): HOLD on, 8 steps, DENSITY at the top (no rests) in every factory preset; the
# per-preset DENSITY above is kept nowhere (turn DENSITY down for rests)
for p in P:
    p["arp"].update(hold=1, length=0, density=127)

# loudness: every sound's GAIN from tests/calibrate.c (CAL=1), so that A, B and the presets match (-15 dBFS RMS
# over their loudest 50 ms of a held C3, before the limiter). Missing file or name: the gain written above
GAINS = {}
_gf = Path(__file__).resolve().parent / "factory_gain.txt"
if _gf.exists():
    for line in _gf.read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            n, ga, gb = line.split("|")
            GAINS[n] = (int(ga), int(gb))
for p in P:
    if p["name"] in GAINS:
        p["a"]["gain"], p["b"]["gain"] = GAINS[p["name"]]


def c_sound(s):
    def a(v):
        return "{" + ", ".join(str(x) for x in v) + "}"
    f = [f".ratio = {a(s['ratio'])}", f".fine = {a(s['fine'])}", f".fixed = {a(s['fixed'])}",
         f".level = {a(s['level'])}", f".out = {a(s['out'])}", f".phase = {a(s['phase'])}",
         f".atk = {a(s['atk'])}", f".dec = {a(s['dec'])}", f".sus = {a(s['sus'])}", f".rel = {a(s['rel'])}",
         f".mat = {a(s['mat'])}"]
    f += [f".{k} = {s[k]}" for k in ("fold", "sym", "bias", "fmix", "character", "gain")]
    neg = sum(1 << e for e in s.get("neg", []))
    f += [f".neg = {{{neg & 255}, {neg >> 8}}}"]
    return "{" + ", ".join(f) + "}"


def check(p):
    for side in ("a", "b"):
        s = p[side]
        for k, v in s.items():
            if k == "neg":
                continue
            vals = v if isinstance(v, list) else [v]
            for x in vals:
                assert 0 <= x <= 127, (p["name"], side, k, x)
    assert len(p["name"]) <= 11, p["name"]


def main():
    L = ["/* generated by tools/gen_bfm_factory.py: do not edit */", "#pragma once",
         f"#define BFM_NALGO {len(ALGOS)}", f"#define BFM_NFACTORY {len(P)}",
         "static const char *const BFM_ALGO_NAME[BFM_NALGO] = {" + ", ".join(f'"{a[0]}"' for a in ALGOS) + "};",
         "static const uint8_t BFM_ALGO_MAT[BFM_NALGO][16] = {"]
    for _, edges, _ in ALGOS:
        m = [0] * 16
        for (a, b), v in edges.items():
            m[a * 4 + b] = v
        L.append("    {" + ", ".join(map(str, m)) + "},")
    L += ["};", "static const uint8_t BFM_ALGO_OUT[BFM_NALGO][4] = {"]
    for _, _, outs in ALGOS:
        L.append("    {" + ", ".join(str(outs.get(k, 0)) for k in range(4)) + "},")
    L += ["};", "static const char *const BFM_CAT_NAME[6] = {" + ", ".join(f'"{c}"' for c in CATS) + ', "USER"};',
          "static const bfm_patch_t BFM_FACTORY[BFM_NFACTORY] = {"]
    for p in P:
        check(p)
        ar = p["arp"]
        head = ", ".join(f".{k} = {p[k]}" for k in (
            "category", "mode", "poly", "algo", "morph", "intensity", "feedback", "movement", "lfo_shape",
            "lfo_rate", "drift_rate", "mod_int", "mod_fold", "mod_pitch", "mod_morph", "mod_fb", "vel_sens",
            "bend", "glide", "drive", "crush", "dly_time", "dly_fb", "dly_mix"))
        arp = ", ".join(f".{k} = {v}" for k, v in ar.items())
        L.append(f'    {{.name = "{p["name"]}", {head},\n     .s = {{{c_sound(p["a"])},\n            '
                 f'{c_sound(p["b"])}}},\n     .arp = {{{arp}}}, .pat = {{.len = {16 if ar["length"] else 8}}}}},')
    L.append("};")
    open(sys.argv[1], "w").write("\n".join(L) + "\n")
    print(f"bfm factory: {len(ALGOS)} algorithms, {len(P)} presets -> {sys.argv[1]}")


if __name__ == "__main__":
    main()
