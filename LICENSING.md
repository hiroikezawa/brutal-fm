# ライセンス

BRUTAL FM は GNU General Public License version 3 only(`GPL-3.0-only`、全文は [LICENSE](LICENSE))の
フリーソフトウェアです。

Copyright (C) 2026 Hiro Ikezawa

## 構成と著作権

| 部分 | 著作権 | ライセンス |
| --- | --- | --- |
| `firmware/brutal/`、`tests/`、`web/emu/`、`tools/gen_bfm_*.py`、`tools/check_package.py`、`tools/target_cost.py`、`tools/spectro.c`、`tools/setup_env.sh`、文書 | Hiro Ikezawa | GPL-3.0-only |
| `firmware/hal/`、`firmware/loader/`、`firmware/drivers/`、`firmware/crt0.S`、`firmware/app.ld`、`tools/build.py`、`tools/fm1pkg_make.py`、`tools/fm1_install.py`、`tools/lz4blk.py`、`tools/gen_aa_font.py`、`tools/aa_raster.py`、`tools/gen_aa_keycaps.py`、`tools/gen_ui_palettes.py`、`tools/get_toolchain.sh` | Leo Kuroshita (@kurogedelic), Hügelton Instruments(Felucca、`hugelton/Felucca` コミット `5738617968a22b5d4c0d3470cfcc95559052f446`) | GPL-3.0-only |
| 上の Felucca のファイルのうち BRUTAL FM が変更したもの(`storage.c`、`usb.c`、`gfx.c`、`loader.c`、`fm1_sys.h`、`build.py`、`fm1_install.py`) | Felucca の著作権表示を保持し、変更点と Hiro Ikezawa の表示を追記 | GPL-3.0-only |
| Inter Tight フォント(UI の文字。ビルド時にファームウェアへラスタライズ) | The Inter Project Authors | SIL OFL 1.1(`assets/fonts/OFL.txt`、`LICENSES/OFL-InterTight.txt`) |
| JieLi AC79 SDK の `uboot.boot`、`cfg_tool.bin`、`cfg/eq_cfg_hw.bin`(パッケージ `brutal.fwsc` に含まれる。ソースツリーには含まない) | Zhuhai Jieli Technology | Apache-2.0(`LICENSES/Apache-2.0.txt`) |

Felucca の音源(FM6、PHASE、Felucca-Engines など)、サンプル、アイコンフォントは BRUTAL FM に含まれていません。
BRUTAL FM の音源とアルペジエーターは独自に書いたものです。

## 配布するとき

- ファームウェア(`brutal.fwsc`、`brutal.bin`)を配布する場合は、GPL に従って対応するソース全体(このリポジトリ、
  ビルドスクリプトとツールを含む)を同じライセンスで提供してください
- パッケージには Apache-2.0 の SDK ファイルが入っているので、`LICENSES/Apache-2.0.txt` を一緒に配布してください
  (`./build.sh --release X.Y` は `build/release-X.Y/` にライセンス一式を入れます)
- USB の VID/PID は Felucca と同じ pid.codes のテスト用 ID(0x1209/0x0001)のままです。公開配布の前に正式な
  ID を取得するかどうかを決めてください

M-VAVE と FM-1 はそれぞれの所有者の商標です。BRUTAL FM は M-VAVE とも Hügelton Instruments とも関係がなく、
承認も受けていません。
