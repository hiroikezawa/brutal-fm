#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Hiro Ikezawa (after Felucca's web/emu/build.sh, (C) 2026 Leo Kuroshita, GPL-3.0)
# BRUTAL FM in the browser: the firmware's sources on a simulated FM-1 (web/emu/bfm_sim.c), compiled to
# WebAssembly with Emscripten, plus the page.
#   web/emu/build.sh   ->  build/emu/{index.html, worklet.js, brutal.wasm}
# Run after one ./build.sh (build/gen: the tables, the presets, the fonts). Serve with
#   python3 -m http.server -d build/emu 8790     (open http://localhost:8790/)
set -e
cd "$(dirname "$0")/../.."
[ -f build/gen/bfm_tables.h ] || { echo "emu: run ./build.sh once first (build/gen)"; exit 1; }
command -v emcc >/dev/null 2>&1 || { echo "emu: no emcc (Emscripten: . ../env.sh)"; exit 1; }
VERSION=$(sed -n 's/^#define BFM_VERSION "\([^"]*\)".*/\1/p' firmware/brutal/brutal.c | head -n 1)
mkdir -p build/emu
# shellcheck disable=SC2086
emcc ${EMU_OPT:--O2} -ffp-contract=off -std=gnu99 -w ${EMU_CFLAGS:-} "-DBFM_VERSION=\"$VERSION\"" \
    -Ibuild/gen -Ifirmware/drivers -Ifirmware/brutal -Ifirmware/hal \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o build/emu/brutal.wasm web/emu/bfm_sim.c
sed -e "s/@VERSION@/$VERSION/g" web/emu/index.html > build/emu/index.html
cp web/emu/worklet.js build/emu/
echo "emu: build/emu ($(wc -c < build/emu/brutal.wasm | tr -d ' ') B wasm, $VERSION)"
