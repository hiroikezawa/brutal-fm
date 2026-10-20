# BRUTAL FM のビルド

## 構成

この開発環境は、同じマシンの他のプロジェクト(Schwung など)から完全に分離しています。

```
<作業ディレクトリ>/            (この環境では /home/pepsi/brutal-fm)
  brutal-fm/                   このリポジトリ
  toolchain/                   BRUTAL FM 専用のツール (tools/setup_env.sh が作る)
    jieli/toolchain/           JieLi Linux ツールチェーン (pi32v2 用 clang 4.0.1)
    fw-AC79_AIoT_SDK/          JieLi AC79 SDK のうちパッケージに必要な 3 ファイルだけ (sparse checkout)
    pylib/                     システムに無い Python パッケージ (fontTools)
    node/                      Node.js 22.11.0 (Web テスト、任意)
    emsdk/                     Emscripten 3.1.74 (ブラウザーエミュレーター、任意)
  env.sh                       `. env.sh` でこのシェルだけに変数を設定する
  upstream/                    調査用に取得した Felucca / JIANT / dxsloop / fm1_open (ビルドには不要)
  work/felucca-vanilla/        元の Felucca を無改造でビルド・テストした作業コピー
```

システム全体へのインストール、シェルの設定ファイル (`~/.bashrc` 等) の変更、`~/.jieli` への展開は一切
行いません。`env.sh` を読み込んだシェルだけで `JIELI_TOOLCHAIN`、`AC79_SDK`、`PYTHONPATH`、`PATH` が
変わります。既にあった共通ツール(git、python3 + Pillow、gcc、make、curl)はそのまま使います。

## 実行環境と使ったバージョン

| 項目 | バージョン / 入手先 |
| --- | --- |
| OS / CPU | Linux 6.18 (WSL2, Ubuntu) / x86-64 |
| JieLi ツールチェーン | `jieli-linux-toolchains-20260730.1`(clang 4.0.1、pi32v2)、`https://pkgman.jieliapp.com/s/linux-toolchain`(JieLi 公式パッケージサーバー。Felucca の `tools/get_toolchain.sh` と同じ取得元) |
| JieLi AC79 SDK | タグ `AC79NN_SDK_V1.2.1_2023-12-13`(コミット `d179b4484759423312073f5fbb232501aa491047`)、`https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK`(JieLi 公式)。`uboot.boot`、`cfg_tool.bin`、`cfg/eq_cfg_hw.bin` の 3 ファイルのみ。SHA-256 は `tools/build.py` が Felucca の検証済みの値と照合 |
| Python | 3.14(システム)、Pillow 12.1.1(システム)、fontTools 4.67.0(`toolchain/pylib`、PyPI) |
| ホストコンパイラ | GCC 15.2.0(システム) |
| Node.js | 22.11.0、`https://nodejs.org/dist/v22.11.0/`(公式) |
| Emscripten | 3.1.74、emsdk `35659964bc2762fffec0d257997fb61475f765b7`、`https://github.com/emscripten-core/emsdk`(公式) |
| Docker | 不要(Linux x86-64 ではツールチェーンがネイティブで動く) |

ツールのバージョンは Felucca の BUILDING.md の指定(clang 4.0.1 for pi32v2、AC79NN_SDK_V1.2.1)に合わせています。

## 環境を作る

```
cd brutal-fm
tools/setup_env.sh              # ../toolchain と ../env.sh を作る (既にあるものは飛ばす)
tools/setup_env.sh --no-web     # Node.js と Emscripten を入れない (ファームウェアのビルドだけ)
```

Pillow が無い場合は `python3-pil` をシステムに入れるか、`python3 -m pip install --target ../toolchain/pylib Pillow`。

## ファームウェアのビルド

```
. ../env.sh
./build.sh                      # build/brutal.fwsc, build/brutal.bin, build/loader/ota.bin
./build.sh --release 0.1        # 製品 ID FM-1_701、build/release-0.1/ にパッケージ・アプリ・SHA256SUMS・ライセンス
```

出力:

| ファイル | 内容 |
| --- | --- |
| `build/brutal.bin` | アプリ本体(XIP 0x02000120 から実行) |
| `build/loader/ota.bin` | アップデートローダー(Felucca のもの。ID だけ `ota-FM-1_700`) |
| `build/brutal.fwsc` | インストール用パッケージ(アプリ + ローダー + SDK の SPL、M-UPGRADE 形式) |
| `build/brutal.dis` | 逆アセンブル(CPU コストの静的見積もりに使う) |

`tools/build.py` はビルドの最後に Felucca と同じ検査をします: RAM で動くコード(フラッシュドライバー)に
外部呼び出しが無いこと、第 2 コアのコードが無いこと、RAM と POOL の空き、ハードウェアレジスターへの
アクセスが `hal/` の中だけであること。1 つでも外れるとパッケージを作りません。

ビルドオプション(環境変数、`0` / `1`):`FELUCCA_FLASH`(保存、既定 1)、`FELUCCA_OTA`(アップデート入口、
既定 1)、`FELUCCA_UAC`(USB オーディオ出力、既定 1)、`BFM_OPT`(最適化、既定 `-O2`)。

パッケージの検査(実機なし):

```
python3 tools/check_package.py build/brutal.fwsc --ref ../work/felucca-vanilla/build/felucca.fwsc
```

製品 ID(FM-1_7XY)、ファイル表と CRC、`flash.bin` の大きさ(0x93000)、アプリ領域を復号するとアプリと
1 バイトも違わないこと、ローダーとその目印、参照パッケージ(Felucca)とフラッシュヘッドが同じことを確かめます。

## ブラウザーエミュレーター

```
. ../env.sh
./build.sh                      # (build/gen が要る)
web/emu/build.sh                # build/emu/{index.html, worklet.js, brutal.wasm}
python3 -m http.server -d build/emu 8790
```

Chrome / Edge で `http://localhost:8790/` を開き、START AUDIO を押します。

## テスト

```
tests/run_tests.sh              # 全部 (ASan / UBSan 付き、約 40 秒)
QUICK=1 tests/run_tests.sh      # サニタイザー無し
```

内容と結果は [TESTING.md](TESTING.md)。

## 元の Felucca の無改造ビルド(検証済み)

BRUTAL FM の実装に入る前に、元の Felucca を無改造でビルドしてテストしました(`work/felucca-vanilla`)。

| 項目 | 結果 |
| --- | --- |
| ホスト向けコードのビルド | 成功 |
| 既存テスト (`tests/run_tests.sh`) | 成功: 5757 項目すべて ok。ただし GCC 15 では `tests/drumfuzz_test.c` が `<stdint.h>` を読まずに `uint32_t` を使っていてコンパイルできず、作業コピーにだけ `#include <stdint.h>` を 1 行足した(上流のテストの移植性の問題で、ファームウェアには影響しない) |
| Web テスト (Node.js) | 成功 |
| ブラウザーエミュレーター | ビルド成功 (793 KB wasm)、エミュレーターのテスト成功 |
| 音声の生成 | 成功(ホストのレンダリングとエミュレーターのテスト) |
| 実機用ファームウェア | 成功: `felucca.fwsc` 610,090 バイト、ID FM-1_900 |

## 他のプロジェクトとの共存

- BRUTAL FM のビルドに使うものはすべて `<作業ディレクトリ>/toolchain/` にあり、`env.sh` は読み込んだシェルにしか
  効きません。Schwung のツールチェーン、環境変数、依存ライブラリには触れていません(この環境で作業前後の
  Schwung リポジトリの状態が同じであることを確認済み)
- `build.sh` は `JIELI_TOOLCHAIN` / `AC79_SDK` が未設定でも `../toolchain/` を見るので、`env.sh` を読まずに
  ビルドしても他のプロジェクトの設定は使いません
