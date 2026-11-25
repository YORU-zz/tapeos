# 契約ベースライン変更履歴

`contract-check.sh --generate` による `contract-manifest.txt` の
再生成のうち、変更分類（interface-spec §7.1）を伴ったものの記録。
分類 B／C の再生成はここへの追記が必須であり、`--generate` が
自動で追記する。分類 A（追加のみ）の再生成は記録なしで通る。

| 日時 | 分類 | 承認者 | 変更行数 | メモ |
|---|---|---|---|---|
| 2026-11-26 05:50 JST | B | muse | 1 | SCED/MCED共通核の切り出し（edcore.inc化、ロードマップ73）: SCEDのbuf_resetバグ（b_flcnt未リセット）をMCED側の堅牢版で正本化し、テキストバッファ管理＋表示層を src/edcore.inc へ共通化。SCEDバイナリが+5バイト変化したため build/sced.bin のHASHが変わる。MCEDバイナリはバイト同一でHASH不変。正本（ourobo-ros）側の契約再生成と対。 |
