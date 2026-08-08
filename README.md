# unixv6-x86_64-cpp


```sh
make run-gui
```

```sh
make run-vscode
```

phase1: boot
phase2: IDT/割り込みハンドラ
phase3: 物理メモリアロケータ
phase4: 仮想メモリ
phase5: プロセス管理
phase6: システムコール
phase6: ユーザランド
phase8: ファイルシステム
phase9: シェル


```
1 Boot
  └── 2 割り込み
        └── 3 物理メモリ
              └── 4 仮想メモリ
                    ├── 5 プロセス管理
                    │     ├── 6 システムコール + キーボード
                    │     │     └── 7 ユーザーランド + fork
                    │     │               └── 9 シェル
                    │     └── 8 ファイルシステム ───┘
                    │
                    └── 10 higher-half 移行 (リファクタリング)


フェーズ6: システムコール + キーボード入力
  ├── syscall/sysret ハンドラ
  ├── write, exit (まずカーネルから呼べる形)
  └── キーボードドライバ (IRQ1, PS/2スキャンコード変換, リングバッファ)

フェーズ7: ユーザーランド + fork
  ├── リング3への遷移 (iretq)
  ├── プロセスごとのアドレス空間 (PML4分離)
  └── fork / exec / exit / wait
  ```

1 → 2 → 3 → 4 → 5 → 6 → 7 → 【10 higher-half移行】 → 8 → 9


# フェーズ7
フェーズ7: ユーザーランド + fork  ← 現在ここ

## セット1: リング3遷移 ✅

GDT 拡張 (ユーザーセグメント + TSS)
TSS.RSP0 設定
iretq でリング3へ降りる
sysret を本来の形に (o64 sysret)


## セット2: PML4分離  ← 次はこれ (アプローチA = identity map維持)

プロセスごとの PML4 生成
カーネル領域は共有、ユーザー領域は分離
コンテキストスイッチで CR3 切り替え
TSS.RSP0 のプロセスごと更新

## セット3: fork / exec / exit / wait

fork (アドレス空間複製, レジスタコピー, 親子で戻り値を変える)
exec (アドレス空間の差し替え, フェーズ8依存)
exit / wait (Zombie 回収, 資源解放)






# フェーズ10: higher-half kernel 移行 (アプローチB)  ★フェーズ7完了直後

- boot.asm の高位マッピング (0xFFFFFFFF80000000)
- kernel.ld のリンクアドレス変更 + AT() で物理ロード指定
- vmm の物理⇔仮想変換変更 (identity map → direct map)
- 高位アドレスへのジャンプ
- 低位 identity map の撤去
- 全フェーズが移行後も動くことの確認


# フェーズ8: ファイルシステム (higher-half前提)

- ブロックデバイスドライバ (ATA PIO or virtio-blk)
- V6 inode レイアウト
- ディレクトリ / ファイル操作
- open / read / write (システムコールと接続)


# フェーズ9: シェル


- 最小シェル sh
- exec でコマンド起動
- キーボード入力 (フェーズ6) からコマンドライン


# How to debug

```sh
$ make run-gdb
```

```sh
$ gdb build/kernel.elf
(gdb) target remote :1234
```

このあとは
他

```sh
(gdb) break kernel_main
(gdb) continue
(gdb) next        # 1行ずつ進める
```

```sh
(gdb) break vmm::VirtualMemoryManager::VirtualMemoryManager
(gdb) continue
(gdb) info registers cr3        # 構築前の正しいCR3(ブート時PML4)をメモ
(gdb) stepi                     # load_cr3 の mov cr3 命令まで進める
(gdb) info registers cr3        # 0 に書き換わる瞬間が見える

(gdb) stepi
48          load_cr3(pml4_phys_); // CR3の値を読み込む
(gdb) info registers cr3
cr3            0x10c000            [ PDBR=268 PCID=0 ]
(gdb) stepi
0x0000000000103e31      48          load_cr3(pml4_phys_); // CR3の値を読み込む
(gdb) info registers cr3
cr3            0x10c000            [ PDBR=268 PCID=0 ]
(gdb) stepi
0x000000000010160a in load_cr3 ()
(gdb) info registers cr3
cr3            0x10c000            [ PDBR=268 PCID=0 ]
(gdb) stepi
Cannot access memory at address 0x10160a
(gdb) info registers cr3
cr3            0x0                 [ PDBR=0 PCID=0 ]
(gdb) 
```
のようにしていく

