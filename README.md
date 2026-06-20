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

方針

先にフェーズ6（syscall）→ フェーズ7（ユーザーランド）を進めてから本格的な fork,sleep,wakeup を行う方針

read()システムコールと一緒にキーボードをやることにする
