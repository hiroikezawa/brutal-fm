#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Hiro Ikezawa (after Felucca's build.sh, (C) 2026 Leo Kuroshita, Hügelton Instruments)
# Build BRUTAL FM (see BUILDING.md).
#   ./build.sh [--release X.Y]
#   JIELI_TOOLCHAIN  JieLi Linux toolchain     AC79_SDK  JieLi AC79 SDK checkout
# Both default to this project's own copies (toolchain/ beside the repository, set up by tools/setup_env.sh),
# so building BRUTAL FM never touches another project's toolchain. `. ../env.sh` sets them explicitly.
set -e
cd "$(dirname "$0")"
HERE="$(pwd)"
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HERE/../toolchain/jieli/toolchain}"
export AC79_SDK="${AC79_SDK:-$HERE/../toolchain/fw-AC79_AIoT_SDK}"
PY="${PYTHON:-python3}"
[ -d "$HERE/../toolchain/pylib" ] && export PYTHONPATH="$HERE/../toolchain/pylib${PYTHONPATH:+:$PYTHONPATH}"

"$PY" -c 'import PIL, fontTools' 2>/dev/null || { echo "build.sh: $PY needs Pillow and fontTools (tools/setup_env.sh)"; exit 1; }
[ -x "$JIELI_TOOLCHAIN/pi32v2/bin/clang" ] || { echo "build.sh: no toolchain in $JIELI_TOOLCHAIN (tools/setup_env.sh)"; exit 1; }
[ -f "$AC79_SDK/cpu/wl82/tools/uboot.boot" ] || { echo "build.sh: no JieLi AC79 SDK in $AC79_SDK (tools/setup_env.sh)"; exit 1; }
if [ "$(uname -s)" != Linux ] || [ "$(uname -m)" != x86_64 ]; then
    docker info >/dev/null 2>&1 || { echo "build.sh: Docker is not running"; exit 1; }
fi
exec "$PY" tools/build.py "$@"
