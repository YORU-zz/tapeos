# TapeOS（Sharp X1 版）

> **暫定版 2026-11-24** ― これは開発中スナップショットの先行公開です。
> 内容は予告なく変更されることがあります。また本リポジトリは暫定版の
> 配布場所で、再現プロジェクト完了後の本公開の際には、正本リポジトリへ
> 統合／移行される予定です。
>
> なお、検証はエミュレータ上で行っており、**実機（Sharp X1）での
> 動作は未検証です**。実機で試された方の動作報告を歓迎します。

TapeOS は、カクヨム連載小説『還暦間際のオッサンが8Bit時代に戻って
不完全燃焼をやり直す話』（辺根除音 氏）に登場する架空の OS
「OUROBO/ROS」の前身として描かれた、テープ時代のシングルタスク OS です。
本リポジトリは、その TapeOS を実在のレトロ PC **Sharp X1** 向けに
再現したソースツリーです（OUROBO/ROS 再現プロジェクトの一成果物）。

## 対象機種

**対象は Sharp X1 のみです。**

プロジェクト内には MZ-80B／MZ-80K 版と PC-8801 版の TapeOS も存在
しますが、本公開の範囲には含めていません（他機種版は本公開または
後続のスナップショットで扱います）。

## 構成

TapeOS は「機械語モニタ＋α」（原作作中の自己評）――モニタシェルを
中心に、IOCS（入出力コールインタフェース）、FD／テープ／RAMDISK の
メディア層、TOSD ファイルシステムを持つ小さな OS です。

- **シェル**（`src/tapeos.z80` → `src/shell.inc` ほか）：メモリ操作、
  ロード／実行、メディア操作などのコマンド群。拡張 IOCS による
  メモリ管理（EP.31 由来）や常駐デバッグスタブも含まれます。
- **IOCS**：アドレス固定のジャンプテーブル経由で呼ばれる入出力
  インタフェース。FD（TOSD 形式）、カセットテープ、EMM 上の
  RAMDISK を扱います。
- **テープの WAV 相互運用**：テープイメージの相互運用形式は
  WAV です。パッチ版 xmil コア（別リポジトリ）が clean な
  PCM WAV の再生／録音に対応し、ホスト側ツール
  `tools/wavclean.py` が汚れた WAV（DC オフセット・レベル
  変動・極性反転・ノイズ）の正規化と `.cmt`⇔WAV 変換を
  担います。素の X1 が標準 WAV テープから CMT ブートする
  実演が可能です。
- **同梱プログラム**（TOSD プログラムとしてシェルから読み込み）：
  - EDAS ― エディタ／アセンブラ（`src/edas.z80`、TOSR リロケータブル版は `src/edasr.z80`）。`Z` コマンドで逆アセンブラを内蔵します（通常版のみ）。デコード部は単体版 DISASM と共有ソース（`src/disasm_core.inc`／`src/disasm_util.inc`）で、モジュール `EDIS`（`src/edis.z80`、0x9000 配置）を `Z` が読み込んで実行し、結果をテキストバッファへ直接追加します。そのまま編集して `B` で再アセンブルできます
  - NEWASM ― オンマシンアセンブラ（`src/newasm.z80`）
  - SCED ― スクリーンエディタ（`src/sced.z80`）
  - MCED ― MicroEmacs 風エディタ（`src/mced.z80`）。モードレスな Emacs 系操作（C-k キル／C-y ヤンク、リージョン、検索、C-x プレフィクス）で、SCED とは別プログラム。ファイル形式は SCED と互換
  - TBC / TBI ― TinyBASIC コンパイラ／インタープリタ（`src/tbc.z80`、`src/tbi.z80`、共有コア `src/tbcore.inc`、X1 ライブラリ `src/tblib.z80`）。X-BASIC 風方言（関数定義の集合、int/char、再帰、配列、print/input/stick/vsync など）。TBC は TOSR 実行ファイルを出力し、TBI は関数単位の遅延 JIT で即実行する。サンプルは `src/fib.tb` / `src/calc.tb` / `src/game.tb` など
  - SCC / SCI ― Small-C コンパイラ／インタープリタ（`src/scc.z80`、`src/sci.z80`、共有コア `src/sccore.inc`、X1 ライブラリ `src/clib.z80`）。Small-C サブセット（int/char、1段ポインタ、配列、再帰、構造化制御、#include/#define、printf/getchar など）。SCC は TOSR 実行ファイルを出力し、SCI は関数単位の遅延 JIT で即実行する（コンパイラと実行結果は完全一致）。TinyBASIC との双方向変換ツール `tools/bascvt.py` 付き（共通サブセット外は変換不可として報告される）。サンプルは `src/fib.c` / `src/calc.c` / `src/game.c` など、変換 corpus は `src/cvsum.tb`＋`src/cvsum.c` ほかの対照ペア
  - DISASM ― Z80 逆アセンブラ（`src/disasm.z80`、デコード部は EDIS と共有の `src/disasm_core.inc`／`src/disasm_util.inc`）。TOSD ファイルまたはメモリ範囲を、NEWASM／EDAS で再アセンブル可能なソースへ変換します。全命令（CB／ED／DD／FD プレフィックス含む）に対応し、範囲内の制御フロー飛び先には自動でラベルを生成します。コード／データの区別は DATA 範囲の指定によります。再アセンブルでバイト一致しない非正規エンコーディングは DB として出力する方針で、出力は常に対象とバイト一致します
  - RDBG ― 単独デバッガ（`src/rdbg.z80`、ビルドは `tools/mkrdbg.py`）。TOSR 形式の自己再配置プログラムで、ディレクトリのロードアドレスを変えれば任意のページ境界アドレスで動作します。メモリ表示／変更、全レジスタ表示、RST 方式のワンショットブレークポイント（元バイトの退避・復元つき）、対象プログラムのロードと実行／継続、実行中の ESC キーによる捕獲停止を備えます。逆アセンブル表示（`U` コマンド）は DISASM／EDIS と共有のデコード部を使います。モニタ埋め込みのデバッグスタブとはページ0ベクタの受け渡しで共存し、終了後はスタブもそのまま使えます
  - Tiny Scheme 2段階ブートローダー ― テープ先頭ローダー（`src/tsload.z80`、最終ロード用チャンク `src/ldfin.z80`）が Tiny Scheme（Z80 アセンブラ製コア、原始手続き 66 個、うち `(random n)`、IOCS RTGET 受け口の `(date)`／`(time)`、機械語ブリッジ `(call addr [hl])` を含む）を内蔵し、直後の固定長スクリプトブロックを検証して評価する。第2段スクリプト（`src/stage2.scm`）が Scheme 自身によるセレクタ／シェルで、スクリプトの保存（テープ全体の再生成）でブート手順が自己進化する。検証用プローブは `src/probe-rand.scm`、テープ作成／解析は `tools/mktape.py` / `tools/lstape.py`
  - SCMCORE ― TinyBASIC／SmallC から Tiny Scheme を呼ぶための組み込みコア（`src/scmcore.z80`、コア本体は単体版と共有の `src/tscore.inc`、組み込み変種の生成は `tools/mkscmcore.py`）。TOSD ファイルとしてディスクに置かれ、言語側のプログラム開始時に自動で読み込まれます。TinyBASIC では `seval("式")`／`scall("名前", 引数...)`／`scmerr()`、SmallC では `scmeval`／`scmcall`／`scmerr` で Scheme の手続きを呼び、整数の結果を受け取ります。サンプルは `src/scm.tb` / `src/scm.c` です

### 設計：機種依存部と共通部の分離

ソースはビルド時に include で組み立てる構成で、機種依存部
（`machine_*.inc`、`boot_*`、`kb_*`、`fd_*`、`tape_*`、`data_*`、
`kanji_*`、`template_*`）と共通部（`shell.inc`、`cmd_*.inc`、
`common.inc` など）がファイル単位で分離されています。本リポジトリ
には X1 用の機種依存部と共通部が入っています（共通ファイルは
他機種向けの記述を含む場合もそのまま収めています）。

## ビルド

必要なもの：Z80 クロスアセンブラ **pasmo** と python3。

```sh
./build.sh
```

`build/` にバイナリ（tapeos.bin、tapeos-shell.bin、edas.bin、
newasm.bin、sced.bin、mced.bin など）とディスク／テープイメージが生成
されます。pasmo の場所は `PASMO=/path/to/pasmo ./build.sh` で
上書きできます。

ビルド済みイメージは `images/` にも同梱してあるので、ビルド
なしでもそのまま試せます。

- `images/shell.d88` ― TapeOS シェルの起動ディスクイメージ
- `images/mced.d88` ― シェル起動ディスク＋ MCED プログラムファイル入り。起動後に `r mced` / `g 4000` で MCED が使えます
- `images/tinybasic.d88` ― シェル起動ディスク＋ TBC / TBI とサンプル（FIB.TB / CALC.TB / GAME.TB / SCM.TB）、Scheme 組み込みコア SCMCORE 入り。起動後に `r tbc` / `g 4000` でコンパイル（SRC/OUT を聞かれます）、`r tbi` / `g 4000` で即実行できます
- `images/smallc.d88` ― シェル起動ディスク＋ SCC / SCI とサンプル（FIB.C / CALC.C / GAME.C / SCM.C）、Scheme 組み込みコア SCMCORE 入り。起動後に `r scc` / `g 4000` でコンパイル（SRC/OUT を聞かれます）、`r sci` / `g 4000` で即実行できます
- `images/disasm.d88` ― シェル起動ディスク＋ DISASM プログラムファイル入り。起動後に `r disasm` / `g 4000` で逆アセンブルできます（SRC/BASE、またはメモリ範囲 FROM/TO、DATA 範囲、OUT を聞かれます）
- `images/rdbg.d88` ― シェル起動ディスク＋ RDBG プログラムファイル入り。起動後に `r rdbg` / `g 4000` でデバッガが使えます（`L ファイル名` で対象を読み、`B`／`G`／`C`／`U`／`D`／`M`／`R`、`Q` でシェルへ戻ります）
- `images/edas.d88` ― シェル起動ディスク＋ EDAS と EDIS モジュール入り。起動後に `r edas` / `g 4000` で EDAS が使え、`Z` コマンドで逆アセンブルできます（SRC にファイル名、空ならメモリ範囲 FROM/TO、続いて DATA 範囲を聞かれます）
- `images/tapeos.cmt` ― テープイメージ（IPL の CMT ブート用）
- `images/tapeos.wav` ― `tapeos.cmt` を変換した WAV 版です。実機で再生して読み込ませる場合や、WAV のまま扱いたい場合はこちらを使ってください（ビット列は `.cmt` と同一です）
- `images/tsboot.cmt` ― Tiny Scheme 2段階ブートテープ（ローダー＋スクリプトブロック＋ TapeOS ＋ TRDEMO）。IPL の CMT ブートで Tiny Scheme シェルが立ち上がり、`run 1` で TapeOS、`run 2` で TRDEMO が起動します
- `images/tsboot.wav` ― `tsboot.cmt` を変換した WAV 版です（ビット列は `.cmt` と同一です）
- `images/font.d88` ― 漢字フォント（美咲フォント由来）のディスク

## 起動方法

実行にはパッチ版 xmil コアが必要です（TapeOS のテープ再生と
EMM／RAMDISK は、素の xmil には無い機能を使うため）。

1. パッチ版 xmil を入手する：
   **https://github.com/YORU-zz/xmil-libretro**（ブランチ
   `tapeos-patch-20261122`）。ビルド手順と X1 互換 IPL ROM の
   配置方法は同リポジトリの README を参照してください。
2. libretro フロントエンド（RetroArch 等）でコアを読み、
   `images/shell.d88` をフロッピーディスクとして起動します。
   IPL が FD から TapeOS を読み込み、シェルのプロンプトが
   出れば起動成功です（`h` コマンドでヘルプが表示されます）。
3. テープ起動を試す場合は、`images/tapeos.cmt` をテープとして
   セットし、IPL メニューから CMT ブートを選んでください。
   パッチ版コアは `images/tapeos.wav` のような WAV 形式の
   テープもそのまま再生できます。

※ エミュレータ側の文字表示には、コア側のシステムフォント
（FNT0808.X1）が必要です。入手方法は xmil 側リポジトリの
README の案内（meister68k 氏の X1 互換フォント配布）に従って
ください。TapeOS 本体はこのフォントを同梱しません（漢字表示用
フォントは上記 font.d88 として TapeOS 側に含まれます）。

## ライセンスとクレジット

- 本リポジトリのコードは **MIT License** です（`LICENSE`）。
- `images/font.d88` の漢字グリフは **美咲フォント**（門真 なむ /
  Num Kadoma 氏）由来です。クレジットと条件は `NOTICE` および
  同梱の `misaki.txt` を参照してください。
- 原作小説の著作権は原作者（辺根除音 氏）に帰属します。本
  リポジトリに小説本文は含まれていません。

## 関連リポジトリ

- パッチ版 xmil（実行環境）: https://github.com/YORU-zz/xmil-libretro
