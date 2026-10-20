# BRUTAL FM

M-VAVE FM-1 を、過激な FM 音源と COMPLEX ARP(押した鍵盤の音だけで複雑なアルペジオを生成して固定ループする
アルペジエーター)を持つ実験的シンセサイザーに変えるカスタムファームウェアです。インダストリアルテクノ、ノイズ、
実験音響、ダークアンビエント向けの、金属的な FM、唸る低音、フィードバックで暴走するカオス、音程からノイズへ
連続的に変わる音、デジタルグリッチ、長く変化し続けるドローンを狙っています。

- 4 オペレーター FM/PM。4×4 の変調マトリクス(自己フィードバック、双方向クロス FM、変調の極性)
- 4 つのモード: METAL / GROWL / CHAOS / NOISE(1 つの共通 FM コアのモード)
- サインウェーブフォールダー(FOLD / SYMMETRY / BIAS / MIX)、HQ(2 倍オーバーサンプリング)
- 音色 A と音色 B の MORPH(ルーティングを含むすべてのパラメーターを連続補間)
- COMPLEX ARP: 8 / 16 ステップ、押した鍵盤の音程だけ、生成は一度だけで再生は完全に固定。HOLD / FREEZE /
  RHYTHM LOCK / PITCH LOCK / TIMBRE LOCK / REGENERATE / UNDO
- FM-1 本体だけで全操作(PC エディター不要)。ファクトリープリセット 40、ユーザープリセット 32
- USB MIDI と TRS MIDI(ノート、CC、Program Change、MIDI Clock / Start / Stop / Continue)
- ブラウザーエミュレーター(PC 上で FM-1 のパネルを再現して音を出せる)

> **状態**: ソース、ホストテスト、ブラウザーエミュレーター、FM-1 用ファームウェアパッケージ(`brutal.fwsc`)まで
> 生成・検証済みです。**実機では一度も動かしていません(すべて未検証)**。書き込みの前に
> [USER_MANUAL.md](USER_MANUAL.md) の「導入の前に」と「純正ファームウェアへ戻す」を必ず読んでください。

## ベースにしたファームウェアと理由

BRUTAL FM は [hugelton/Felucca](https://github.com/hugelton/Felucca)(GPL-3.0-only、Leo Kuroshita / Hügelton
Instruments)の FM-1 ハードウェア層の上に作った、独立したファームウェアです。

- ベース: Felucca v1.6.1、コミット `5738617968a22b5d4c0d3470cfcc95559052f446`
- そのまま使ったもの: `firmware/hal/`(FM-1 のレジスター層)、`firmware/loader/`(アップデートローダー)、
  `firmware/drivers/` の LCD・描画・USB(MIDI とオーディオ)・TRS MIDI・フラッシュ保存・アップデート入口、
  ビルドとパッケージ生成のツール。変更したファイルには変更点と著作権表示を追記しています
- BRUTAL FM の部分(`firmware/brutal/`): 音源、COMPLEX ARP、プリセット、UI、メインループ

調査した候補と比較:

| | Felucca | JIANT | dxsloop | fm1_open (M-VAVE) |
| --- | --- | --- | --- | --- |
| 調査コミット | `5738617` | `e9692d7` | `eeec0a6` | `4d26685` |
| 実機での実績 | 多数のリリース | Felucca のフォーク | Felucca のフォーク | 公式 |
| ソースの公開範囲 | 全部 (GPL-3.0) | 全部 (GPL-3.0) | 全部 (GPL-3.0) | アプリのみ GPL-3.0。音源エンジンはバイナリ SDK (`libfm1_engine.a`、独自ライセンス) |
| ビルド環境 | Linux x86-64 ネイティブ (macOS は Docker) | 同左 | 同左 | Windows x64 + PowerShell のみ |
| エミュレーター / テスト | ブラウザーエミュレーター、ホストテスト約 5700 項目 | あり | あり | Web プレビュー |
| DSP の追加 | 単一翻訳単位、HAL がヘッダーのみで分離が明確 | 同左 (Felucca の拡張) | 同左 | エンジンが非公開 |
| USB MIDI / 保存 | あり / A/B セクターの CRC 付き保存 | あり | あり | あり |

Felucca を選んだ理由: 実機での実績が最も多く、ソースがすべて公開され、この Linux 環境でそのままビルドでき、
ブラウザーエミュレーターとホストテストがあり、ハードウェア層 (`hal/`) とドライバーがアプリから分離されていて、
音源とアプリを丸ごと入れ替えやすいこと。JIANT と dxsloop は Felucca から派生したもので、ハードウェア層は
同じです。fm1_open は音源エンジンが非公開のバイナリで、ビルドが Windows 専用なので対象外にしました。

Felucca は 4 トラックのシーケンサー、ドラム、サンプラーを持つ大きなアプリですが、BRUTAL FM はその層を使わず、
CPU とメモリを FM 音源に割り当てています(アプリ 154 KB、RAM 12 KB / 72 KB、プール 136 KB / 360 KB)。

## すぐに使う

```
. ../env.sh                    # BRUTAL FM 専用の環境 (tools/setup_env.sh が作る)
./build.sh                     # build/brutal.fwsc (FM-1 用パッケージ)
tests/run_tests.sh             # 全テスト
web/emu/build.sh               # ブラウザーエミュレーター
python3 -m http.server -d build/emu 8790     # http://localhost:8790/ を Chrome で開く
```

詳しくは [BUILDING.md](BUILDING.md)(開発環境とビルド)、[USER_MANUAL.md](USER_MANUAL.md)(操作)、
[ARCHITECTURE.md](ARCHITECTURE.md)(内部設計)、[TESTING.md](TESTING.md)(テストと測定結果)、
[LICENSING.md](LICENSING.md)(ライセンス)。

## ライセンス

GPL-3.0-only([LICENSE](LICENSE))。Felucca 由来のファイルは Felucca の著作権表示を保持しています。
フォント(Inter Tight、SIL OFL 1.1)と、パッケージに含まれる JieLi AC79 SDK の 3 ファイル(Apache-2.0)は
それぞれのライセンスです。詳細は [LICENSING.md](LICENSING.md) と [LICENSES/](LICENSES/)。

M-VAVE と FM-1 はそれぞれの所有者の商標です。BRUTAL FM は M-VAVE とも Hügelton Instruments とも関係がなく、
承認も受けていません。ファームウェアの書き込みは自己責任です。

Copyright (C) 2026 Hiro Ikezawa(Felucca 由来の部分は Copyright (C) 2026 Leo Kuroshita, Hügelton Instruments)
