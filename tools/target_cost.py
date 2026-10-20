#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Hiro Ikezawa (the listing parser after Felucca's tests/target_budget.py, (C) 2026 Leo Kuroshita)
"""Static cost of the hot loops in the pi32v2 listing (build/brutal.dis): instructions per pass of each loop
depth, divides and calls inside. Not a cycle count (no simulator of the chip exists here); used to compare
BRUTAL FM's voice kernel with Felucca's FM6 operator loop, compiled by the same compiler.
  tools/target_cost.py DIS FUNC [FUNC ...]"""
import re
import sys

LABEL = re.compile(r"^([A-Za-z_][A-Za-z_0-9.]*):$")
INSN = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)$")
TARGET = re.compile(r"goto -?\d+ <[^>]*: ([0-9a-f]+) >")


def functions(path, want):
    out, cur = {}, None
    for line in open(path):
        m = LABEL.match(line.strip())
        if m:
            name = m.group(1)
            if not name.startswith("."):
                cur = out.setdefault(name, []) if name in want else None
            continue
        m = INSN.match(line)
        if cur is not None and m and not m.group(3).lstrip().startswith("<"):
            cur.append((int(m.group(1), 16), m.group(3).strip()))
    return out


def depths(insns):
    lo = insns[0][0]
    heads = {}
    for a, t in insns:
        m = TARGET.search(t)
        if m and lo <= int(m.group(1), 16) < a:
            h = int(m.group(1), 16)
            heads[h] = max(heads.get(h, a), a)
    per = {}
    divs = calls = 0
    for a, t in insns:
        d = sum(1 for h, e in heads.items() if h <= a <= e)
        per[d] = per.get(d, 0) + 1
        if d and re.search(r"= r\d+ / r\d+", t):
            divs += 1
        if d and t.startswith("call"):
            calls += 1
    return per, divs, calls, len(heads)


def main():
    path, names = sys.argv[1], sys.argv[2:]
    fns = functions(path, set(names))
    for n in names:
        if n not in fns:
            print(f"{n:22s} (not in the listing: inlined)")
            continue
        per, divs, calls, loops = depths(fns[n])
        d = " ".join(f"depth{k}={v}" for k, v in sorted(per.items()))
        print(f"{n:22s} {len(fns[n]):5d} insns, {loops} loops: {d}; divides in loops {divs}, calls in loops {calls}")


if __name__ == "__main__":
    main()
