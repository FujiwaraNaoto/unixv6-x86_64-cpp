> **この文書は2つの設計を含みます。**
> - 【A】**syscall/sysret 版**（以下、行1〜120）: ユーザープロセスが `fork()` を syscall で呼ぶ前提の設計。**将来ユーザープロセス fork にする時の設計**として残す。
> - 【B】**カーネルスレッド版**（末尾の新セクション）: 今のデモは `parent_thread` がカーネルスレッド（リング0）で、syscall を一度も撃たないため【A】は不整合（`sysret` する syscall フレームが無い）。**現在実装するのはこちら**。
>
> 経緯は [fork_debug.md](fork_debug.md) を参照（バグ②で【A】が破綻 → 【B】へ方針転換）。

---

# 【A】syscall/sysret 版（ユーザープロセス fork・将来用）

親の場合(普通のsyscall)

```
ユーザー: fork() → syscall 命令
  ↓
syscall_entry
  ├── push rcx, r11 (戻りRIP, RFLAGS)
  ├── push rdi, rsi, rdx, r10, r8, r9
  ├── レジスタ並べ替え
  ├── call syscall_dispatch → sys_fork() → 子のpidを返す
  ├── pop で復元
  ├── pop r11, pop rcx
  └── o64 sysret → ユーザー空間へ (RAX = 子のpid)
```


子の場合（問題はここ）

子は「まだ一度も動いていない」状態で生まれます。swtch で初めてスケジュールされたとき、子はどこに着地すればいいでしょうか。
答えは「親と同じ syscall_entry の復帰部分」です。子も親と同じユーザー空間の地点（fork の直後）に戻る必要があるからです

fork()では親のカーネルスタックを子供に丸ごとコピーする
```C++
for (size_t i = 0; i < KSTACK_SIZE; i++)
    cstack[i] = reinterpret_cast<uint8_t *>(parent->kstack)[i];
```
この時点で、親のカーネルスタックには syscall_entry が積んだデータが乗っています。
```
親のカーネルスタック (fork中の状態)

高位アドレス
  ┌──────────────┐
  │ (syscall前の状態) │
  ├──────────────┤
  │ RCX (戻りRIP)  │ ← push rcx
  │ R11 (RFLAGS)   │ ← push r11
  │ RDI            │ ← push rdi
  │ RSI            │
  │ RDX            │
  │ R10            │
  │ R8             │
  │ R9             │ ← push r9
  ├──────────────┤
  │ (dispatch呼び出し中のフレーム) │
  └──────────────┘
低位アドレス
```
子のスタックはこれの完全なコピーなので、同じ位置に同じ値（同じ戻りRIP、同じRFLAGS、同じ引数）が入っています。
子供が着地する場所は,子が swtch でスケジュールされたら、上記のスタックを使って syscall_entry の復帰部分だけを実行すればユーザーに戻れます。
親子の違いはRax(戻り値)を0にすること


全体の流れ
```
親: fork() 呼ぶ
  ├── syscall_entry に入る
  ├── スタックに RCX/R11/引数を push
  ├── syscall_dispatch → sys_fork()
  │     ├── 子のProcess確保
  │     ├── 子のカーネルスタック = 親のコピー (pushしたデータごと)
  │     ├── 子の context->rip = fork_child_return
  │     └── return 子のpid
  ├── RAX = 子のpid
  ├── syscall_return_path で pop 復元
  └── sysret → ユーザーへ (RAX = 子のpid)

子: (後でスケジュールされる)
  ├── swtch の ret で fork_child_return に着地
  ├── xor rax, rax        ← 戻り値を0に
  ├── jmp syscall_return_path
  ├── pop で復元 (親からコピーしたスタックの値を使う)
  └── sysret → ユーザーへ (RAX = 0)
```



# 子供のContextとスタックポインタの整合


もう1つ重要な点があります。swtch は子の context が指すアドレスをRSPにセットして復帰します。

```asm
mov rsp, rsi     ; 次プロセスの rsp に切り替え
pop rbp
pop rbx
pop r12
pop r13
pop r14
pop r15
ret              ; context->rip へジャンプ
```
つまり子の context は、swtch が pop する6つのレジスタ + rip を持つ位置になければなりません。そしてその ret の後（= fork_child_return 実行時）に、RSPが親からコピーしたsyscallフレームの先頭を指している必要があります。

これを実現するには、子のカーネルスタック上でContextを適切な位置に配置します。


```
子のカーネルスタック

高位
  ┌──────────────┐
  │ RCX (戻りRIP)  │ ← 親からコピー
  │ R11            │
  │ RDI            │
  │ RSI            │
  │ RDX            │
  │ R10            │
  │ R8             │
  │ R9             │ ← syscall_return_path の pop はここから始まる
  ├──────────────┤ ← swtch の ret 後のRSPがここを指すべき
  │ rip = fork_child_return │ ← Context.rip
  │ rbp            │
  │ rbx            │ ← Context (swtch が pop する)
  │ r12            │
  │ r13            │
  │ r14            │
  │ r15            │ ← context ポインタはここ
  └──────────────┘
低位
```

---

# 【B】カーネルスレッド版 fork（現在の実装方針）

## 背景: なぜ【A】ではダメか

今のデモの `parent_thread`（[kernel/main.cpp](kernel/main.cpp)）は **カーネルスレッド（リング0）** で、syscall を一度も撃っていない。
そのためカーネルスタックに **syscall フレーム（`rcx=戻りRIP, r11=RFLAGS, 引数群`）が存在しない**。
【A】の `fork_child_return` は無条件に `syscall_return_path` へ飛んで `pop rcx / pop r11 → sysret` するので、
**ゴミを pop して sysret でゴミへ飛ぶ**（#DB、TF付き）。詳細は [fork_debug.md](fork_debug.md) のバグ②。

さらに旧コードには2つ目のバグがあった:

```cpp
child->context = parent->context + offset;  // ← parent->context が古い
```

`parent->context` は「最後に親が switch_context で**切り替えられた**時」の値。親は起動以来 yield していないので、
`create_process` が入れた初期値（スタック頂上付近・rip=trampoline）のまま。しかもその領域は
trampoline 実行でスタックが伸びて上書き済み。→ **復帰情報が全部ゴミ**。

## 方針

子は **`sysret` を使わず、リング0のまま `fork()` の途中から復帰**し、`current_proc_` で親子を判定して
`0` を返す。あとは **fork の通常エピローグ**が、親からコピーしたスタック上で `parent_thread` へ `ret` する。
これは xv6 の `forkret` をカーネルスレッド用に簡略化したもの。

## 土台: switch_context の復帰フォーマット

[boot/switch.asm](boot/switch.asm) の復帰側:

```asm
switch_context(→ ctx):
    mov rsp, ctx
    pop rbp   ; [ctx+0]
    pop rbx   ; [ctx+8]
    pop r12   ; [ctx+16]
    pop r13   ; [ctx+24]
    pop r14   ; [ctx+32]
    pop r15   ; [ctx+40]
    ret       ; [ctx+48] を rip として実行 → 実行後 rsp = ctx+56
```

子に用意すべき `context` ブロックは、**低位→高位で `rbp, rbx, r12, r13, r14, r15, rip` の7ワード**。
（注: `ProcessContext` 構造体のフィールド名は逆順 `r15…rbp` だが、`create_process` は全部0で埋めるので
今まで顕在化していないだけ。今回は名前に頼らず**生オフセット**で扱う。）

## 中核: `fork_capture`（setjmp 風ヘルパ）

`-O2` ではフレームポインタが省略され `__builtin_frame_address` 系は使えない。
そこで **switch_context と同じ並びで「その場の callee-saved + 戻りアドレス」を保存する** asm ヘルパを使う。
**最適化レベルにもフレームポインタにも依存しない**のが利点。

```asm
; extern "C" uint64_t fork_capture(ProcessContext* out);
;   rdi = out。呼出時点の callee-saved と「呼び出し元へ戻るアドレス(= fork内の復帰点)」を
;   switch_context 形式で out に保存し、呼び出し元(fork)の rsp を返す。
fork_capture:
    mov  rax, [rsp]      ; 戻りアドレス = 復帰点
    mov  [rdi+0],  rbp
    mov  [rdi+8],  rbx
    mov  [rdi+16], r12
    mov  [rdi+24], r13
    mov  [rdi+32], r14
    mov  [rdi+40], r15
    mov  [rdi+48], rax   ; rip = 復帰点
    lea  rax, [rsp+8]    ; 戻り値 = 呼び出し元の rsp
    ret
```

## スタック配置（親 ↔ 子コピーの対応）

`offset = child_stack - parent->kernel_stack` で全アドレスが平行移動する。

```
        親カーネルスタック                         子カーネルスタック(コピー)
  高位 ┌────────────────────────┐            ┌────────────────────────┐
       │ parent_thread のフレーム │            │ (同じ内容)              │
       │  [RET → parent_thread]  │ ←call fork │  [RET → parent_thread]  │
       │  fork のフレーム         │            │  (同じ内容)              │
caller_rsp →─────────────────────┤            ├─ caller_rsp+offset ─────┤ ← 復帰後の子rsp
       │ (未使用・レッドゾーン)   │            │ ┌ ここに context を書く ┐│
  低位 └────────────────────────┘            │ │[rbp,rbx,r12-r15,rip]  ││ ← child->context
                                             │ └───────────────────────┘│  = caller_rsp+offset-56
                                             └────────────────────────┘
```

- `child->context = caller_rsp + offset - 56` に置き、そこへ `fork_capture` が捕捉した
  **親の現在の callee-saved + rip(=復帰点)** を書き込む。
- switch_context がこの7ワードを pop → 6本復元 + ret で復帰点へ。復帰後
  `rsp = child->context+56 = caller_rsp+offset` ＝ **親の fork の rsp をミラーした位置**。
- 書き込み先は**子スタックの未使用域**（誰も走っていない＝割り込みでも壊れない）。
  親のレッドゾーンを汚さないので競合しない。

## 復帰の流れ（時系列）

```
【親】fork():
  1. child 確保, child_stack 確保, 親スタックを child_stack へ丸ごとコピー
  2. caller_rsp = fork_capture(&snap)     ; snap = rbp/rbx/r12-r15/rip(=下の★)
  3. ★復帰点: if (current_proc_ == child) return 0;   ← 親は current_proc_==parent で素通り
  4. child->context = (caller_rsp - 56 + offset); *child->context = snap
  5. child->state = Ready; return child->pid;   ← 親は pid を返し parent_thread へ

  （その後スケジューラが子を選ぶ）

【子】switch_context(→ child->context):
  pop rbp..r15 (=親の値に復元) → ret → ★復帰点へ, rsp = caller_rsp+offset
  ★: current_proc_ == child なので  return 0;
  → fork のエピローグ(コピー済みフレームから callee-saved 復元, ret)
  → parent_thread の "int pid = fork();" の直後へ, rax=0
  → pid==0 分岐 → exit(42)
```

## なぜ正しく動くか

- **親子判定**は戻り値トリック不要で `current_proc_ == child`（グローバル）。`fork_capture` は不透明な
  extern 呼び出しなので、コンパイラは前後でグローバルを再ロードする＝最適化で消えない。
- 復帰後の ABI 状態（callee-saved / rsp / スタック内容）が「`fork_capture` 呼出直後」と完全一致するので、
  コンパイラが期待する状態と齟齬がない（setjmp が成立する条件そのもの）。
- `sysret` を使わないので、syscall フレームの有無に依存しない。

## 変更点

1. [syscall/fork_ret.asm](syscall/fork_ret.asm): `fork_capture` を追加。旧 `fork_child_return`(sysret版)は
   【A】用として残す（現在は未使用）。
2. [include/process.cpp](include/process.cpp) の `fork()`: コピー後の context 設定〜return を上記ロジックへ置換。
   `extern "C" uint64_t fork_capture(ProcessContext*);` を宣言。
3. Makefile 変更なし（`fork_ret.asm` は既に `ASM_SRC` に含む）。

---

# 用語補足: 「カーネルスレッド（リング0）で、syscall を一度も撃っていない」とは

【B】の背景を理解する土台。ここが【A】破綻の核心。

## リング0 / リング3

x86 の特権レベル。CPUが「今どれだけ強い権限で動いているか」。

| リング | 通称 | 何ができるか |
|---|---|---|
| **リング0** | カーネルモード | 全命令OK。`cli`/`hlt`、I/Oポート、ページテーブル操作、任意メモリ |
| **リング3** | ユーザーモード | 特権命令は禁止、割り当てられたメモリしか触れない |

`parent_thread`（[kernel/main.cpp](kernel/main.cpp)）は `create_process` で作られ、スケジューラから
`switch_context` で直接呼ばれる。**リング0のカーネルコードのまま実行**され、一度もリング3に降りない
（CS=0x08 のまま）。

## カーネルスレッド

**リング0でずっと動き続ける実行の流れ**。ユーザープロセスとの対比:

```
ユーザープロセス:  リング3で動く → 必要な時だけ syscall でリング0へ降りてカーネルに頼む
カーネルスレッド:  最初から最後までリング0。ユーザー空間を持たない
```

`parent_thread` / `thread_A` / `sleeper_thread` は全部カーネルスレッド。リング3で動く
`user_program` とは別物。

## 「syscall を撃つ」

`syscall` は **CPU命令**そのもの。リング3のユーザーコードが「カーネルに仕事を頼む」時に実行する
（[kernel/main.cpp](kernel/main.cpp) の `user_program` 内 `syscall`）。実行されると CPU が自動で:

- 戻り先RIPを `rcx` に、RFLAGSを `r11` に退避
- リング0へ昇格し `syscall_entry`（[syscall/syscall_entry.asm](syscall/syscall_entry.asm)）へジャンプ
- `syscall_entry` が `rcx`/`r11`/引数をカーネルスタックに push（＝ **syscall フレーム**）

仕事が終わると `sysret` で `rcx`→RIP・`r11`→RFLAGS を使いリング3へ戻る。

## なぜ問題か（【A】破綻の理由）

【A】の子の復帰は **syscall フレームがスタックに積まれている前提**:

```asm
fork_child_return:
    xor rax, rax
    jmp syscall_return_path   ; → pop r11 / pop rcx → sysret
```

```
ユーザープロセスの fork:
  syscall 撃った → スタックに [rcx=戻りRIP, r11=RFLAGS, 引数...] がある
                 → pop して sysret すれば正しくユーザーへ戻れる ✓

parent_thread(カーネルスレッド)の fork:
  syscall 撃ってない → そんなフレームは無い(ただのカーネル関数の呼び出し履歴)
                     → pop rcx/r11 が無関係なゴミを拾う
                     → sysret でゴミのアドレスへ飛ぶ → #DB(バグ②) ✗
```

これが `RIP: 0x05000000e93b0605` のゴミへ sysret して落ちた正体
（[fork_debug.md](fork_debug.md) バグ②）。

**結論**: カーネルスレッドはそもそもユーザー空間に戻る必要がない。リング0のまま `fork()` から
普通に return すればよい ＝ 【B】の `fork_capture` 方式。
