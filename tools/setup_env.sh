#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Hiro Ikezawa
# Sets up BRUTAL FM's own build environment in DEST (default: the directory above this repository), apart from
# anything else on the machine: nothing is installed system-wide, no shell profile is touched, no other
# project's toolchain or variables are used or changed.
#   DEST/toolchain/jieli/toolchain     JieLi's Linux toolchain (clang 4.0.1 for pi32v2), from JieLi's package server
#   DEST/toolchain/fw-AC79_AIoT_SDK    the three files of the AC79 SDK (Apache-2.0) the package needs, tag
#                                      AC79NN_SDK_V1.2.1_2023-12-13, sparse (not the whole 2 GB SDK)
#   DEST/toolchain/pylib               Python packages the build needs that the system lacks (fontTools)
#   DEST/toolchain/node                Node.js 22.11.0 (web tests; optional)
#   DEST/toolchain/emsdk               Emscripten 3.1.74 (the browser emulator; optional, ~1 GB)
#   DEST/env.sh                        `. DEST/env.sh` sets JIELI_TOOLCHAIN, AC79_SDK, PYTHONPATH and PATH for this
#                                      shell only
#   tools/setup_env.sh [DEST] [--no-web]
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${1:-$(dirname "$HERE")}"
[ "$1" = "--no-web" ] && DEST="$(dirname "$HERE")"
WEB=1
for a in "$@"; do [ "$a" = "--no-web" ] && WEB=0; done
TC="$DEST/toolchain"
mkdir -p "$TC"
echo "== BRUTAL FM environment in $TC"
[ "$(uname -s)" = Linux ] && [ "$(uname -m)" = x86_64 ] || echo "note: the JieLi toolchain is Linux x86-64 only (elsewhere: Docker, BUILDING.md)"
if [ ! -x "$TC/jieli/toolchain/pi32v2/bin/clang" ]; then
    echo "-- JieLi toolchain"
    sh "$HERE/tools/get_toolchain.sh" "$TC/jieli"
fi
if [ ! -f "$TC/fw-AC79_AIoT_SDK/cpu/wl82/tools/uboot.boot" ]; then
    echo "-- AC79 SDK (sparse: uboot.boot, cfg_tool.bin, eq_cfg_hw.bin)"
    rm -rf "$TC/fw-AC79_AIoT_SDK"
    mkdir -p "$TC/fw-AC79_AIoT_SDK"
    (cd "$TC/fw-AC79_AIoT_SDK" && git init -q && git remote add origin https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git &&
     git sparse-checkout set --no-cone /cpu/wl82/tools/uboot.boot /cpu/wl82/tools/cfg_tool.bin \
         '/cpu/wl82/tools/**/eq_cfg_hw.bin' /LICENSE '/*.md' &&
     git fetch -q --depth 1 --filter=blob:none origin tag AC79NN_SDK_V1.2.1_2023-12-13 &&
     git checkout -q AC79NN_SDK_V1.2.1_2023-12-13)
fi
python3 -c 'import PIL' 2>/dev/null || { echo "build needs Pillow: install python3-pil (or pip --target $TC/pylib Pillow)"; }
if ! PYTHONPATH="$TC/pylib" python3 -c 'import fontTools' 2>/dev/null; then
    echo "-- fontTools into $TC/pylib"
    python3 -m pip install -q --target "$TC/pylib" fonttools
fi
if [ "$WEB" = 1 ]; then
    if [ ! -x "$TC/node/bin/node" ]; then
        echo "-- Node.js 22.11.0"
        (cd "$TC" && curl -fsSL https://nodejs.org/dist/v22.11.0/node-v22.11.0-linux-x64.tar.xz | tar -xJ &&
         ln -sfn node-v22.11.0-linux-x64 node)
    fi
    if [ ! -f "$TC/emsdk/emsdk_env.sh" ]; then
        echo "-- Emscripten 3.1.74"
        (cd "$TC" && git clone -q --depth 1 https://github.com/emscripten-core/emsdk.git &&
         cd emsdk && ./emsdk install 3.1.74 >/dev/null && ./emsdk activate 3.1.74 >/dev/null)
    fi
fi
cat > "$DEST/env.sh" <<'ENV'
# Source this file to build BRUTAL FM. It only sets variables in the current shell;
# nothing global (another project's toolchain, ~/.jieli, the shell profile) is touched.
BFM_ROOT="$(cd "$(dirname "${BASH_SOURCE:-$0}")" && pwd)"
export JIELI_TOOLCHAIN="$BFM_ROOT/toolchain/jieli/toolchain"
export AC79_SDK="$BFM_ROOT/toolchain/fw-AC79_AIoT_SDK"
export PYTHONPATH="$BFM_ROOT/toolchain/pylib${PYTHONPATH:+:$PYTHONPATH}"
[ -f "$BFM_ROOT/toolchain/emsdk/emsdk_env.sh" ] && . "$BFM_ROOT/toolchain/emsdk/emsdk_env.sh" >/dev/null 2>&1
[ -d "$BFM_ROOT/toolchain/node/bin" ] && export PATH="$BFM_ROOT/toolchain/node/bin:$PATH"
true
ENV
echo "== done: . $DEST/env.sh && ./build.sh"
