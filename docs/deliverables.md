# 成果物年表

この文書は、公開リポジトリ（tapeos および xmil-libretro）の履歴に基づく成果物の一覧です。新しい順に並べています。2026-11-22 の先行公開以降を対象とし、プロジェクト自体の作業はそれ以前から始まっています。[xmil] と付いた項目は xmil-libretro 側の成果物です。

## 2026-11-25

- CI 堅牢化：Pasmo 0.5.3-7 へのバージョン固定、ROM 台帳検査への拡張子ベース走査の併用
- 変更分類 A/B/C と承認ゲートの導入（仕様 §7.1、ベースライン再生成の機械的ゲート、変更履歴）
- CI ワークフロー（ビルド＋契約チェック＋台帳チェック＋スモーク）
- 監査ドキュメント（docs/audit.md）とスモークチェック
- ROM 出自台帳（許可リスト方式）と検査スクリプト
- 契約チェック（公開範囲の抜粋）の同梱
- インターフェース仕様の公開写し（docs/interface-spec.md）
- 技術解説シリーズ（docs/tech/、全13文書。C 処理系の系譜とプリンタの扱いについての追記を含む）
- 言語仕様（TinyBASIC、SmallC、Tiny Scheme）
- 使用例（docs/use-cases.md）
- シェルコマンドの詳細リファレンス
- 起動と IPL-ROM の動き、シェルの最低限コマンドの文書
- TSDEMO 参照名の修正と tsboot の現行シェルでの再生成

## 2026-11-24

- デバッガ改良（第56話）：二層リモートデバッガとシェル世代交代
- 単独デバッガを SDBG から RDBG（Relocatable Debugger）へ改名
- 単独デバッガ SDBG（TOSR 自己再配置＋逆アセンブル表示つき）
- README に実機未検証の注記を追加
- テープイメージの変換済み WAV 版（tapeos.wav／tsboot.wav）
- TinyBASIC／SmallC から Tiny Scheme を呼ぶ Scheme 呼び出し
- TinyBASIC 潜在バグ2件の修正
- EDAS への逆アセンブラ組み込み（Z コマンド＋EDIS モジュール）
- DISASM（Z80 逆アセンブラ単体版）
- [xmil] RS-232C ボード（i8251）の追加

## 2026-11-23

- SmallC（SCC コンパイラ／SCI インタープリタ）と TinyBASIC 双方向変換
- wavclean（WAV 正規化／.cmt⇔WAV 変換ツール）
- Tiny Scheme 2段階ブートローダー（TSLOAD）
- TinyBASIC（TBC コンパイラ／TBI インタープリタ）
- [xmil] CMT の WAV 対応（再生／録音、固定しきい値）

## 2026-11-22

- MCED（MicroEmacs 風エディタ）
- TapeOS（Sharp X1）暫定版の先行公開スナップショット
- [xmil] 暫定版パッケージ（CC0 互換 IPL ROM の同梱、README、NOTICE、VERSION）
- [xmil] TapeOS パッチセット（CMT プレイヤー、EMM 1MB／RAMDISK、キーボード修正）
