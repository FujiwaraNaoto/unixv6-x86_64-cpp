# unixv6-x86_64-cpp

Unix V6 (と xv6) を下敷きに、**x86-64 + C++20** で書き直している自作 OS です。
GRUB (Multiboot2) から起動し、QEMU 上で動かしています。

製作途中で、2026-10-24 時点では **phase8: ファイルシステムの途中** です。

## 特徴

- 64bit ロングモード (x86-64) 前提。ブート直後の 32bit 部分だけ NASM、それ以外は C++
- C++20 のフリースタンディング環境 (`-ffreestanding -nostdlib -nostdinc`、例外・RTTI なし)
  - 標準ライブラリが使えないので `std::string` の代わりに固定長の `kstring`、
    `std::function` の代わりに関数ポインタ、といった置き換えをしている
- ハードウェアは QEMU の標準的な構成 (PIC / PIT / PS/2 キーボード / VGA テキスト / シリアル / PCI + virtio-blk)
- 動作確認はカーネル内のテストハーネスで行い、GitHub Actions の CI でビルド・QEMU 実行・clang-format を毎回チェックする

## 遊び方

```sh
make run-gui      # 別ウィンドウ(GTK)で起動。シリアル出力は serial.log へ
```

```sh
make run-vscode   # ターミナル直結(-nographic)で起動。終了は Ctrl-A X
```

必要なもの: `g++-13` (Makefile でバージョン固定)、`nasm`、`ld`、`grub-mkrescue` (+`xorriso` / `mtools`)、`qemu-system-x86_64`。
ディスクイメージ `fs.img` は `make fs.img` で作る (未フォーマットのまま起動すると、カーネルが自分でフォーマットする)。

### 機能テスト

`kernel_main` は初期化して `hlt` ループに入るだけで、各機能の動作確認コードは
`tests/` 以下にまとめてある。テストは `make tests` (= `TESTS=1`) のときだけ
`-DENABLE_TESTS` 付きでコンパイル・リンクされ、通常ビルドのカーネルには一切含まれない。

```sh
make tests        # テスト入りカーネルをビルドしてターミナル直結で起動
make tests-gui    # 同上を別ウィンドウ(GTK)で
make tests-gdb    # 同上を gdb 待ち受けで
make tests-build  # 起動せずビルドだけ
```

走らせるテストは [`tests/tests.cpp`](tests/tests.cpp) の `kTests` 表に並べる。
`run_all()` が順に実行して `[TEST] <名前>: OK / FAILED` と集計行をシリアル/VGA に出し、
最後に `isa-debug-exit` へ書き込んで QEMU を終了させる (成功なら終了コード 33、失敗なら 35)。
このデバイスを付けずに起動した場合は何も起きず、そのまま `hlt` ループへ進む。
リング3遷移 (`usermode_ring3`) やリング3からの `fork` (`fork_from_ring3`) も、
リング3から `exit` してテストに戻ってくるので `kTests` に入っている。

キーボードのエコー (`keyboard_echo`) のように「呼ぶと戻ってこない」テストは `kTests` に入れられないので、
`ONLY=<テスト名>` でそのテストだけを入れたカーネルを作って起動する
(`tests.cpp` を書き換えなくて済む)。

```sh
make ONLY=keyboard_echo tests   # そのテストだけ入った ISO を起動
make ONLY=keyboard_echo iso     # ISO を作るだけ
```

生成物は構成ごとに分かれているので、通常ビルドとテストビルドを行き来しても
`make clean` は要らない。

| | オブジェクト | ISO |
|---|---|---|
| `make` | `build/kernel/` | `unixv6.iso` |
| `make tests` | `build/tests/` | `unixv6-tests.iso` |
| `make ONLY=<名前> tests` | `build/tests-<名前>/` | `unixv6-tests-<名前>.iso` |

### CI

[`.github/workflows/ci.yml`](.github/workflows/ci.yml) で、push / PR ごとに次の 3 つを走らせる。

- **build** — `make iso` と `make TESTS=1 iso` を両方ビルドし、ログに error や
  `-Wreturn-type` / `-Wuninitialized` が出ていたら落とす
- **qemu** — テスト入り ISO を QEMU でヘッドレス起動し、終了コードで合否を見る
  (33 = 成功、35 = テスト失敗、124 = タイムアウト、それ以外 = トリプルフォルト等でハーネスまで届いていない)
- **format** — clang-format 14 で整形済みかを確認する (直すときは `make format`)

### デバッグ

QEMU を gdb 待ち受け (`-s -S`) で起動して、別の端末から繋ぐ。

```sh
make run-gdb
```

```sh
gdb build/kernel/kernel.elf
(gdb) target remote :1234
(gdb) break kernel_main
(gdb) continue
(gdb) next        # 1行ずつ進める
(gdb) stepi       # 命令単位で進める (CR3 の書き換えなどを追うとき)
(gdb) info registers cr3
```

## ディレクトリ

| パス | 中身 |
| --- | --- |
| [boot/](boot/) | Multiboot2 ヘッダ、32bit→64bit の移行、コンテキストスイッチ (`switch.asm`) |
| [kernel/](kernel/) | `kernel_main`。各サブシステムの初期化だけを行う |
| [include/](include/) | カーネル本体 (PMM / VMM / heap / GDT / IDT / PIC / process / syscall / PCI / virtio-blk / バッファキャッシュ / ファイルシステム …) |
| [interrupt/](interrupt/) | ISR スタブとレジスタ退避のアセンブリ |
| [io/](io/) | ポート I/O のアセンブリ |
| [syscall/](syscall/) | `syscall` 命令のエントリ (TrapFrame の積み下ろし)、`fork` の子側の戻り口 |
| [user/](user/) | リング3 に降りるためのエントリ |
| [tests/](tests/) | テストハーネスと各機能のテスト (`make tests` のときだけリンクされる) |

## 実装フェーズ

| | フェーズ | 状態 |
| --- | --- | --- |
| phase1 | boot | ✅ |
| phase2 | IDT / 割り込みハンドラ | ✅ |
| phase3 | 物理メモリアロケータ | ✅ |
| phase4 | 仮想メモリ | ✅ |
| phase5 | プロセス管理 | ✅ |
| phase6 | システムコール | ✅ |
| phase7 | ユーザランド | ✅ |
| phase8 | ファイルシステム | 🚧 **途中** |
| phase9 | シェル | ⬜ 未着手 |

### phase1: boot

GRUB が Multiboot2 カーネルとして認識するヘッダを置き、保護モード (32bit) で起動したところから
ページテーブルを組んでロングモードに入り、64bit の `kernel_main` へジャンプする。
Multiboot2 のメモリマップタグを拾って PMM に渡すところまでがここ。
画面出力は VGA テキストモード (1セル2バイト、上位が属性・下位が ASCII) とシリアルの2本。

### phase2: IDT / 割り込みハンドラ

IDT を構築し、CPU 例外 (0-31) と PIC 経由の IRQ を受ける。PIC のリマップ (IRQ0-15 → 0x20-0x2F)、
PIT を 100Hz で叩いてタイマ割り込み、IRQ1 で PS/2 キーボード。
キーボードはスキャンコードを変換してリングバッファに積む形。

### phase3: 物理メモリアロケータ

Multiboot2 のメモリマップから使える領域を求め、4KB ページ単位のビットマップで管理する
`PhysicalMemoryManager` (`allocate()` / `free()`)。カーネル末尾 (`kernel_end`) より後ろを空きとして扱う。

### phase4: 仮想メモリ

4段ページテーブル (PML4 → PDPT → PD → PT) を自前で構築する `VirtualMemoryManager`。
`map_page` / `unmap_page` / `virtual_to_physical` に加えて、プロセス用に
`create_address_space()` (カーネル領域は共有、ユーザー領域は分離) と `map_page_in()`、CR3 の切り替えを持つ。
その上に `sbrk` / `morecore` でページを足しながら伸びる連結リスト方式のカーネルヒープを載せている。

### phase5: プロセス管理

`Process` テーブル (最大64) と状態 (`Unused` / `Embryo` / `Runnable` / `Running` / `Sleeping` / `Zombie`)、
16KB のカーネルスタック、`switch.asm` でのコンテキストスイッチ。
スケジューラはラウンドロビンの**協調型**で、`yield()` を呼んだときにだけ切り替わる
(タイマ割り込みからは `yield()` しない)。
プロセスごとに PML4 を持つので、同じ仮想アドレスがプロセスごとに別の物理ページを指すことを確認済み。
`sleep(channel)` / `wakeup(channel)` も xv6 と同じ形で入っている。

### phase6: システムコール

`syscall` / `sysret` 命令を MSR (EFER.SCE / STAR / LSTAR / FMASK) で有効化し、
Linux x86-64 と同じ番号で `read`(0) / `write`(1) / `fork`(57) / `exit`(60) / `wait`(61) を実装。
syscall の入口でカーネルスタックに切り替え、レジスタ一式を `TrapFrame` として積む。
`read` はキーボード入力を待つあいだ `sti; hlt` で寝る (lost-wakeup を避けるため sti の直後に hlt を置く)。

### phase7: ユーザランド

GDT にユーザーセグメントと TSS を足し、`TSS.RSP0` を設定して `iretq` でリング3 へ降りる。
リング3 のプログラムから `syscall` を撃って文字が出るところまで動く (テストの `usermode_ring3`)。
`fork` / `exit` / `wait` も実装済みで、リング3 のプログラムから `fork` を呼べる。
子は親の `TrapFrame` の複製を持ち、`fork_ret.asm` から syscall の出口へ着地して 0 を返す。
ユーザーページはコピーするので親子でアドレス空間が分かれ、`exit` した子 (`Zombie`) は親の `wait()` が
回収してカーネルスタック・ページテーブルまで解放する。
確認はテストの `fork_wait` (カーネルスレッド版) / `fork_from_ring3` / `process_free` で行っている。

### phase8: ファイルシステム 🚧 ← 2026-10-24 時点はここ

**書きかけです。** 下から順に積んでいて、`main` には「ブロックが読み書きできてキャッシュに乗り、
スーパーブロックとブロックビットマップでブロックを確保・解放できる」ところまで入っている。

`main` に入っているもの:

- **higher-half kernel への移行** — ファイルシステムの前提として、カーネルを
  `0xFFFFFFFF80000000` に置き、全物理メモリの direct map (PML4[256]) を張る形に作り替えた
- **PCI コンフィグ空間の列挙** — ベンダ/デバイス ID でデバイスを探し、BAR を読み、bus master を有効化する
- **virtio-blk ドライバ** — virtqueue (descriptor / avail / used) を組んでセクタ単位の read/write。
  完了待ちはポーリング
- **バッファキャッシュ** — 512B × 30 ブロックの LRU キャッシュ。`read` / `mark_dirty` / `write` / `flush`。
  協調型スケジューラなのでロックは持たない (プリエンプティブにするなら xv6 同様バッファ単位の sleeplock が要る)
- **ディスクレイアウトとブロック確保** — boot / super / inode / bitmap / data の xv6 風レイアウト
  ([`include/file_system.hpp`](include/file_system.hpp) に図あり)。起動時にスーパーブロックを読み、
  未フォーマットなら自分でフォーマットする。`allocate_block()` / `free_block()` はビットマップで管理。
  ファイルシステムはブロックの読み書きを `IBlockStore` 越しに行うので、テストでは偽物に差し替えられる

### phase9: シェル

未着手。`exec` を作って、キーボード入力からコマンドを起動する最小の `sh` を動かすのが目標。

## 制約 / TODO (2026-10-24 時点)

- スケジューラが協調型。タイマ割り込みからのプリエンプションは入れていない
- `exec` がないので、ユーザープログラムはカーネルに埋め込んで User ページとしてマップしている
  (テスト用のリング3コードは `tests/` にあり、通常ビルドにはユーザープログラムが入っていない)
- ファイルディスクリプタ層がなく、`read`/`write` はキーボードと VGA に直結
- ロックが一切ない (シングル CPU + 協調型スケジューラという前提の上に乗っている)
- init プロセスがおらず、親が先に死んだ子を引き取る仕組み (reparent) がない
- ユーザースタックの位置が固定
- inode 層が `main` にマージされていない
