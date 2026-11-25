# ドキュメント目次

TapeOS の公開文書の一覧です。目的別に並べています。

## まず読む

- [TapeOS について](about-tapeos.md) ― 何を再現し、どういう考え方で
  作り、どこへ向かっているか。全体像はこちらから。
- [README](../README.md) ― リポジトリの入口。起動方法とビルド済み
  イメージの案内もこちらにあります。

## 使う

- [起動と IPL-ROM の動き](boot-and-ipl.md) ― 起動した時に画面の
  向こうで何が起きているか。
- [シェルの最低限コマンド](shell-commands.md) ― 最初に必要になる
  コマンドだけ。
- [シェルコマンド リファレンス](shell-command-reference.md) ―
  全コマンドの詳細。
- [使用例（ユースケース）](use-cases.md) ― 場面ごとの操作の流れ。

## 言語仕様

- [TinyBASIC 言語仕様（TBI／TBC）](lang-tinybasic.md)
- [SmallC 言語仕様（SCC／SCI）](lang-smallc.md)
- [Tiny Scheme 言語仕様](lang-tiny-scheme.md)

## 技術解説

- [技術解説シリーズ索引](tech/tech-00-index.md) ― モジュール別の
  設計と実装、原作との技術的差異を根拠付きで解説するシリーズの
  入口です。

## 仕様と記録

- [インターフェース仕様](interface-spec.md) ― 上層プログラムが
  依存してよい契約の正本。
- [監査（全スイート回帰）の方法と結果](audit.md)
- [ROM・フォント等の出自とライセンス](rom-provenance.md)
- [成果物年表](deliverables.md) ― 公開リポジトリの履歴に基づく
  成果物の一覧（新しい順）。
