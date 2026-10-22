# BRUTAL FM ユーザーマニュアル

画面とボタンの表記は英語です(FM-1 の画面フォントに日本語が無いため)。このマニュアルでは画面の表記を
そのまま `MORPH` のように書きます。

> **未検証**: BRUTAL FM はまだ実機で一度も動かしていません。ここに書いた操作はブラウザーエミュレーターと
> ホストテストで確認したもので、実機での動作、ボタンとエンコーダーの割り当て、CPU 負荷、音量、USB の
> 認識は確認していません。

---

## 1. 導入の前に

### 1.1 バックアップ

BRUTAL FM を書き込むと、今のファームウェアのデータを BRUTAL FM から開くことはできません。また BRUTAL FM は
フラッシュのうち Felucca が使っている領域(設定 0xFC000〜、プリセット 0xC0000〜0xC9FFF)に書き込みます。
**書き込みの前に、今のデータを必ずパソコンへ保存してください。**

| 今のファームウェア | バックアップの方法 |
| --- | --- |
| M-VAVE 純正(V13〜V15、FM1 Open) | M-VAVE の Workbench(`https://open.m-vave.com/fm1-workbench`)でプロジェクト、DX7 ボイス(グループ A〜D を別々に)、サンプルバンクを書き出す。M-VAVE も「アップデート前にプロジェクト、ボイス、サンプルをバックアップする」よう案内しています |
| Felucca | Felucca の Web エディター / Web インストーラーの「全体バックアップ」(`Felucca-backup-日付.json`) |
| 他のファームウェア | そのファームウェアの方法で |

あわせて、純正ファームウェアのパッケージ `FM-1.fwsc`(V15、未改造のもの)を M-VAVE のダウンロードページ
(`https://www.m-vave.com/download`)から入手して手元に置いてください。戻すときに使います。

BRUTAL FM の中のユーザープリセットはパソコンへ書き出す機能がありません(本体の中だけ)。

### 1.2 注意

- ファームウェアの書き込みは自己責任です。書き込み中は USB ケーブルを抜かず、電源を切らないでください
- USB の VID/PID は Felucca と同じテスト用の番号です(配布用の正式な番号ではありません)
- 書き込み用パッケージ: `build/brutal.fwsc`(製品 ID `FM-1_700`)

## 2. インストール

`tools/fm1_install.py`(Felucca のコマンドラインインストーラー。BRUTAL FM のポート名にも対応させたもの)を、
FM-1 を USB でつないだパソコン(Windows / macOS / Linux、USB MIDI が見える OS)で実行します。

```
pip3 install mido python-rtmidi
python3 tools/fm1_install.py --info                   # つながっている FM-1 の識別名を表示
python3 tools/fm1_install.py build/brutal.fwsc        # 書き込み
```

WSL2 からは USB MIDI が見えないので、Windows 側の Python で実行してください。他の MIDI アプリはすべて閉じて
ください。インストーラーは FM-1 を更新モードへ切り替え、パッケージを書き、再起動後に識別名が `FM-1_700` に
なったことを確かめます。

起動すると `BRUTAL FM` のスプラッシュのあと PERFORM ページになり、F01 RUST BELL が選ばれています。
パソコンからは MIDI ポート `BRUTAL FM-1` として見えます(USB オーディオ出力もあります)。

## 3. 純正ファームウェアへ戻す

1. **コマンドライン(推奨)**: 純正の未改造 `FM-1.fwsc` をそのまま書きます。インストーラーは SHA-256 で
   未改造の V15 であることを確認します。
   ```
   python3 tools/fm1_install.py FM-1.fwsc
   ```
2. **Felucca の Web インストーラー**(`https://hugelton.github.io/Felucca/`)の「Return to official V15」。
   このページは先に Felucca 形式の全体バックアップを取ろうとしますが、BRUTAL FM はその形式に対応していません。
   バックアップは 1.1 で済ませてあるので、「I already saved a complete backup」(再開)を選んで進めます
   (この手順は未検証です)
3. **M-VAVE の公式アップデーター / Workbench**
4. **起動しない場合**: OCT- と OCT+ を 5 秒押し続けると UBOOT(チップ内蔵の更新モード)に入ります
   (BRUTAL FM が動いている間)。起動直後に 2 回続けて落ちた場合も自動で UBOOT に入ります。パソコンに
   `WL82 UBOOT1.00`(ID 4C4A:8057)が見えたら、M-VAVE の手順で純正を書くか、次のどちらかで復旧します
   - **Windows だけで**(2026-10-21 に実機で使用): `tools/uboot/` の 2 本のスクリプトと
     [jl-uboot-tool](https://github.com/kagaimiq/jl-uboot-tool)(MIT)を同じフォルダーに置き、管理者の
     PowerShell で `fm1_backup.py`(フラッシュ全体 1 MiB を 2 回読んで保存。書き込みなし)、
     `fm1_write.py 目的のflash.bin バックアップ.bin`(予行演習)、同じく `--write`(書き込み)の順に実行します。
     書くのはアプリ領域 0x4000〜0x93000 の変わったセクターだけで、先頭と保存データには触れません。
     UBOOT は Mac では BRUTAL FM に戻ってしまいました(Windows では入ったまま待ちます)
   - Felucca の案内にある FM-1 Transporter(`https://github.com/kurogedelic/FM-1-transporter`、XIAO RP2040 が必要)

## 4. 操作子

```
 MASTER  SELECT          [  画面  ]        KNOB 1  KNOB 2  KNOB 3  KNOB 4
 PRESETS ALGORITHM                         FX  SCL  ENV  LFO  EDIT GLO
 OCT-  OCT+                                HOME SAVE ARP SEQ  PLAY REC
 [ 鍵盤 27 鍵 F3 .. G5 ]
```

| 操作子 | 働き |
| --- | --- |
| MASTER | 音量(ポット) |
| SELECT | テンポ(BPM 40〜255) |
| PRESETS | プリセットを選ぶ(F01〜F40 ファクトリー、U01〜U32 ユーザー)。回して止めると 0.2 秒後に読み込む |
| ALGORITHM | 変調ルーティングのテンプレート(STACK / PAIRS / FAN / TRIPLE / X-FM / RING / MESH / LOOP)を編集中の音色のマトリクスに書く |
| KNOB 1〜4 | 画面の 4 つのパラメーター。速く回すと 2 倍の速さで動く |
| 鍵盤 | 演奏(F3〜G5、ベロシティ 100 固定)。ARP がオンのときは ARP への入力 |
| OCT- / OCT+ | オクターブ(−2〜+2)。両方同時でリセット。両方 5 秒で UBOOT(更新モード) |
| HOME | PERFORM ページ。1 秒押し続けると全音停止 |
| ARP | ARP ページ(押すたびに次の ARP ページ) |
| SEQ | ARP のステップ編集 |
| EDIT | オペレーター、変調マトリクス、固定周波数、位相(押すたびに次) |
| ENV | オペレーターのエンベロープ |
| LFO | 変調(LFO、DRIFT、ボイス) |
| FX | ウェーブフォールダー、DRIVE / CRUSH / DELAY、MODE |
| GLO | 同期と MIDI、CC、システム、音色ツール |
| SAVE | SAVE ページ。SAVE ページでもう一度押すと書き込み |
| SCL | 編集する音色: A → B → A+B(ヘッダーの A / B / A+B 表示と SCL の LED: ほのか A、点灯 B、中くらい A+B) |
| PLAY | ARP オン / オフ |
| REC | 押してすぐ離す: REGENERATE。0.6 秒以上押す: UNDO |
| ARP を押したまま FX / SCL / ENV / LFO / EDIT / GLO | HOLD / FREEZE / RHYTHM LOCK / PITCH LOCK / TIMBRE LOCK / UNDO(押している間、各ボタンの LED が状態を示す) |

ページボタンは、そのグループで最後に見ていたページを開きます。同じボタンをもう一度押すと次のページです。

画面: 上がページ名、編集中の音色(A / B / A+B)、ARP の状態(ARP / HOLD)、テンポ(MIDI クロックに同期
しているときは EXT)。中段が 4 つのノブ(名前、値、バー、ノブ番号)。下段がページごとの表示と、
プリセット番号・名前(`*` は読み込み後に変更あり)・モード。

LED: 暗いステージでも位置が分かるよう、すべてのボタンと鍵盤が常にほのかに光ります。その上で 3 段階の明るさで
状態を示します。

- 点灯: 開いているページのボタン、PLAY(ARP 再生中)、OCT(オクターブがずれているとき)、鳴っている音と押している鍵盤
- 中くらい: PLAY(ARP オンで待機)、REC(パターンあり)、ARP の入力音の鍵盤
- REC の点滅: 次のループで切り替わるパターンあり

## 5. ページとパラメーター

値は 0〜127 が基本です。±表示は 64 が 0 の双方向の値です。

### HOME: PERFORM

| ノブ | 名前 | 内容 |
| --- | --- | --- |
| 1 | MORPH | 音色 A(0)と音色 B(127)の間。ルーティングを含むすべての音色パラメーターが連続して変わる |
| 2 | INTENS(INTENSITY) | FM の深さ。順方向の変調すべての倍率(64 = 書いた通り、127 ≒ 3.9 倍、0 = 変調なし) |
| 3 | FEEDBK(FEEDBACK) | 自己フィードバックと逆方向(クロス)の変調の倍率(同上) |
| 4 | MOVE(MOVEMENT) | LFO と DRIFT による動きの量(64 = 書いた通り、0 = 静止、127 = 2 倍) |

下段: A〜B の MORPH の位置と出力の波形。

### ARP(4 ページ)

| ページ | ノブ 1 | ノブ 2 | ノブ 3 | ノブ 4 |
| --- | --- | --- | --- | --- |
| ARP GENERATE | DENSITY 発音密度 | COMPLEX 音順とリズムの複雑さ | G.VAR ゲート長の変化幅 | T.VAR 音色変化の幅 |
| ARP TIME | LENGTH 8 STEP / 16 STEP | RATE 1/32 1/16T 1/16 1/8T 1/8 1/4 | GATE 基準ゲート 1〜100 % | WINDOW 同時押しの受付時間 0〜508 ms(既定 80 ms) |
| ARP LOCKS | HOLD | FREEZE | R.LOCK(RHYTHM LOCK) | P.LOCK(PITCH LOCK) |
| ARP MORE | T.LOCK(TIMBRE LOCK) | SEED 今のパターンのシード(回すとそのシードで生成) | ARP オン / オフ | SYNC INT / EXT / AUTO |

下段: パターン(8 または 16 マス。音名、ゲート長のバー、右上の四角 = アクセント、右下 = タイ、左の縦線 =
FM オフセット)、再生位置、入力音、LOCK の状態、`NEXT`(次のループで切り替わる)。

### SEQ STEPS

生成されたパターンを 1 ステップずつ直します。

| ノブ | 内容 |
| --- | --- |
| STEP | 編集するステップ(枠で表示) |
| NOTE | そのステップの音: REST か、入力された音のどれか(入力に無い音は選べない) |
| S.GATE | そのステップのゲート 1〜100 % |
| S.FM | そのステップの FM オフセット −64〜+63 |

### EDIT(10 ページ)

- **OP1〜OP4**: RATIO(周波数比 0.25x〜16x、非整数比 1.06 1.41 1.73 2.24 2.72 3.14 4.2 5.4 6.28 9.42 11.2 などを含む
  36 種)、FINE(±128 セント、2 セント刻み)、LEVEL(オペレーターの振幅。変調の深さにもキャリアの音量にも効く)、
  OUT(出力へ出す量。0 なら純粋なモジュレーター)
- **OP4 MODULATES 〜 OP1 MODULATES**: そのオペレーターが OP1〜OP4 をどれだけ変調するか(−127〜+127)。
  自分自身への値は `FB`(自己フィードバック)。0 を下に越えると負(位相を逆に変調する)。下段の 4×4 の図で、
  明るい四角が正、暗い四角に横線が負
- **FIXED FREQ**: FIX 1〜4。RATIO のかわりに固定周波数(1 Hz〜9 kHz、鍵盤の音程に関係なく一定)。RATIO が比率モード
- **PHASE RESET**: PH 1〜4。FREE = 位相を続ける。0〜357 deg = ノートオンでその位相から始める(アタックが毎回同じになる)

### ENV(4 ページ: ENV OP1〜OP4)

ATK(アタック 0.3 ms〜12 s)、DEC(ディケイ 1 ms〜30 s)、SUS(サスティン)、REL(リリース 1 ms〜30 s)。
下段に形を表示。

### LFO(3 ページ)

| ページ | ノブ 1 | ノブ 2 | ノブ 3 | ノブ 4 |
| --- | --- | --- | --- | --- |
| MOD LFO | SHAPE SINE / TRI / SAW / SQUARE / S&H / CHAOS | RATE 0.02〜40 Hz | >INTENS LFO → INTENSITY(±) | >FOLD LFO → FOLD(±) |
| MOD DRIFT | RATE DRIFT の速さ | >MORPH DRIFT → MORPH(±) | >FDBK DRIFT → FEEDBACK(±) | VIBRATO LFO → 音程(±) |
| MOD VOICE | VELO ベロシティの効き | BEND ピッチベンド幅 0〜24 半音 | GLIDE ポルタメント | VOICE POLY / MONO(MONO はレガート) |

DRIFT は滑らかに漂うランダムな変化です。CHAOS 形の LFO はロジスティック写像で、周期ごとに不規則な値に跳びます。
すべての深さは PERFORM の MOVE で一括して増減します。

### FX(3 ページ)

| ページ | ノブ 1 | ノブ 2 | ノブ 3 | ノブ 4 |
| --- | --- | --- | --- | --- |
| WAVEFOLDER | FOLD 折り返しの量 | SYMM 正負の非対称(±) | BIAS 片寄せ(±) | MIX 原音との混合 |
| DRIVE DELAY | DRIVE 飽和 | CRUSH サンプルレートとビット数の削減 | D.TIME ディレイ時間 1〜367 ms | D.FDBK ディレイのフィードバック |
| MODE | D.MIX ディレイの量 | MODE METAL / GROWL / CHAOS / NOISE | CHAR モード固有の量 | GAIN 音色の音量 −37.5〜+10 dB |

FOLD、SYMM、BIAS、MIX、CHAR、GAIN は音色 A / B ごとの値です。下段にフォールダーの入出力の曲線を表示します。
高い音(A4 より上)ではフォールドが自動的に浅くなり、エイリアスを抑えます。

MODE と CHAR:

- **METAL**: CHAR でオペレーターの周波数比を互いに広げる(非整数倍音、ベル、金属)
- **GROWL**: CHAR で基音とその 1 オクターブ下を足す(深い変調でも低音が残る)
- **CHAOS**: CHAR でクロスとフィードバックの経路を折り曲げ、出力を位相へ戻す(不安定、暴走)
- **NOISE**: CHAR でノイズを位相に注入し、オペレーターのビット数を落とす(グリッチ、デジタルノイズ)

### GLO(4 ページ)

| ページ | ノブ 1 | ノブ 2 | ノブ 3 | ノブ 4 |
| --- | --- | --- | --- | --- |
| SYNC MIDI | SYNC INT / EXT / AUTO | BPM 40〜255 | MIDI CH OMNI / 1〜16 | QUALITY ECO / HQ 2x |
| MIDI CC | MORPH の CC 番号(既定 1) | INTENS(既定 74) | FEEDBK(既定 71) | MOVE(既定 76) |
| SYSTEM | CC IN オン / オフ | PC IN(Program Change)オン / オフ | COLOR 画面の配色 | VOICES 同時発音数 2〜8(HQ はその半分) |
| SOUND TOOLS | A>B 音色 A を B へ複製 | B>A | SWAP A と B を入れ替え | INIT 編集中の音色を初期化 |

SOUND TOOLS はノブを右へ 4 クリック回すと実行します(バーが満ちる)。GLO ページの設定(プリセットに
含まれないもの)は、変更から 1.5 秒後、ARP が止まっているときに自動で保存されます。下段に MIDI クロックの
状態、使用中のボイス数、CPU 負荷を表示します。

### SAVE

SLOT(U01〜U32、今そこにあるプリセット名を下段に表示)、POS(名前の文字位置)、CHAR(その文字。
A〜Z 0〜9 - + . # / * と空白)。SAVE ページでもう一度 SAVE を押すと書き込み、`SAVED U05` のように表示します。
保存したプリセットは次回の起動時に読み込まれます。

プリセットに含まれるもの: モード、オペレーター、ルーティング(極性を含む)、フィードバック、フォールダー、
A / B の両方の音色、MORPH などのマクロ、エンベロープ、変調、エフェクト、ARP の設定、ステップ数、生成済みの
パターン(ステップのデータそのもの)、シード、ベロシティ・ベンド・グライド。含まれないもの: SYNC、MIDI
チャンネル、CC 番号、QUALITY、VOICES、BPM、配色(GLO の設定)。

## 6. COMPLEX ARP

### 6.1 基本

1. PLAY を押して ARP をオンにする(PLAY が中くらいの明るさで光る)。画面は PERFORM ページに切り替わり、
   4 つのノブで音色(MORPH / FM の深さ / フィードバック / 動き)を変えられる
2. 鍵盤をいくつか押す。最初の鍵盤ですぐ鳴り始め、受付時間(WINDOW、既定 80 ms)のうちに押した鍵盤が
   1 つの和音になり、その音だけでパターンができる
3. 鍵盤を離しても鳴り続ける(HOLD は最初からオン)。新しく押した鍵盤が次の和音になる。止めるときは
   PLAY をもう一度押す。HOLD は ARP を押したまま FX で切り替え

最初の状態は **8 ステップ、休符なし**(DENSITY が最大の 127 のとき、すべてのステップが鳴る)。休符を
入れたいときは ARP GENERATE ページで DENSITY を下げ、REC(REGENERATE)で作り直す。16 ステップは
ARP TIME ページの LENGTH。

**生成されるのは押した鍵盤の音程だけです。** オクターブ違いの音も、経過音も足しません(FM による倍音は
音色の一部として出ます)。パターンは一度生成したら固定で、何周しても同じ内容を繰り返します。再生中に
確率で音を変えることはありません。

### 6.2 生成の設定

DENSITY(打点の多さ。127 で休符なし)、COMPLEX(低いと短いモチーフの繰り返しと隣の音への進行、高いとモチーフが長く
跳躍が多い)、G.VAR(ゲートのばらつき)、T.VAR(ステップごとの FM / FEEDBACK / MORPH の変化の幅)、GATE
(基準のゲート長)。これらは**次に生成するパターン**の設定で、今鳴っているパターンは変わりません。
REGENERATE で反映されます。

### 6.2.1 プリセットを変えても止まらない

ARP が鳴っている間にプリセットを変えると、変わるのは**音色だけ**です。押さえている和音、HOLD、パターン、
再生位置、テンポ、ARP の設定はそのまま続き、次のステップから新しい音色で鳴ります。そのプリセットに保存
されている ARP の設定とパターンを使いたいときは、ARP を止めてから読み込みます。

### 6.3 和音を変える

- 鍵盤を押し足す、離す(HOLD オフ)、HOLD 中に全部離してから新しく押す → 受付時間で和音をまとめ、
  **次のループの頭**で新しいパターンに切り替わる(REC が点滅し、画面に `NEXT`)
- 同じ鍵盤を押し直しただけなら何も変わらない
- WINDOW を長くすると、1 音ずつゆっくり押して和音を作れる(最後の押鍵から測る)
- 入力は最大 16 音(それより多いと低い方から 16 音)

### 6.4 再生成とロック

| 操作 | 結果 |
| --- | --- |
| REGENERATE(REC) | 今の入力音で新しいパターン(シードが次へ進む)。再生中は次のループで切り替わる |
| UNDO(REC 長押し、ARP + GLO) | 直前のパターンへ(4 回まで) |
| FREEZE | 再生成しない。和音を変えても、リズム・ゲート・音色・音の並び(入力音の何番目か)はそのままで、音だけ新しい和音に入れ替わる。REGENERATE は効かない |
| RHYTHM LOCK | 再生成でリズム(打点、ゲート、アクセント、タイ)を残し、音程と音色を新しくする |
| PITCH LOCK | 音の並び(何番目の音を何番目に鳴らすか)を残し、リズムと音色を新しくする |
| TIMBRE LOCK | ステップごとの FM / FEEDBACK / MORPH を残す |
| LENGTH の切り替え | 新しい長さのために生成し直す(16 の前半を切り出すのではない)。PITCH LOCK だけは引き継ぐ |
| SEED(ARP MORE) | シードを直接選ぶ。同じ入力・設定・シードからは常に同じパターンができる |

### 6.5 テンポと同期

- SYNC INT: SELECT のテンポで動く
- SYNC EXT: MIDI クロック(USB または TRS)だけで動く。Start でステップ 1 から、Continue で続きから、Stop で
  止まる。クロックが 300 ms 来ないと停止とみなし、音を止める
- SYNC AUTO(既定): クロックが来ている間はそれに従い、途切れたら最後に測ったテンポで内部クロックが
  引き継いで鳴り続ける
- 1 ステップ = RATE(既定 1/16)。ゲートは 1 ステップの長さに対する %

Ableton Move などから MIDI クロックを送らなくても、本体だけで演奏できます(INT か AUTO)。

## 7. MIDI

| 受信 | 内容 |
| --- | --- |
| Note On / Off | 演奏(ARP オンなら ARP の入力)。MIDI CH の設定(既定 OMNI) |
| CC(番号は設定可能) | MORPH(1)、INTENSITY(74)、FEEDBACK(71)、MOVEMENT(76) |
| CC 120 / 123 | 全音停止 |
| Program Change 0〜71 | 0〜39 = F01〜F40、40〜71 = U01〜U32(PC IN オンのとき) |
| Pitch Bend | ±BEND 半音 |
| MIDI Clock / Start / Continue / Stop | SYNC EXT / AUTO のとき |

USB MIDI と TRS MIDI IN は同時に使えます(1 つの受信キューにまとまります)。MIDI 出力はありません。
USB オーディオでパソコンへ出力を録音できます(16 bit ステレオ、44.1 / 48 kHz、Felucca の機能)。

## 8. ファクトリープリセット

| 番号 | METAL | 番号 | GROWL | 番号 | CHAOS | 番号 | NOISE | 番号 | DRONE |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| F01 | RUST BELL | F09 | IRON BASS | F17 | FDBK HELL | F25 | STATIC HIT | F33 | IRON DRONE |
| F02 | ANVIL HIT | F10 | WOBBLE GUT | F18 | ATTRACTOR | F26 | CLICK STORM (ARP) | F34 | VOID HUM |
| F03 | SHEET STEEL | F11 | REESE FM | F19 | CROSSFIRE | F27 | DATA STORM | F35 | FURNACE |
| F04 | GAMELAN X (ARP) | F12 | SUB GRINDER | F20 | RUNAWAY | F28 | WHITE METAL | F36 | CATHEDRAL X |
| F05 | SAW BLADE | F13 | ACID GROWL (ARP) | F21 | DEAD RADIO | F29 | CRUSH HAT (ARP) | F37 | SLOW MELT |
| F06 | CHAIN DRAG | F14 | DIESEL | F22 | ORBIT DECAY | F30 | TAPE SHRED | F38 | TURBINE |
| F07 | GONG PIT | F15 | BIT BASS | F23 | MUTANT LEAD | F31 | NOISE SWEEP | F39 | DEAD SIGNAL |
| F08 | TIN SNARE (ARP) | F16 | SLUDGE | F24 | CHAOS ARP (ARP) | F32 | ERROR TONE | F40 | OCEAN RUST |

(ARP) は ARP がオンの状態で読み込まれます(鍵盤を押すとすぐ ARP)。すべてのプリセットは音色 A と B を持ち、
MORPH で変化します。A と B、プリセット同士の音量はそろえてあります。

## 9. 音質と負荷

- QUALITY HQ 2x(既定): 2 倍オーバーサンプリング。エイリアスが少ないが、同時発音数は VOICES の半分(既定 3)
- QUALITY ECO: 1 倍。VOICES の数だけ鳴る(既定 6)
- 負荷が 85 % を超えると、古いボイスから自動で止めます。GLO ページの下段に CPU 負荷が出ます
- 実機での CPU 負荷は未測定です(TESTING.md の見積もり参照)。音が途切れる場合は VOICES を減らすか ECO に

## 10. 困ったとき

| 症状 | 対処 |
| --- | --- |
| 鍵盤を押しても ARP が鳴らない | SYNC が EXT でクロックが来ていない(INT か AUTO にする) |
| 音が出ない | MASTER、プリセットの GAIN、ARP オンで鍵盤を押していないか |
| 音が途切れる | VOICES を減らす、QUALITY を ECO に、DELAY を切る |
| 和音を変えてもすぐ変わらない | 仕様(次のループの頭で切り替わる)。FREEZE がオンだと構造は変わらない |
| REGENERATE が効かない | FREEZE がオン(`FROZEN` と表示)、または入力音が無い |
| 起動しない | 3 章の 4 |
