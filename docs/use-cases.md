# 使用例（ユースケース）

実際に使う場面ごとに、シェルコマンドと TinyBASIC の操作の流れをまとめたものです。コマンドの詳細は「シェルコマンド リファレンス」（docs/shell-command-reference.md）、言語の詳細は「TinyBASIC 言語仕様」（docs/lang-tinybasic.md）にあります。

## シェルの使用例

### 例1：起動してサンプルプログラムを実行する

前提：`tinybasic.d88` をフロッピーとして起動し、シェルの `>` が出ている状態。

```
f
r tbi
g 4000
```

- `f` でディスクの中身（TBI、FIB.TB、CALC.TB、GAME.TB、SCM.TB、SCMCORE など）を確認します
- `r tbi` でインタープリタを読み込み、`g 4000` で実行します
- `SRC:` と聞かれたら、実行したいファイル名（例：`FIB.TB`）を入力します

### 例2：メモリに書いた機械語をディスクへ保存し、呼び出す

```
m 8000 3e 2a c9
w TEST 8000 3
r TEST
g 8000
```

- `m` で 0x8000 からバイト列（3E 2A C9）を書きます
- `w TEST 8000 3` で、名前 TEST・ロードアドレス 0x8000 として保存します
- 次回からは `r TEST` → `g 8000` で呼び出せます

### 例3：RAMDISK を作業用に使う

```
i 40
u 1
f
（このドライブに対して r / w が効きます）
u 0
```

- `i 40` で RAMDISK を 0x40 ブロック（16KB）で初期化します（内容はフォーマットされます。EMM が必要です）
- `u 1` で対象ドライブを RAMDISK に切り替え、`u 0` で FD に戻します
- `u` だけ入力すると、現在のドライブが表示されます

### 例4：テープに複数のプログラムを入れる

```
s 8000 100 PROG1
a 9000 80 PROG2
t PROG2
```

- `s` は新規テープとして保存します（テープの内容は置き換わります）
- 続けて `a` を使うと、同じテープへ追加保存できます
- 読む時は `t PROG2` のように名前で選べます。名前を省略した `t` は先頭のプログラムを読みます
- 注意：再生や巻き戻しを挟むと追加の連続性が切れ、次の `a` は新規テープとして始まります

### 例5：メモリの中身を確かめる

```
d 8000
e 8000
```

- `d` は128バイトのダンプ（アドレス省略で続きを表示）
- `e` は1バイトずつの対話式確認・変更です（空行＝次へ、`.`＝終了）

### 例6：プログラムを常駐イメージとして保持する

```
n i 20
n s WORK 8000 100
n
n r WORK
n f WORK
```

- `n i` でイメージ用プールを初期化し、`n s` でメモリ範囲を名前付きで保持します
- `n r` で記録アドレスへ戻して実行（RET でシェルへ帰ります）、`n f` で解放します

## TinyBASIC の使用例

### 例7：入力を受け取るプログラム

```
func int Main()
    int n, i, s
    print("N?")
    n = input()
    s = 0
    for i = 1 to n
        s = s + i
    next
    print("SUM=")
    print(s)
    print()
    return 0
endfunc
```

エディタ（MCED）で例えば `SUM.TB` として保存し、`r tbi` → `g 4000` → `SRC:` に `SUM.TB` と入力して実行します。

### 例8：キー入力でキャラクタを動かす（GAME.TB の型）

```
func int Main()
    int px, py, f, s
    px = 20
    py = 12
    cls()
    for f = 0 to 179
        vsync()
        s = stick()
        if s == 3 then
            px = px - 1
        endif
        if s == 4 then
            px = px + 1
        endif
        locate(px, py)
        putc(64)
    next
    return 0
endfunc
```

- `vsync()` で1フレーム待ち、`stick()` で方向キー（8/2/4/6、I/K/J/L、W/S/A/D）を読みます
- 同梱サンプルの GAME.TB は、この型で `@` を180フレーム動かす完全な例です

### 例9：Scheme を呼び出す

```
func int Main()
    int r
    seval("(define (sq x) (* x x))")
    r = scall("sq", 12)
    print(r)
    print()
    return 0
endfunc
```

- ディスクに `SCMCORE` がある場合に使えます（tinybasic.d88 には入っています）
- 結果は 144 と表示されます。エラーの有無は `scmerr()` で確認できます
- 完全な例は同梱サンプルの SCM.TB です
