# TapeOS（Sharp X1 版）

> **暫定版 2026-11-23** ― これは開発中スナップショットの先行公開です。
> 内容は予告なく変更されることがあります。また本リポジトリは暫定版の
> 配布場所で、再現プロジェクト完了後の本公開の際には、正本リポジトリへ
> 統合／移行される予定です。

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
- **同梱プログラム**（TOSD プログラムとしてシェルから読み込み）：
  - EDAS ― エディタ／アセンブラ（`src/edas.z80`、TOSR リロケータブル版は `src/edasr.z80`）
  - NEWASM ― オンマシンアセンブラ（`src/newasm.z80`）
  - SCED ― スクリーンエディタ（`src/sced.z80`）
  - MCED ― MicroEmacs 風エディタ（`src/mced.z80`）。モードレスな Emacs 系操作（C-k キル／C-y ヤンク、リージョン、検索、C-x プレフィクス）で、SCED とは別プログラム。ファイル形式は SCED と互換

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
- `images/tapeos.cmt` ― テープイメージ（IPL の CMT ブート用）
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
