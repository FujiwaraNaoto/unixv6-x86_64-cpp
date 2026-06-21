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
                    │     ├── 6 システムコール
                    │     │     └── 7 ユーザーランド
                    │     │               └── 9 シェル
                    │     └── 8 ファイルシステム ───┘
```


```sh

フェーズ6: システムコール + キーボード入力
  ├── syscall/sysret ハンドラ
  ├── write, exit (まずカーネルから呼べる形)
  └── キーボードドライバ (IRQ1, PS/2スキャンコード変換, リングバッファ)

フェーズ7: ユーザーランド + fork
  ├── リング3への遷移 (iretq)
  ├── プロセスごとのアドレス空間 (PML4分離)
  └── fork / exec / exit / wait
  ```
