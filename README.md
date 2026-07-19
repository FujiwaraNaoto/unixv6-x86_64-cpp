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
