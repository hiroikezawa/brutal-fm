#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Hiro Ikezawa
# Every BRUTAL FM test. Run after ./build.sh (build/gen); the emulator part needs Node.js and Emscripten
# (. ../env.sh). Prints each suite and ends with ALL TESTS PASSED, or exits non-zero.
#   tests/run_tests.sh            the host suites (ASan / UBSan), the factory loudness, the package, the emulator
#   QUICK=1 tests/run_tests.sh    without the sanitisers (faster)
set -e
cd "$(dirname "$0")/.."
[ -f build/gen/bfm_tables.h ] || { echo "tests: run ./build.sh first (build/gen)"; exit 1; }
CC="${CC:-cc}"
OUT=build/host
mkdir -p "$OUT" build/ui build/wav
if [ -n "$QUICK" ]; then SAN="-O2"; else SAN="-O1 -g -fsanitize=address,undefined -fno-sanitize=shift -fno-sanitize-recover=all"; fi
FLAGS="-std=gnu99 -fwrapv -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable"
run() { echo "== $1"; shift; "$@"; }

$CC $SAN $FLAGS -Ibuild/gen -o $OUT/arp_test tests/arp_test.c -lm
run "COMPLEX ARP (generator, pool rule, capture, loop boundary, locks, clock, MIDI)" $OUT/arp_test
$CC $SAN $FLAGS -Ibuild/gen -o $OUT/dsp_test tests/dsp_test.c -lm
run "DSP (extremes, random patches, DC, decay, clicks, polarity, MORPH level, aliasing, 60 s)" $OUT/dsp_test
$CC $SAN $FLAGS -w -DSIM_NATIVE -Ibuild/gen -Ifirmware/drivers -Ifirmware/brutal -Ifirmware/hal -o $OUT/ui_test tests/ui_test.c -lm
run "UI on the simulated FM-1 (pages, knobs, A/B, presets, flash, ARP keys, octave, MIDI PC)" $OUT/ui_test build/ui
$CC -O2 $FLAGS -Ibuild/gen -o $OUT/calibrate tests/calibrate.c -lm
run "factory presets: A and B at the same loudness" $OUT/calibrate
$CC -O2 -w -Ibuild/gen -o $OUT/render tests/render.c -lm
run "render every preset and the demos to build/wav" sh -c "$OUT/render build/wav > build/wav/presets.txt && $OUT/render build/wav -2 > build/wav/demos.txt && cat build/wav/demos.txt"
if [ -f build/brutal.fwsc ]; then
    run "the firmware package (offline)" python3 tools/check_package.py build/brutal.fwsc
    python3 tools/target_cost.py build/brutal.dis bfm_kernel bfm_kernel_x fm1_alnk0_irq
else
    echo "== skip the package (no build/brutal.fwsc: ./build.sh)"
fi
if command -v emcc >/dev/null 2>&1 && command -v node >/dev/null 2>&1; then
    sh web/emu/build.sh
    run "browser emulator (the wasm, driven as the page drives it)" node web/emu/emu_test.mjs build/emu/brutal.wasm
else
    echo "== skip the browser emulator (no emcc / node: . ../env.sh)"
fi
echo "ALL TESTS PASSED"
