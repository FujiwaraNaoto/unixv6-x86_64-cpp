# unixv6-x86_64-cpp

Unix V6 (と xv6) を下敷きに、**x86-64 + C++20** で書き直している自作 OS です。
GRUB (Multiboot2) から起動し、QEMU 上で動かしています。

まだ製作途中で、いまは **phase8: ファイルシステムの途中** にいます。

## 特徴

- 64bit ロングモード (x86-64) 前提。ブート直後の 32bit 部分だけ NASM、それ以外は C++
- C++20 のフリースタンディング環境 (`-ffreestanding -nostdlib -nostdinc`、例外・RTTI なし)
  - 標準ライブラリが使えないので `std::string` の代わりに固定長の `kstring`、
    `std::function` の代わりに関数ポインタ、といった置き換えをしている
- ハードウェアは QEMU の標準的な構成 (PIC / PIT / PS/2 キーボード / VGA テキスト / シリアル / PCI + virtio-blk)

## 遊び方

```sh
make run-gui      # 別ウィンドウ(GTK)で起動。シリアル出力は serial.log へ
```

```sh
make run-vscode   # ターミナル直結(-nographic)で起動。終了は Ctrl-A X
```

必要なもの: `g++` (C++20)、`nasm`、`ld`、`grub-mkrescue` (+`xorriso`)、`qemu-system-x86_64`。

### デバッグ

QEMU を gdb 待ち受け (`-s -S`) で起動して、別の端末から繋ぐ。

```sh
make run-gdb
```

```sh
gdb build/kernel.elf
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
| [kernel/](kernel/) | `kernel_main`。各サブシステムの初期化と動作確認コードが並ぶ |
| [include/](include/) | カーネル本体 (PMM / VMM / heap / GDT / IDT / PIC / process / syscall / driver …) |
| [interrupt/](interrupt/) | ISR スタブとレジスタ退避のアセンブリ |
| [syscall/](syscall/) | `syscall` 命令のエントリ、`fork` の子側の戻り口 |
| [user/](user/) | リング3 に降りるためのエントリ |

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
Linux x86-64 と同じ番号で `read`(0) / `write`(1) / `exit`(60) を実装。
`read` はキーボード入力を待つあいだ `sti; hlt` で寝る (lost-wakeup を避けるため sti の直後に hlt を置く)。

### phase7: ユーザランド

GDT にユーザーセグメントと TSS を足し、`TSS.RSP0` を設定して `iretq` でリング3 へ降りる。
リング3 のプログラムから `syscall` を撃って文字が出るところまで動く。
`fork` / `exit` / `wait` も実装済みで、親子で戻り値を変える (子は `fork_ret.asm` 経由で 0 を返す)、
`Zombie` を親の `wait()` が回収する、という流れが `kernel_main` の `parent_thread` で確認できる。

### phase8: ファイルシステム 🚧 ← いまここ

**まだ書きかけです。** 下から順に積んでいて、いまは「ブロックが読み書きできてキャッシュに乗る」ところまで。

できているもの (ブランチ `feature/file-system-*` 上。まだ `main` には入っていません):

- **higher-half kernel への移行** — ファイルシステムの前提として、カーネルを
  `0xFFFFFFFF80000000` に置き、全物理メモリの direct map (PML4[256]) を張る形に作り替えた
- **PCI コンフィグ空間の列挙** — ベンダ/デバイス ID でデバイスを探し、BAR を読み、bus master を有効化する
- **virtio-blk ドライバ** — virtqueue (descriptor / avail / used) を組んでセクタ単位の read/write。
  完了待ちはポーリング
- **バッファキャッシュ** — 512B × 30 ブロックの LRU キャッシュ。`read` / `mark_dirty` / `write` / `flush`。
  協調型スケジューラなのでロックは持たない (プリエンプティブにするなら xv6 同様バッファ単位の sleeplock が要る)
- ドライバ確認用のテスト用ディスクイメージ `fs.img` (先頭に文字列を置いて読み書きを確かめるだけのもの。
  まだファイルシステムのイメージではない)

これからやること:

- mkfs (ホスト側でイメージを作るツール) と、V6 の inode レイアウト
  (スーパーブロック / inode / ビットマップ / データブロック)
- inode 層 (`ialloc` / `iget` / `bmap` / `readi` / `writei`)
- ディレクトリと path 解決
- ファイルディスクリプタを作って `open` / `read` / `write` / `close` をシステムコールに接続
  (いまの `read` / `write` はコンソール直結、`exit` もプロセス管理と繋がずその場で停止している)

### phase9: シェル

未着手。`exec` を作って、キーボード入力からコマンドを起動する最小の `sh` を動かすのが目標。

## 現在の制約 / TODO

- スケジューラが協調型。タイマ割り込みからのプリエンプションはまだ入れていない
- `exec` がないので、ユーザープログラムはカーネルに埋め込んで User ページとしてマップしている
- ファイルディスクリプタ層がなく、`read`/`write` はキーボードと VGA に直結
- ロックが一切ない (シングル CPU + 協調型スケジューラという前提の上に乗っている)
- higher-half 移行とファイルシステムがまだ `main` にマージされていない
- `kernel_main` に各フェーズの動作確認コードがコメントアウトで残っている
