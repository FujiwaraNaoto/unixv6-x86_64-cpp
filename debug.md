

# How to debug

デバッグ方法


```sh
$ make run-gdb
```

```sh
$ gdb build/kernel.elf
(gdb) target remote :1234
```

実行例

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

